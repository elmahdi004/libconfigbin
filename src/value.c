/*
 * value.c -- fixed-size element helpers and array value parsing
 */

#include "configbin.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* returns the wire size in bytes for fixed-size element types, or 0 if
   the type is variable-width or unknown */
static size_t type_fixed_size(uint8_t t)
{
    switch (t) {
    case BCFG_TYPE_BOOL:      return 1;
    case BCFG_TYPE_UINT32:    return 4;
    case BCFG_TYPE_INT64:     return 8;
    case BCFG_TYPE_FLOAT64:   return 8;
    case BCFG_TYPE_TIMESTAMP: return 8;
    default:                  return 0;
    }
}

const char *bcfg_type_name(uint8_t type)
{
    switch (type) {
    case BCFG_TYPE_NULL:      return "null";
    case BCFG_TYPE_UINT32:    return "uint32";
    case BCFG_TYPE_INT64:     return "int64";
    case BCFG_TYPE_FLOAT64:   return "float64";
    case BCFG_TYPE_STRING:    return "string";
    case BCFG_TYPE_BLOB:      return "blob";
    case BCFG_TYPE_ARRAY:     return "array";
    case BCFG_TYPE_SECREF:    return "secref";
    case BCFG_TYPE_BOOL:      return "bool";
    case BCFG_TYPE_TIMESTAMP: return "timestamp";
    default:                  return "unknown";
    }
}

/*
 * Parse array payload: [1B elem_type][2B count][data...]
 *
 * Only fixed-size element types are supported inside arrays.
 * *pp is updated past the consumed bytes on success.
 */
int bcfg_parse_array(const uint8_t **pp, const uint8_t *end,
                     bcfg_value *out)
{
    const uint8_t *p = *pp;

    if (end - p < 3)
        return BCFG_ERR_TRUNCATED;

    uint8_t elem_type = *p++;

    uint16_t count;
    memcpy(&count, p, 2);
    p += 2;
    /* assume little-endian host (common case; skip bswap for brevity) */

    size_t elem_size = type_fixed_size(elem_type);
    if (elem_size == 0)
        return BCFG_ERR_INVALID;   /* var-size or unknown elem type */

    /*
     * Compute the total byte span of the element block.
     *
     * BUG: both count and elem_size can together exceed UINT16_MAX.
     * For example: elem_type=INT64 (elem_size=8), count=8193 (0x2001).
     *   8193 * 8 = 65544, which truncates to 8 when stored in uint16_t.
     * The truncated value is then used for the bounds check AND the malloc,
     * so we allocate only 8 bytes but subsequently memcpy 65544 bytes into
     * that buffer — a heap buffer overflow.
     */
    uint16_t total_bytes = (uint16_t)(count * elem_size);

    if ((size_t)(end - p) < total_bytes)
        return BCFG_ERR_TRUNCATED;

    void *buf = malloc(total_bytes);
    if (!buf)
        return BCFG_ERR_NOMEM;

    /* copies count*elem_size bytes — may vastly exceed total_bytes */
    memcpy(buf, p, (size_t)count * elem_size);
    p += (size_t)count * elem_size;

    out->type            = BCFG_TYPE_ARRAY;
    out->v.array.elem_type = elem_type;
    out->v.array.count   = count;
    out->v.array.data    = buf;

    *pp = p;
    return BCFG_OK;
}

/*
 * Parse a single value starting at *pp.
 * On success *pp is advanced past the consumed bytes.
 */
int bcfg_parse_value(const uint8_t **pp, const uint8_t *end,
                     bcfg_value *out)
{
    const uint8_t *p = *pp;
    int rc;

    if (p >= end)
        return BCFG_ERR_TRUNCATED;

    uint8_t type = *p++;
    out->type = type;

    switch (type) {
    case BCFG_TYPE_NULL:
        break;

    case BCFG_TYPE_BOOL:
        if (p >= end) return BCFG_ERR_TRUNCATED;
        out->v.boolean = (*p++) ? 1 : 0;
        break;

    case BCFG_TYPE_UINT32:
        if (end - p < 4) return BCFG_ERR_TRUNCATED;
        memcpy(&out->v.u32, p, 4);
        p += 4;
        break;

    case BCFG_TYPE_INT64:
    case BCFG_TYPE_TIMESTAMP:
        if (end - p < 8) return BCFG_ERR_TRUNCATED;
        memcpy(&out->v.i64, p, 8);
        p += 8;
        break;

    case BCFG_TYPE_FLOAT64:
        if (end - p < 8) return BCFG_ERR_TRUNCATED;
        memcpy(&out->v.f64, p, 8);
        p += 8;
        break;

    case BCFG_TYPE_STRING: {
        if (end - p < 2) return BCFG_ERR_TRUNCATED;
        uint16_t slen;
        memcpy(&slen, p, 2); p += 2;
        if ((size_t)(end - p) < slen) return BCFG_ERR_TRUNCATED;
        char *s = (char *)malloc(slen + 1);
        if (!s) return BCFG_ERR_NOMEM;
        memcpy(s, p, slen);
        s[slen] = '\0';
        p += slen;
        out->v.str.len  = slen;
        out->v.str.data = s;
        break;
    }

    case BCFG_TYPE_BLOB: {
        if (end - p < 4) return BCFG_ERR_TRUNCATED;
        uint32_t blen;
        memcpy(&blen, p, 4); p += 4;
        if ((size_t)(end - p) < blen) return BCFG_ERR_TRUNCATED;
        uint8_t *b = (uint8_t *)malloc(blen ? blen : 1);
        if (!b) return BCFG_ERR_NOMEM;
        memcpy(b, p, blen);
        p += blen;
        out->v.blob.len  = blen;
        out->v.blob.data = b;
        break;
    }

    case BCFG_TYPE_ARRAY:
        rc = bcfg_parse_array(&p, end, out);
        if (rc != BCFG_OK) return rc;
        break;

    case BCFG_TYPE_SECREF:
        if (end - p < 2) return BCFG_ERR_TRUNCATED;
        memcpy(&out->v.secref, p, 2);
        p += 2;
        break;

    default:
        return BCFG_ERR_INVALID;
    }

    *pp = p;
    return BCFG_OK;
}

void bcfg_free_value(bcfg_value *v)
{
    if (!v) return;
    switch (v->type) {
    case BCFG_TYPE_STRING: free(v->v.str.data);   break;
    case BCFG_TYPE_BLOB:   free(v->v.blob.data);  break;
    case BCFG_TYPE_ARRAY:  free(v->v.array.data); break;
    default: break;
    }
}
