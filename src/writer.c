/*
 * writer.c -- encoder: serialize a bcfg_doc back to the v1 wire format.
 *
 * This is the inverse of the parser in configbin.c / value.c.  Layout:
 *   [4B magic][2B version][2B num_sections]
 *   section: [1B name_len][name][4B num_entries]
 *   entry:   [1B key_len][key][1B type][payload]
 *   array payload: [1B elem_type][2B count][count * elem_size bytes]
 *
 * All multi-byte fields are little-endian; the bcfg_buf put helpers already
 * emit little-endian, so we lean on them rather than hand-rolling byte order.
 */

#include "writer.h"

#include <string.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Wire size in bytes for the fixed-size element types that may legally appear
 * inside an array.  Returns 0 for variable-width or unknown types, matching
 * type_fixed_size() in value.c (the read side).
 */
static size_t writer_elem_size(uint8_t t)
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

/* Append an IEEE-754 double as 8 little-endian bytes via its bit pattern. */
static int write_f64(bcfg_buf *out, double d)
{
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);   /* type-pun safely through memcpy */
    return bcfg_buf_put_u64(out, bits);
}

/*
 * Serialize an array value's payload: [1B elem_type][2B count][data...].
 *
 * Only fixed-size element types are encodable; anything else is rejected with
 * BCFG_ERR_INVALID, mirroring the parser's refusal to read such arrays.  The
 * raw element block is written exactly as stored, since the in-memory layout
 * already matches the little-endian wire layout produced on the read side.
 */
static int write_array(const bcfg_value *v, bcfg_buf *out)
{
    uint8_t  elem_type = v->v.array.elem_type;
    uint16_t count     = v->v.array.count;

    size_t elem_size = writer_elem_size(elem_type);
    if (elem_size == 0)
        return BCFG_ERR_INVALID;   /* var-size or unknown element type */

    /* count == 0 is valid: no data block, just the header. */
    size_t total = (size_t)count * elem_size;
    if (total != 0 && v->v.array.data == NULL)
        return BCFG_ERR_INVALID;   /* claims elements but holds no buffer */

    int rc;
    if ((rc = bcfg_buf_put_u8(out, elem_type)) != BCFG_OK) return rc;
    if ((rc = bcfg_buf_put_u16(out, count))    != BCFG_OK) return rc;

    if (total != 0) {
        rc = bcfg_buf_put_bytes(out, v->v.array.data, total);
        if (rc != BCFG_OK) return rc;
    }
    return BCFG_OK;
}

/*
 * Encode a single value: its 1-byte type tag followed by the type-specific
 * payload.  NULL is rejected up front so callers never deref a missing value.
 */
int bcfg_write_value(const bcfg_value *v, bcfg_buf *out)
{
    if (!v || !out)
        return BCFG_ERR_INVALID;

    int rc = bcfg_buf_put_u8(out, v->type);
    if (rc != BCFG_OK)
        return rc;

    switch (v->type) {
    case BCFG_TYPE_NULL:
        /* no payload */
        return BCFG_OK;

    case BCFG_TYPE_UINT32:
        return bcfg_buf_put_u32(out, v->v.u32);

    case BCFG_TYPE_INT64:
        return bcfg_buf_put_u64(out, (uint64_t)v->v.i64);

    case BCFG_TYPE_FLOAT64:
        return write_f64(out, v->v.f64);

    case BCFG_TYPE_STRING: {
        /* [2B len][data]; a NULL pointer is only acceptable when len == 0. */
        uint16_t len = v->v.str.len;
        if (len != 0 && v->v.str.data == NULL)
            return BCFG_ERR_INVALID;
        if ((rc = bcfg_buf_put_u16(out, len)) != BCFG_OK)
            return rc;
        if (len != 0)
            return bcfg_buf_put_bytes(out, v->v.str.data, len);
        return BCFG_OK;
    }

    case BCFG_TYPE_BLOB: {
        /* [4B len][data]; same NULL-only-when-empty rule as strings. */
        uint32_t len = v->v.blob.len;
        if (len != 0 && v->v.blob.data == NULL)
            return BCFG_ERR_INVALID;
        if ((rc = bcfg_buf_put_u32(out, len)) != BCFG_OK)
            return rc;
        if (len != 0)
            return bcfg_buf_put_bytes(out, v->v.blob.data, len);
        return BCFG_OK;
    }

    case BCFG_TYPE_ARRAY:
        return write_array(v, out);

    case BCFG_TYPE_SECREF:
        return bcfg_buf_put_u16(out, v->v.secref);

    case BCFG_TYPE_BOOL:
        /* normalise to a strict 0/1 byte */
        return bcfg_buf_put_u8(out, v->v.boolean ? 1 : 0);

    case BCFG_TYPE_TIMESTAMP:
        return bcfg_buf_put_u64(out, (uint64_t)v->v.ts);

    default:
        return BCFG_ERR_INVALID;   /* unknown type tag */
    }
}

/* Encode one entry: [1B key_len][key][value]. */
static int write_entry(const bcfg_entry *e, bcfg_buf *out)
{
    /* key_len is a uint8_t, so the on-wire length cannot exceed 255 by
       construction; a non-empty key must carry a backing pointer. */
    if (e->key_len != 0 && e->key == NULL)
        return BCFG_ERR_INVALID;

    int rc = bcfg_buf_put_u8(out, e->key_len);
    if (rc != BCFG_OK)
        return rc;

    if (e->key_len != 0) {
        rc = bcfg_buf_put_bytes(out, e->key, e->key_len);
        if (rc != BCFG_OK)
            return rc;
    }

    return bcfg_write_value(&e->value, out);
}

/* Encode one section: [1B name_len][name][4B num_entries][entries...]. */
static int write_section(const bcfg_section *s, bcfg_buf *out)
{
    if (s->name_len != 0 && s->name == NULL)
        return BCFG_ERR_INVALID;
    /* a section with entries must actually hold the entry array */
    if (s->num_entries != 0 && s->entries == NULL)
        return BCFG_ERR_INVALID;

    int rc = bcfg_buf_put_u8(out, s->name_len);
    if (rc != BCFG_OK)
        return rc;

    if (s->name_len != 0) {
        rc = bcfg_buf_put_bytes(out, s->name, s->name_len);
        if (rc != BCFG_OK)
            return rc;
    }

    rc = bcfg_buf_put_u32(out, s->num_entries);
    if (rc != BCFG_OK)
        return rc;

    for (uint32_t i = 0; i < s->num_entries; i++) {
        rc = write_entry(&s->entries[i], out);
        if (rc != BCFG_OK)
            return rc;
    }
    return BCFG_OK;
}

/*
 * Encode a whole document: the fixed header followed by every section.  On
 * any error the bytes already appended to out are left in place (unspecified
 * per the contract); callers typically discard the buffer on failure.
 */
int bcfg_write_doc(const bcfg_doc *doc, bcfg_buf *out)
{
    if (!doc || !out)
        return BCFG_ERR_INVALID;
    if (doc->num_sections != 0 && doc->sections == NULL)
        return BCFG_ERR_INVALID;

    int rc;
    if ((rc = bcfg_buf_put_u32(out, BCFG_MAGIC))                != BCFG_OK) return rc;
    if ((rc = bcfg_buf_put_u16(out, (uint16_t)BCFG_VERSION))    != BCFG_OK) return rc;
    if ((rc = bcfg_buf_put_u16(out, doc->num_sections))         != BCFG_OK) return rc;

    for (uint16_t i = 0; i < doc->num_sections; i++) {
        rc = write_section(&doc->sections[i], out);
        if (rc != BCFG_OK)
            return rc;
    }
    return BCFG_OK;
}
