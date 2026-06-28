/*
 * ctext_write.c -- render a bcfg_doc back to canonical ".bct" text
 *
 * The inverse of ctext_parse.c: each section becomes a "[name]" header and
 * each entry a "key = value" line.  Scalars use the same surface syntax the
 * parser accepts (quoted/escaped strings, b16"" blobs, @n section refs,
 * t<ns> timestamps, [a, b, c] arrays), so a parse/write/parse cycle is a
 * fixed point for the supported types.
 */

#include "ctext.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int put_lit(bcfg_buf *b, const char *s)
{
    return bcfg_buf_put_cstr(b, s);
}

static int put_fmt(bcfg_buf *b, const char *fmt, ...)
{
    char    tmp[64];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return BCFG_ERR_INVALID;
    if ((size_t)n < sizeof(tmp))
        return bcfg_buf_put_bytes(b, tmp, (size_t)n);

    /* rare: rendering exceeded the stack buffer; do it on the heap */
    {
        char *big = (char *)malloc((size_t)n + 1);
        if (!big) return BCFG_ERR_NOMEM;
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        int rc = bcfg_buf_put_bytes(b, big, (size_t)n);
        free(big);
        return rc;
    }
}

static int put_quoted(bcfg_buf *b, const char *s, size_t len)
{
    if (bcfg_buf_put_u8(b, '"')) return BCFG_ERR_NOMEM;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        int rc = BCFG_OK;
        switch (c) {
        case '\n': rc = put_lit(b, "\\n");  break;
        case '\t': rc = put_lit(b, "\\t");  break;
        case '\r': rc = put_lit(b, "\\r");  break;
        case '"':  rc = put_lit(b, "\\\""); break;
        case '\\': rc = put_lit(b, "\\\\"); break;
        default:
            if (c < 0x20 || c > 0x7e) rc = put_fmt(b, "\\x%02X", c);
            else                      rc = bcfg_buf_put_u8(b, c);
            break;
        }
        if (rc) return rc;
    }
    return bcfg_buf_put_u8(b, '"');
}

static int put_array(bcfg_buf *b, const bcfg_value *v)
{
    const uint8_t *d = (const uint8_t *)v->v.array.data;
    uint8_t  et = v->v.array.elem_type;
    uint16_t n  = v->v.array.count;
    int rc;

    if ((rc = bcfg_buf_put_u8(b, '['))) return rc;
    for (uint16_t i = 0; i < n; i++) {
        if (i && (rc = put_lit(b, ", "))) return rc;
        switch (et) {
        case BCFG_TYPE_UINT32: {
            uint32_t u; memcpy(&u, d + (size_t)i * 4, 4);
            rc = put_fmt(b, "%u", u);
            break;
        }
        case BCFG_TYPE_INT64: {
            int64_t x; memcpy(&x, d + (size_t)i * 8, 8);
            rc = put_fmt(b, "%lld", (long long)x);
            break;
        }
        case BCFG_TYPE_TIMESTAMP: {
            int64_t x; memcpy(&x, d + (size_t)i * 8, 8);
            rc = put_fmt(b, "t%lld", (long long)x);
            break;
        }
        case BCFG_TYPE_FLOAT64: {
            double f; memcpy(&f, d + (size_t)i * 8, 8);
            rc = put_fmt(b, "%.17g", f);
            break;
        }
        case BCFG_TYPE_BOOL:
            rc = put_lit(b, d[i] ? "true" : "false");
            break;
        default:
            rc = BCFG_ERR_INVALID;
            break;
        }
        if (rc) return rc;
    }
    return bcfg_buf_put_u8(b, ']');
}

static int put_value(bcfg_buf *b, const bcfg_value *v)
{
    switch (v->type) {
    case BCFG_TYPE_NULL:      return put_lit(b, "null");
    case BCFG_TYPE_UINT32:    return put_fmt(b, "%u", v->v.u32);
    case BCFG_TYPE_INT64:     return put_fmt(b, "%lld", (long long)v->v.i64);
    case BCFG_TYPE_TIMESTAMP: return put_fmt(b, "t%lld", (long long)v->v.ts);
    case BCFG_TYPE_FLOAT64:   return put_fmt(b, "%.17g", v->v.f64);
    case BCFG_TYPE_BOOL:      return put_lit(b, v->v.boolean ? "true" : "false");
    case BCFG_TYPE_SECREF:    return put_fmt(b, "@%u", v->v.secref);
    case BCFG_TYPE_STRING:    return put_quoted(b, v->v.str.data, v->v.str.len);
    case BCFG_TYPE_BLOB: {
        int rc = put_lit(b, "b16\"");
        if (rc) return rc;
        for (uint32_t i = 0; i < v->v.blob.len; i++)
            if ((rc = put_fmt(b, "%02X", v->v.blob.data[i]))) return rc;
        return bcfg_buf_put_u8(b, '"');
    }
    case BCFG_TYPE_ARRAY:     return put_array(b, v);
    default:                  return BCFG_ERR_INVALID;
    }
}

int bcfg_ctext_write(const bcfg_doc *doc, bcfg_buf *out)
{
    int rc;

    if (!doc || !out) return BCFG_ERR_INVALID;

    for (uint16_t i = 0; i < doc->num_sections; i++) {
        const bcfg_section *s = &doc->sections[i];

        if (i && (rc = bcfg_buf_put_u8(out, '\n'))) return rc;
        if ((rc = bcfg_buf_put_u8(out, '['))) return rc;
        if ((rc = bcfg_buf_put_bytes(out, s->name, s->name_len))) return rc;
        if ((rc = put_lit(out, "]\n"))) return rc;

        for (uint32_t j = 0; j < s->num_entries; j++) {
            const bcfg_entry *e = &s->entries[j];
            if ((rc = bcfg_buf_put_bytes(out, e->key, e->key_len))) return rc;
            if ((rc = put_lit(out, " = "))) return rc;
            if ((rc = put_value(out, &e->value))) return rc;
            if ((rc = bcfg_buf_put_u8(out, '\n'))) return rc;
        }
    }
    return BCFG_OK;
}
