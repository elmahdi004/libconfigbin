/*
 * buffer.c -- growable little-endian byte buffer
 *
 * Append-only byte sink used by the encoder and dump helpers.  Multi-byte
 * integers are stored little-endian to match the v1 wire format regardless of
 * host endianness.  Capacity grows geometrically (doubling) on demand.  The
 * put helpers return BCFG_OK on success or BCFG_ERR_NOMEM if (re)allocation
 * fails; the buffer is left unmodified on failure.
 */

#include "buffer.h"
#include <stdlib.h>
#include <string.h>

/* Smallest capacity we bother allocating on the first growth.  Small enough
   not to waste memory on tiny buffers, large enough to avoid a flurry of
   reallocs for typical headers. */
#define BCFG_BUF_MIN_CAP 16

void bcfg_buf_init(bcfg_buf *b)
{
    if (!b) return;
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

int bcfg_buf_reserve(bcfg_buf *b, size_t extra)
{
    if (!b) return BCFG_ERR_NOMEM;

    /* Nothing to do if the spare capacity already covers the request. */
    if (extra <= b->cap - b->len)
        return BCFG_OK;

    /* Guard against size_t overflow when computing the required total. */
    if (extra > (size_t)-1 - b->len)
        return BCFG_ERR_NOMEM;

    size_t need = b->len + extra;

    /* Double until we satisfy `need`, starting from a sane minimum. */
    size_t new_cap = b->cap ? b->cap : BCFG_BUF_MIN_CAP;
    while (new_cap < need) {
        /* Detect doubling overflow; fall back to the exact size needed. */
        if (new_cap > ((size_t)-1) / 2) {
            new_cap = need;
            break;
        }
        new_cap *= 2;
    }

    uint8_t *p = (uint8_t *)realloc(b->data, new_cap);
    if (!p)
        return BCFG_ERR_NOMEM;

    b->data = p;
    b->cap  = new_cap;
    return BCFG_OK;
}

int bcfg_buf_put_u8(bcfg_buf *b, uint8_t v)
{
    int rc = bcfg_buf_reserve(b, 1);
    if (rc != BCFG_OK) return rc;
    b->data[b->len++] = v;
    return BCFG_OK;
}

int bcfg_buf_put_u16(bcfg_buf *b, uint16_t v)
{
    int rc = bcfg_buf_reserve(b, 2);
    if (rc != BCFG_OK) return rc;
    b->data[b->len++] = (uint8_t)(v & 0xff);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xff);
    return BCFG_OK;
}

int bcfg_buf_put_u32(bcfg_buf *b, uint32_t v)
{
    int rc = bcfg_buf_reserve(b, 4);
    if (rc != BCFG_OK) return rc;
    for (int i = 0; i < 4; i++)
        b->data[b->len++] = (uint8_t)((v >> (8 * i)) & 0xff);
    return BCFG_OK;
}

int bcfg_buf_put_u64(bcfg_buf *b, uint64_t v)
{
    int rc = bcfg_buf_reserve(b, 8);
    if (rc != BCFG_OK) return rc;
    for (int i = 0; i < 8; i++)
        b->data[b->len++] = (uint8_t)((v >> (8 * i)) & 0xff);
    return BCFG_OK;
}

int bcfg_buf_put_bytes(bcfg_buf *b, const void *src, size_t n)
{
    if (!b) return BCFG_ERR_NOMEM;
    if (n == 0) return BCFG_OK;        /* nothing to copy; src may be NULL */
    if (!src) return BCFG_ERR_NOMEM;   /* n>0 with no source is a caller bug */

    int rc = bcfg_buf_reserve(b, n);
    if (rc != BCFG_OK) return rc;

    memcpy(b->data + b->len, src, n);
    b->len += n;
    return BCFG_OK;
}

int bcfg_buf_put_varint(bcfg_buf *b, uint64_t v)
{
    if (!b) return BCFG_ERR_NOMEM;

    /* Unsigned LEB128: 7 bits per byte, low groups first, high bit marks
       continuation.  A 64-bit value needs at most 10 bytes. */
    uint8_t tmp[10];
    size_t  n = 0;

    do {
        uint8_t byte = (uint8_t)(v & 0x7f);
        v >>= 7;
        if (v != 0)
            byte |= 0x80;   /* more bytes follow */
        tmp[n++] = byte;
    } while (v != 0);

    return bcfg_buf_put_bytes(b, tmp, n);
}

int bcfg_buf_put_cstr(bcfg_buf *b, const char *s)
{
    if (!b) return BCFG_ERR_NOMEM;
    if (!s) return BCFG_OK;            /* treat NULL as the empty string */
    return bcfg_buf_put_bytes(b, s, strlen(s));
}

void bcfg_buf_reset(bcfg_buf *b)
{
    if (!b) return;
    b->len = 0;   /* keep data/cap so the storage can be reused */
}

void bcfg_buf_free(bcfg_buf *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

void bcfg_buf_steal(bcfg_buf *b, uint8_t **out_data, size_t *out_len)
{
    if (!b) {
        if (out_data) *out_data = NULL;
        if (out_len)  *out_len  = 0;
        return;
    }

    if (out_data) *out_data = b->data;
    if (out_len)  *out_len  = b->len;

    /* If the caller does not take the pointer, we would otherwise leak it;
       free it so ownership transfer is well defined either way. */
    if (!out_data)
        free(b->data);

    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}
