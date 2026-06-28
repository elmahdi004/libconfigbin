/*
 * cjson_write.c -- serialise a bcfg_doc to subset JSON
 *
 * Implements bcfg_json_write() declared in cjson.h.  The output is the inverse
 * of the parse mapping: the document becomes a top-level JSON object whose
 * members are sections; each section is an object whose members are entries.
 *
 * Value rendering (round-trips the supported types):
 *   UINT32  -> JSON integer
 *   INT64   -> JSON integer
 *   FLOAT64 -> JSON real (always with a fractional part / exponent so it
 *              re-parses as a real, not an integer)
 *   STRING  -> JSON string (escaped)
 *   BOOL    -> true / false
 *   NULL    -> null
 *   ARRAY   -> JSON array of numbers (uint32 / int64 / float64 by elem_type)
 *
 * Types with no JSON mapping (BLOB, SECREF, TIMESTAMP) and arrays of
 * unsupported element types are not representable; they are emitted as JSON
 * null so the output stays well-formed and the call still succeeds.
 */

#include "cjson.h"

#include "configbin.h"
#include "buffer.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* low-level append helpers                                            */
/* ------------------------------------------------------------------ */

/* Append a literal C string (sans NUL).  Returns BCFG_OK / BCFG_ERR_NOMEM. */
static int emit_lit(bcfg_buf *out, const char *s)
{
    return bcfg_buf_put_cstr(out, s);
}

/* Append `n` indentation units (2 spaces each) when pretty-printing. */
static int emit_indent(bcfg_buf *out, int pretty, int depth)
{
    if (!pretty)
        return BCFG_OK;
    for (int i = 0; i < depth; i++) {
        int rc = bcfg_buf_put_bytes(out, "  ", 2);
        if (rc != BCFG_OK)
            return rc;
    }
    return BCFG_OK;
}

/* Append a newline only when pretty-printing. */
static int emit_newline(bcfg_buf *out, int pretty)
{
    if (!pretty)
        return BCFG_OK;
    return bcfg_buf_put_u8(out, '\n');
}

/* ------------------------------------------------------------------ */
/* JSON string escaping                                               */
/* ------------------------------------------------------------------ */

/*
 * Emit a JSON string literal for the first `len` bytes of `s`, including the
 * surrounding double quotes.  All control characters and the two mandatory
 * escapes (\" and \\) are handled; bytes >= 0x20 pass through verbatim so
 * UTF-8 content is preserved.
 */
static int emit_json_string(bcfg_buf *out, const char *s, size_t len)
{
    int rc = bcfg_buf_put_u8(out, '"');
    if (rc != BCFG_OK)
        return rc;

    if (s != NULL) {
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)s[i];
            switch (c) {
            case '"':  rc = bcfg_buf_put_bytes(out, "\\\"", 2); break;
            case '\\': rc = bcfg_buf_put_bytes(out, "\\\\", 2); break;
            case '\b': rc = bcfg_buf_put_bytes(out, "\\b", 2);  break;
            case '\f': rc = bcfg_buf_put_bytes(out, "\\f", 2);  break;
            case '\n': rc = bcfg_buf_put_bytes(out, "\\n", 2);  break;
            case '\r': rc = bcfg_buf_put_bytes(out, "\\r", 2);  break;
            case '\t': rc = bcfg_buf_put_bytes(out, "\\t", 2);  break;
            default:
                if (c < 0x20) {
                    /* other control char -> \u00XX */
                    char esc[7];
                    snprintf(esc, sizeof esc, "\\u%04x", (unsigned)c);
                    rc = bcfg_buf_put_bytes(out, esc, 6);
                } else {
                    rc = bcfg_buf_put_u8(out, c);
                }
                break;
            }
            if (rc != BCFG_OK)
                return rc;
        }
    }

    return bcfg_buf_put_u8(out, '"');
}

/* ------------------------------------------------------------------ */
/* number rendering                                                   */
/* ------------------------------------------------------------------ */

static int emit_u32(bcfg_buf *out, uint32_t v)
{
    char tmp[16];
    int n = snprintf(tmp, sizeof tmp, "%lu", (unsigned long)v);
    if (n < 0)
        return BCFG_ERR_INVALID;
    return bcfg_buf_put_bytes(out, tmp, (size_t)n);
}

static int emit_i64(bcfg_buf *out, int64_t v)
{
    char tmp[24];
    int n = snprintf(tmp, sizeof tmp, "%lld", (long long)v);
    if (n < 0)
        return BCFG_ERR_INVALID;
    return bcfg_buf_put_bytes(out, tmp, (size_t)n);
}

/*
 * Render a double as JSON.  Non-finite values (NaN, +/-Inf) have no JSON
 * representation, so they are emitted as null.  Finite values use %.17g for a
 * round-trippable representation; a ".0" suffix is appended when the rendered
 * text would otherwise look like an integer, so the value re-parses as a real.
 */
static int emit_f64(bcfg_buf *out, double v)
{
    if (!isfinite(v))
        return emit_lit(out, "null");

    char tmp[40];
    int n = snprintf(tmp, sizeof tmp, "%.17g", v);
    if (n < 0)
        return BCFG_ERR_INVALID;

    int has_dot_or_exp = 0;
    for (int i = 0; i < n; i++) {
        char c = tmp[i];
        if (c == '.' || c == 'e' || c == 'E' ||
            c == 'n' || c == 'N' /* nan */ ||
            c == 'i' || c == 'I' /* inf */) {
            has_dot_or_exp = 1;
            break;
        }
    }

    int rc = bcfg_buf_put_bytes(out, tmp, (size_t)n);
    if (rc != BCFG_OK)
        return rc;
    if (!has_dot_or_exp)
        rc = bcfg_buf_put_bytes(out, ".0", 2);
    return rc;
}

/* ------------------------------------------------------------------ */
/* array rendering                                                    */
/* ------------------------------------------------------------------ */

/*
 * Emit a numeric array.  Only uint32/int64/float64 element types are
 * representable; any other element type produces an empty array so the output
 * remains well-formed.
 */
static int emit_array(bcfg_buf *out, const bcfg_value *val)
{
    int rc = bcfg_buf_put_u8(out, '[');
    if (rc != BCFG_OK)
        return rc;

    uint8_t et = val->v.array.elem_type;
    uint16_t count = val->v.array.count;
    const void *data = val->v.array.data;

    int representable = (et == BCFG_TYPE_UINT32 ||
                         et == BCFG_TYPE_INT64 ||
                         et == BCFG_TYPE_FLOAT64);

    if (data != NULL && representable) {
        for (uint16_t i = 0; i < count; i++) {
            if (i > 0) {
                rc = bcfg_buf_put_u8(out, ',');
                if (rc != BCFG_OK)
                    return rc;
            }
            switch (et) {
            case BCFG_TYPE_UINT32: {
                uint32_t u;
                memcpy(&u, (const uint8_t *)data + (size_t)i * 4, 4);
                rc = emit_u32(out, u);
                break;
            }
            case BCFG_TYPE_INT64: {
                int64_t s;
                memcpy(&s, (const uint8_t *)data + (size_t)i * 8, 8);
                rc = emit_i64(out, s);
                break;
            }
            case BCFG_TYPE_FLOAT64: {
                double d;
                memcpy(&d, (const uint8_t *)data + (size_t)i * 8, 8);
                rc = emit_f64(out, d);
                break;
            }
            default:
                rc = BCFG_ERR_INVALID; /* unreachable */
                break;
            }
            if (rc != BCFG_OK)
                return rc;
        }
    }

    return bcfg_buf_put_u8(out, ']');
}

/* ------------------------------------------------------------------ */
/* single value rendering                                             */
/* ------------------------------------------------------------------ */

static int emit_value(bcfg_buf *out, const bcfg_value *val)
{
    switch (val->type) {
    case BCFG_TYPE_NULL:
        return emit_lit(out, "null");
    case BCFG_TYPE_UINT32:
        return emit_u32(out, val->v.u32);
    case BCFG_TYPE_INT64:
        return emit_i64(out, val->v.i64);
    case BCFG_TYPE_FLOAT64:
        return emit_f64(out, val->v.f64);
    case BCFG_TYPE_STRING:
        return emit_json_string(out, val->v.str.data, val->v.str.len);
    case BCFG_TYPE_BOOL:
        return emit_lit(out, val->v.boolean ? "true" : "false");
    case BCFG_TYPE_ARRAY:
        return emit_array(out, val);

    /* No JSON representation in the subset mapping; emit null. */
    case BCFG_TYPE_BLOB:
    case BCFG_TYPE_SECREF:
    case BCFG_TYPE_TIMESTAMP:
    default:
        return emit_lit(out, "null");
    }
}

/* ------------------------------------------------------------------ */
/* section rendering                                                  */
/* ------------------------------------------------------------------ */

static int emit_section(bcfg_buf *out, const bcfg_section *sec,
                        int pretty, int depth)
{
    int rc = bcfg_buf_put_u8(out, '{');
    if (rc != BCFG_OK)
        return rc;

    uint32_t n = sec->entries ? sec->num_entries : 0;

    if (n > 0) {
        rc = emit_newline(out, pretty);
        if (rc != BCFG_OK)
            return rc;

        for (uint32_t i = 0; i < n; i++) {
            const bcfg_entry *e = &sec->entries[i];

            rc = emit_indent(out, pretty, depth + 1);
            if (rc != BCFG_OK)
                return rc;

            rc = emit_json_string(out, e->key, e->key_len);
            if (rc != BCFG_OK)
                return rc;

            rc = emit_lit(out, pretty ? ": " : ":");
            if (rc != BCFG_OK)
                return rc;

            rc = emit_value(out, &e->value);
            if (rc != BCFG_OK)
                return rc;

            if (i + 1 < n) {
                rc = bcfg_buf_put_u8(out, ',');
                if (rc != BCFG_OK)
                    return rc;
            }
            rc = emit_newline(out, pretty);
            if (rc != BCFG_OK)
                return rc;
        }

        rc = emit_indent(out, pretty, depth);
        if (rc != BCFG_OK)
            return rc;
    }

    return bcfg_buf_put_u8(out, '}');
}

/* ------------------------------------------------------------------ */
/* public entry point                                                 */
/* ------------------------------------------------------------------ */

int bcfg_json_write(const bcfg_doc *doc, bcfg_buf *out, int pretty)
{
    if (doc == NULL || out == NULL)
        return BCFG_ERR_INVALID;

    int rc = bcfg_buf_put_u8(out, '{');
    if (rc != BCFG_OK)
        return rc;

    uint16_t n = doc->sections ? doc->num_sections : 0;

    if (n > 0) {
        rc = emit_newline(out, pretty);
        if (rc != BCFG_OK)
            return rc;

        for (uint16_t i = 0; i < n; i++) {
            const bcfg_section *sec = &doc->sections[i];

            rc = emit_indent(out, pretty, 1);
            if (rc != BCFG_OK)
                return rc;

            rc = emit_json_string(out, sec->name, sec->name_len);
            if (rc != BCFG_OK)
                return rc;

            rc = emit_lit(out, pretty ? ": " : ":");
            if (rc != BCFG_OK)
                return rc;

            rc = emit_section(out, sec, pretty, 1);
            if (rc != BCFG_OK)
                return rc;

            if (i + 1 < n) {
                rc = bcfg_buf_put_u8(out, ',');
                if (rc != BCFG_OK)
                    return rc;
            }
            rc = emit_newline(out, pretty);
            if (rc != BCFG_OK)
                return rc;
        }
    }

    return bcfg_buf_put_u8(out, '}');
}
