/*
 * dump.c -- human-readable rendering
 *
 * Pretty-printers for inspection and debugging:
 *   - bcfg_value_to_string: format one scalar into a caller buffer (snprintf-ish)
 *   - bcfg_dump_text:       INI/TOML-ish "[section]" + "key = value" view
 *   - bcfg_dump_hex:        classic offset/hex/ascii hexdump
 *
 * Text output is appended to a growable bcfg_buf.  All entry points tolerate
 * NULL inputs and propagate BCFG_ERR_NOMEM on allocation failure.
 */

#include "dump.h"

#include <stdarg.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Cap on the number of blob bytes rendered as hex in the text view, so a
   large blob does not flood the output.  Longer blobs get an ellipsis. */
#define BCFG_DUMP_BLOB_CAP 32

/* Cap on the number of array elements rendered in the text view. */
#define BCFG_DUMP_ARRAY_CAP 64

/* ------------------------------------------------------------------ */
/* small append helpers                                                 */
/* ------------------------------------------------------------------ */

/* Append a NUL-terminated C string; returns BCFG_OK / BCFG_ERR_NOMEM. */
static int put_str(bcfg_buf *out, const char *s)
{
    return bcfg_buf_put_cstr(out, s);
}

/* Append a formatted line via a temporary stack buffer.  Anything that does
   not fit the scratch buffer is truncated, which is fine for a debug dump. */
static int put_fmt(bcfg_buf *out, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);

    if (n < 0)
        return BCFG_ERR_INVALID;
    /* vsnprintf NUL-terminates; put_cstr stops at the NUL regardless of n. */
    return bcfg_buf_put_cstr(out, tmp);
}

/* Append a string with surrounding double quotes, escaping the few control
   characters that would otherwise corrupt the single-line layout. */
static int put_quoted(bcfg_buf *out, const char *s, size_t len)
{
    int rc;
    if ((rc = bcfg_buf_put_u8(out, '"')) != BCFG_OK)
        return rc;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char ubuf[8];

        switch (c) {
        case '"':  esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n";  break;
        case '\r': esc = "\\r";  break;
        case '\t': esc = "\\t";  break;
        default:   break;
        }

        if (esc) {
            if ((rc = put_str(out, esc)) != BCFG_OK)
                return rc;
        } else if (c < 0x20 || c == 0x7f) {
            /* other non-printables -> \xHH */
            snprintf(ubuf, sizeof ubuf, "\\x%02x", c);
            if ((rc = put_str(out, ubuf)) != BCFG_OK)
                return rc;
        } else {
            if ((rc = bcfg_buf_put_u8(out, c)) != BCFG_OK)
                return rc;
        }
    }
    return bcfg_buf_put_u8(out, '"');
}

/* ------------------------------------------------------------------ */
/* scalar -> string                                                     */
/* ------------------------------------------------------------------ */

/* Read one fixed-size array element at index i into a bcfg_value scalar.
   Returns 0 if the element type is not a renderable fixed scalar. */
static int array_elem_value(const bcfg_value *arr, uint16_t i, bcfg_value *out)
{
    const uint8_t *base = (const uint8_t *)arr->v.array.data;
    if (!base)
        return 0;

    out->type = arr->v.array.elem_type;
    switch (arr->v.array.elem_type) {
    case BCFG_TYPE_BOOL:
        out->v.boolean = base[i] ? 1 : 0;
        return 1;
    case BCFG_TYPE_UINT32:
        memcpy(&out->v.u32, base + (size_t)i * 4, 4);
        return 1;
    case BCFG_TYPE_INT64:
        memcpy(&out->v.i64, base + (size_t)i * 8, 8);
        return 1;
    case BCFG_TYPE_TIMESTAMP:
        out->type = BCFG_TYPE_TIMESTAMP;
        memcpy(&out->v.ts, base + (size_t)i * 8, 8);
        return 1;
    case BCFG_TYPE_FLOAT64:
        memcpy(&out->v.f64, base + (size_t)i * 8, 8);
        return 1;
    default:
        return 0;
    }
}

size_t bcfg_value_to_string(const bcfg_value *v, char *buf, size_t buflen)
{
    /* Helper that mimics snprintf's return contract even for buflen == 0
       (in which case buf may legitimately be NULL). */
    int n;

    if (!v) {
        n = snprintf(buf, buflen, "<null>");
        return (n < 0) ? 0 : (size_t)n;
    }

    switch (v->type) {
    case BCFG_TYPE_NULL:
        n = snprintf(buf, buflen, "null");
        break;
    case BCFG_TYPE_BOOL:
        n = snprintf(buf, buflen, "%s", v->v.boolean ? "true" : "false");
        break;
    case BCFG_TYPE_UINT32:
        n = snprintf(buf, buflen, "%" PRIu32, v->v.u32);
        break;
    case BCFG_TYPE_INT64:
        n = snprintf(buf, buflen, "%" PRId64, v->v.i64);
        break;
    case BCFG_TYPE_TIMESTAMP:
        /* render with a hint that the units are unix nanoseconds */
        n = snprintf(buf, buflen, "%" PRId64 "ns", v->v.ts);
        break;
    case BCFG_TYPE_FLOAT64:
        n = snprintf(buf, buflen, "%g", v->v.f64);
        break;
    case BCFG_TYPE_SECREF:
        n = snprintf(buf, buflen, "@section[%" PRIu16 "]", v->v.secref);
        break;
    case BCFG_TYPE_STRING:
        /* scalar form: surround with quotes but do not escape (the buffer
           form is single-value, callers wanting escaping use dump_text) */
        n = snprintf(buf, buflen, "\"%s\"",
                     v->v.str.data ? v->v.str.data : "");
        break;
    case BCFG_TYPE_BLOB:
        n = snprintf(buf, buflen, "<blob %" PRIu32 " bytes>", v->v.blob.len);
        break;
    case BCFG_TYPE_ARRAY:
        n = snprintf(buf, buflen, "<array %s[%" PRIu16 "]>",
                     bcfg_type_name(v->v.array.elem_type), v->v.array.count);
        break;
    default:
        n = snprintf(buf, buflen, "<type 0x%02x>", v->type);
        break;
    }

    if (n < 0) {
        if (buflen)
            buf[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

/* ------------------------------------------------------------------ */
/* value -> buffer (rich, escaped, full arrays/blobs)                   */
/* ------------------------------------------------------------------ */

/* Append a blob as hex, truncated to BCFG_DUMP_BLOB_CAP bytes. */
static int append_blob(bcfg_buf *out, const uint8_t *data, uint32_t len)
{
    int rc;
    uint32_t shown = (len < BCFG_DUMP_BLOB_CAP) ? len : BCFG_DUMP_BLOB_CAP;

    if ((rc = put_fmt(out, "blob(%" PRIu32 ") 0x", len)) != BCFG_OK)
        return rc;
    if (len && !data)
        return put_str(out, "<null>");

    for (uint32_t i = 0; i < shown; i++) {
        char hx[3];
        snprintf(hx, sizeof hx, "%02x", data[i]);
        if ((rc = put_str(out, hx)) != BCFG_OK)
            return rc;
    }
    if (shown < len)
        return put_str(out, "...");
    return BCFG_OK;
}

/* Append an array as "[a, b, c]" with each element rendered via the scalar
   formatter; element count is capped to keep the dump bounded. */
static int append_array(bcfg_buf *out, const bcfg_value *v)
{
    int rc;
    uint16_t count = v->v.array.count;
    uint16_t shown = (count < BCFG_DUMP_ARRAY_CAP) ? count : BCFG_DUMP_ARRAY_CAP;

    if ((rc = bcfg_buf_put_u8(out, '[')) != BCFG_OK)
        return rc;

    for (uint16_t i = 0; i < shown; i++) {
        char cell[64];
        bcfg_value ev;

        if (i) {
            if ((rc = put_str(out, ", ")) != BCFG_OK)
                return rc;
        }
        if (array_elem_value(v, i, &ev)) {
            bcfg_value_to_string(&ev, cell, sizeof cell);
            if ((rc = put_str(out, cell)) != BCFG_OK)
                return rc;
        } else {
            if ((rc = put_str(out, "?")) != BCFG_OK)
                return rc;
        }
    }
    if (shown < count) {
        if ((rc = put_str(out, ", ...")) != BCFG_OK)
            return rc;
    }
    return bcfg_buf_put_u8(out, ']');
}

/* Append the value portion of a "key = value" line. */
static int append_value(bcfg_buf *out, const bcfg_value *v)
{
    char scratch[64];

    switch (v->type) {
    case BCFG_TYPE_STRING:
        return put_quoted(out, v->v.str.data ? v->v.str.data : "",
                          v->v.str.len);
    case BCFG_TYPE_BLOB:
        return append_blob(out, v->v.blob.data, v->v.blob.len);
    case BCFG_TYPE_ARRAY:
        return append_array(out, v);
    default:
        bcfg_value_to_string(v, scratch, sizeof scratch);
        return put_str(out, scratch);
    }
}

/* ------------------------------------------------------------------ */
/* text dump                                                            */
/* ------------------------------------------------------------------ */

int bcfg_dump_text(const bcfg_doc *doc, bcfg_buf *out)
{
    int rc;

    if (!out)
        return BCFG_ERR_INVALID;
    if (!doc)
        return put_str(out, "<null doc>\n");

    for (uint16_t s = 0; s < doc->num_sections; s++) {
        const bcfg_section *sec = &doc->sections[s];
        const char *name = sec->name ? sec->name : "";

        if (s) {
            if ((rc = bcfg_buf_put_u8(out, '\n')) != BCFG_OK)
                return rc;
        }
        if ((rc = put_fmt(out, "[%s]\n", name)) != BCFG_OK)
            return rc;

        for (uint32_t e = 0; e < sec->num_entries; e++) {
            const bcfg_entry *ent = &sec->entries[e];
            const char *key = ent->key ? ent->key : "";

            if ((rc = put_fmt(out, "  %s = ", key)) != BCFG_OK)
                return rc;
            if ((rc = append_value(out, &ent->value)) != BCFG_OK)
                return rc;
            if ((rc = bcfg_buf_put_u8(out, '\n')) != BCFG_OK)
                return rc;
        }
    }
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* hexdump                                                              */
/* ------------------------------------------------------------------ */

int bcfg_dump_hex(const uint8_t *data, size_t n, bcfg_buf *out)
{
    int rc;

    if (!out)
        return BCFG_ERR_INVALID;
    if (!data && n)
        return BCFG_ERR_INVALID;

    /* one line per 16 bytes: "OFFSET  hh hh ...  |ascii|" */
    for (size_t off = 0; off < n; off += 16) {
        char line[8];
        size_t i;

        snprintf(line, sizeof line, "%08zx", off);
        if ((rc = put_str(out, line)) != BCFG_OK)
            return rc;
        if ((rc = put_str(out, "  ")) != BCFG_OK)
            return rc;

        /* hex columns, with an extra gap after the 8th byte */
        for (i = 0; i < 16; i++) {
            if (off + i < n) {
                char hx[4];
                snprintf(hx, sizeof hx, "%02x ", data[off + i]);
                if ((rc = put_str(out, hx)) != BCFG_OK)
                    return rc;
            } else {
                if ((rc = put_str(out, "   ")) != BCFG_OK)
                    return rc;
            }
            if (i == 7) {
                if ((rc = bcfg_buf_put_u8(out, ' ')) != BCFG_OK)
                    return rc;
            }
        }

        /* ascii gutter */
        if ((rc = put_str(out, " |")) != BCFG_OK)
            return rc;
        for (i = 0; i < 16 && off + i < n; i++) {
            unsigned char c = data[off + i];
            char ch = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
            if ((rc = bcfg_buf_put_u8(out, (uint8_t)ch)) != BCFG_OK)
                return rc;
        }
        if ((rc = put_str(out, "|\n")) != BCFG_OK)
            return rc;
    }
    return BCFG_OK;
}
