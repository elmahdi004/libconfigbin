/*
 * hash.c -- hashing utilities
 *
 * Byte-level primitives (FNV-1a, CRC-32) and structural hashes for the
 * parsed value/entry/section types.  The structural hashes fold the type
 * tag together with the scalar bytes, string/blob payload, or array element
 * block so that two values hash equal iff they are byte-for-byte equivalent.
 *
 * All multi-byte scalars are hashed in little-endian order, matching the
 * convention used by the parser (configbin.c / value.c).
 */

#include "hash.h"

#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* FNV-1a (32-bit)                                                      */
/* ------------------------------------------------------------------ */

#define FNV1A_OFFSET 0x811c9dc5U
#define FNV1A_PRIME  0x01000193U

uint32_t bcfg_fnv1a(const void *data, size_t n)
{
    uint32_t h = FNV1A_OFFSET;
    const uint8_t *p = (const uint8_t *)data;

    /* A NULL buffer is treated as the empty input (returns the offset
       basis), so callers need not special-case zero-length fields. */
    if (!p)
        return h;

    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= FNV1A_PRIME;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* CRC-32 (reflected, polynomial 0xEDB88320)                           */
/* ------------------------------------------------------------------ */

static uint32_t crc32_table[256];
static int      crc32_table_ready = 0;

/* Build the 256-entry lookup table once, on first use. */
static void crc32_build_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

uint32_t bcfg_crc32(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;

    if (!crc32_table_ready)
        crc32_build_table();

    if (!p)
        return crc ^ 0xFFFFFFFFU;   /* CRC of the empty message == 0 */

    for (size_t i = 0; i < n; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFFU] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFFU;
}

/* ------------------------------------------------------------------ */
/* 64-bit FNV-1a mixing core for the structural hashes                 */
/* ------------------------------------------------------------------ */

#define FNV1A64_OFFSET 0xcbf29ce484222325ULL
#define FNV1A64_PRIME  0x00000100000001b3ULL

/* Fold n bytes at p into the running 64-bit FNV-1a accumulator. */
static uint64_t hash64_bytes(uint64_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!p)
        return h;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= FNV1A64_PRIME;
    }
    return h;
}

/* Fold a single byte into the accumulator. */
static uint64_t hash64_u8(uint64_t h, uint8_t b)
{
    h ^= b;
    h *= FNV1A64_PRIME;
    return h;
}

/*
 * Fold a length prefix into the accumulator before the corresponding
 * payload.  Mixing the length defends against ambiguity between adjacent
 * variable-width fields (e.g. ["ab",""] vs ["a","b"]).  Encoded
 * little-endian as a fixed 8 bytes for stability across field widths.
 */
static uint64_t hash64_len(uint64_t h, uint64_t len)
{
    uint8_t buf[8];
    for (int i = 0; i < 8; i++)
        buf[i] = (uint8_t)(len >> (8 * i));
    return hash64_bytes(h, buf, sizeof buf);
}

/*
 * Wire size in bytes for fixed-size element types, or 0 if the type is
 * variable-width or unknown.  Mirrors value.c's private helper; arrays in
 * this format only ever carry fixed-size elements.
 */
static size_t elem_fixed_size(uint8_t t)
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

/* ------------------------------------------------------------------ */
/* structural hashes                                                    */
/* ------------------------------------------------------------------ */

uint64_t bcfg_value_hash(const bcfg_value *v)
{
    uint64_t h = FNV1A64_OFFSET;

    if (!v)
        return h;

    /* The type tag always leads, so distinct types diverge immediately
       even when their payload bytes happen to coincide. */
    h = hash64_u8(h, v->type);

    switch (v->type) {
    case BCFG_TYPE_NULL:
        /* No payload: the tag alone identifies the value. */
        break;

    case BCFG_TYPE_BOOL:
        h = hash64_u8(h, v->v.boolean ? 1 : 0);
        break;

    case BCFG_TYPE_UINT32: {
        uint8_t buf[4];
        uint32_t x = v->v.u32;
        for (int i = 0; i < 4; i++)
            buf[i] = (uint8_t)(x >> (8 * i));
        h = hash64_bytes(h, buf, sizeof buf);
        break;
    }

    case BCFG_TYPE_INT64:
    case BCFG_TYPE_TIMESTAMP: {
        /* i64 and ts share storage; both fold their 8 LE bytes. */
        uint8_t buf[8];
        uint64_t x = (uint64_t)v->v.i64;
        for (int i = 0; i < 8; i++)
            buf[i] = (uint8_t)(x >> (8 * i));
        h = hash64_bytes(h, buf, sizeof buf);
        break;
    }

    case BCFG_TYPE_FLOAT64: {
        /* Hash the raw IEEE-754 bit pattern, little-endian. */
        uint64_t bits;
        memcpy(&bits, &v->v.f64, sizeof bits);
        uint8_t buf[8];
        for (int i = 0; i < 8; i++)
            buf[i] = (uint8_t)(bits >> (8 * i));
        h = hash64_bytes(h, buf, sizeof buf);
        break;
    }

    case BCFG_TYPE_SECREF: {
        uint8_t buf[2];
        uint16_t x = v->v.secref;
        buf[0] = (uint8_t)(x & 0xFF);
        buf[1] = (uint8_t)(x >> 8);
        h = hash64_bytes(h, buf, sizeof buf);
        break;
    }

    case BCFG_TYPE_STRING:
        /* length first, then the character data (NUL not included) */
        h = hash64_len(h, v->v.str.len);
        if (v->v.str.len)
            h = hash64_bytes(h, v->v.str.data, v->v.str.len);
        break;

    case BCFG_TYPE_BLOB:
        h = hash64_len(h, v->v.blob.len);
        if (v->v.blob.len)
            h = hash64_bytes(h, v->v.blob.data, v->v.blob.len);
        break;

    case BCFG_TYPE_ARRAY: {
        /* Mix element type and count, then the raw element block.  The
           block is count * elem_size contiguous little-endian bytes, so
           hashing it wholesale reproduces element order and values. */
        uint8_t et = v->v.array.elem_type;
        h = hash64_u8(h, et);
        h = hash64_len(h, v->v.array.count);

        size_t esz = elem_fixed_size(et);
        if (esz && v->v.array.count && v->v.array.data) {
            size_t total = (size_t)v->v.array.count * esz;
            h = hash64_bytes(h, v->v.array.data, total);
        }
        break;
    }

    default:
        /* Unknown tag: the tag byte already folded above is all we have. */
        break;
    }

    return h;
}

uint64_t bcfg_entry_hash(const bcfg_entry *e)
{
    uint64_t h = FNV1A64_OFFSET;

    if (!e)
        return h;

    /* key length + key bytes, then the value's structural hash */
    h = hash64_u8(h, e->key_len);
    if (e->key_len && e->key)
        h = hash64_bytes(h, e->key, e->key_len);

    h = hash64_len(h, bcfg_value_hash(&e->value));
    return h;
}

uint64_t bcfg_section_hash(const bcfg_section *s)
{
    uint64_t h = FNV1A64_OFFSET;

    if (!s)
        return h;

    /* name length + name bytes, then each entry hash in order */
    h = hash64_u8(h, s->name_len);
    if (s->name_len && s->name)
        h = hash64_bytes(h, s->name, s->name_len);

    h = hash64_len(h, s->num_entries);

    if (s->entries) {
        for (uint32_t i = 0; i < s->num_entries; i++)
            h = hash64_len(h, bcfg_entry_hash(&s->entries[i]));
    }

    return h;
}
