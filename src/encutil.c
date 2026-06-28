/*
 * encutil.c -- byte-encoding helpers
 *
 * Implements base64 (RFC 4648, '+' '/' '='), base16/hex, unsigned LEB128
 * varints, and zigzag-mapped signed varints.  Decoders reject malformed
 * input; fixed-buffer writers never overrun the caller's region.  All
 * multi-byte work is byte-at-a-time, so the code is endianness-neutral.
 */

#include "encutil.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* base64 tables                                                        */
/* ------------------------------------------------------------------ */

static const char b64_enc_tbl[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

/*
 * Decode lookup: maps an ASCII byte to its 0-63 value, or one of the
 * sentinels below.  Built lazily on first use to avoid a 256-entry
 * literal initialiser.
 */
#define B64_INVALID 0xFF
#define B64_PAD     0xFE

static uint8_t b64_dec_tbl[256];
static int     b64_dec_ready = 0;

static void b64_dec_init(void)
{
    int i;
    for (i = 0; i < 256; i++)
        b64_dec_tbl[i] = B64_INVALID;
    for (i = 0; i < 64; i++)
        b64_dec_tbl[(unsigned char)b64_enc_tbl[i]] = (uint8_t)i;
    b64_dec_tbl[(unsigned char)'='] = B64_PAD;
    b64_dec_ready = 1;
}

/* ------------------------------------------------------------------ */
/* base64 sizing                                                        */
/* ------------------------------------------------------------------ */

size_t bcfg_base64_enc_len(size_t n)
{
    /* groups of 3 input bytes -> 4 output chars, rounded up */
    return ((n + 2) / 3) * 4;
}

size_t bcfg_base64_dec_len(size_t n)
{
    /* groups of 4 input chars -> at most 3 output bytes */
    return (n / 4) * 3;
}

/* ------------------------------------------------------------------ */
/* base64 encode                                                        */
/* ------------------------------------------------------------------ */

int bcfg_base64_encode(const uint8_t *src, size_t n, bcfg_buf *out)
{
    size_t i;
    size_t produced = 0;

    if (out == NULL || (src == NULL && n != 0))
        return BCFG_ERR_INVALID;

    if (bcfg_buf_reserve(out, bcfg_base64_enc_len(n)) != BCFG_OK)
        return BCFG_ERR_NOMEM;

    for (i = 0; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)src[i] << 16) |
                     ((uint32_t)src[i + 1] << 8) |
                     ((uint32_t)src[i + 2]);
        if (bcfg_buf_put_u8(out, (uint8_t)b64_enc_tbl[(v >> 18) & 0x3F]) != BCFG_OK ||
            bcfg_buf_put_u8(out, (uint8_t)b64_enc_tbl[(v >> 12) & 0x3F]) != BCFG_OK ||
            bcfg_buf_put_u8(out, (uint8_t)b64_enc_tbl[(v >> 6) & 0x3F]) != BCFG_OK ||
            bcfg_buf_put_u8(out, (uint8_t)b64_enc_tbl[v & 0x3F]) != BCFG_OK)
            return BCFG_ERR_NOMEM;
        produced += 4;
    }

    if (i < n) {
        size_t rem = n - i;              /* 1 or 2 */
        uint32_t v = (uint32_t)src[i] << 16;
        if (rem == 2)
            v |= (uint32_t)src[i + 1] << 8;

        if (bcfg_buf_put_u8(out, (uint8_t)b64_enc_tbl[(v >> 18) & 0x3F]) != BCFG_OK ||
            bcfg_buf_put_u8(out, (uint8_t)b64_enc_tbl[(v >> 12) & 0x3F]) != BCFG_OK)
            return BCFG_ERR_NOMEM;
        if (rem == 2) {
            if (bcfg_buf_put_u8(out, (uint8_t)b64_enc_tbl[(v >> 6) & 0x3F]) != BCFG_OK)
                return BCFG_ERR_NOMEM;
        } else {
            if (bcfg_buf_put_u8(out, (uint8_t)'=') != BCFG_OK)
                return BCFG_ERR_NOMEM;
        }
        if (bcfg_buf_put_u8(out, (uint8_t)'=') != BCFG_OK)
            return BCFG_ERR_NOMEM;
        produced += 4;
    }

    return (int)produced;
}

/* ------------------------------------------------------------------ */
/* base64 decode                                                        */
/* ------------------------------------------------------------------ */

int bcfg_base64_decode(const char *src, size_t n, bcfg_buf *out)
{
    size_t i;
    size_t produced = 0;

    if (out == NULL || (src == NULL && n != 0))
        return BCFG_ERR_INVALID;

    if (!b64_dec_ready)
        b64_dec_init();

    /* base64 text length must be a multiple of four */
    if (n % 4 != 0)
        return BCFG_ERR_INVALID;

    if (bcfg_buf_reserve(out, bcfg_base64_dec_len(n)) != BCFG_OK)
        return BCFG_ERR_NOMEM;

    for (i = 0; i < n; i += 4) {
        uint8_t c0 = b64_dec_tbl[(unsigned char)src[i]];
        uint8_t c1 = b64_dec_tbl[(unsigned char)src[i + 1]];
        uint8_t c2 = b64_dec_tbl[(unsigned char)src[i + 2]];
        uint8_t c3 = b64_dec_tbl[(unsigned char)src[i + 3]];
        int pad = 0;

        /* first two characters must always be data */
        if (c0 == B64_INVALID || c0 == B64_PAD ||
            c1 == B64_INVALID || c1 == B64_PAD)
            return BCFG_ERR_INVALID;

        if (c3 == B64_PAD) {
            /* padding only permitted in the final group */
            if (i + 4 != n)
                return BCFG_ERR_INVALID;
            if (c2 == B64_PAD) {
                pad = 2;            /* "xx==" -> 1 output byte */
            } else if (c2 == B64_INVALID) {
                return BCFG_ERR_INVALID;
            } else {
                pad = 1;            /* "xxx=" -> 2 output bytes */
            }
        } else if (c2 == B64_PAD) {
            /* '=' may not appear in the third slot alone */
            return BCFG_ERR_INVALID;
        } else if (c2 == B64_INVALID || c3 == B64_INVALID) {
            return BCFG_ERR_INVALID;
        }

        {
            uint32_t v = (uint32_t)c0 << 18 | (uint32_t)c1 << 12;
            if (pad < 2)
                v |= (uint32_t)c2 << 6;
            if (pad < 1)
                v |= (uint32_t)c3;

            if (bcfg_buf_put_u8(out, (uint8_t)((v >> 16) & 0xFF)) != BCFG_OK)
                return BCFG_ERR_NOMEM;
            produced += 1;
            if (pad < 2) {
                if (bcfg_buf_put_u8(out, (uint8_t)((v >> 8) & 0xFF)) != BCFG_OK)
                    return BCFG_ERR_NOMEM;
                produced += 1;
            }
            if (pad < 1) {
                if (bcfg_buf_put_u8(out, (uint8_t)(v & 0xFF)) != BCFG_OK)
                    return BCFG_ERR_NOMEM;
                produced += 1;
            }
        }
    }

    return (int)produced;
}

/* ------------------------------------------------------------------ */
/* base16 / hex                                                         */
/* ------------------------------------------------------------------ */

static const char hex_digits[16] = "0123456789ABCDEF";

/* Decode one hex nibble, or -1 if not a hex digit. */
static int hex_nibble(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int bcfg_hex_encode(const uint8_t *src, size_t n, char *dst, size_t dstcap)
{
    size_t i;

    if (dst == NULL || (src == NULL && n != 0))
        return BCFG_ERR_INVALID;

    /* 2n output chars; guard against size_t overflow of 2n */
    if (n > (size_t)-1 / 2 || 2 * n > dstcap)
        return BCFG_ERR_RANGE;

    for (i = 0; i < n; i++) {
        dst[2 * i]     = hex_digits[(src[i] >> 4) & 0x0F];
        dst[2 * i + 1] = hex_digits[src[i] & 0x0F];
    }

    return (int)(2 * n);
}

int bcfg_hex_decode(const char *src, size_t n, uint8_t *dst, size_t dstcap)
{
    size_t i;
    size_t produced;

    if (dst == NULL || (src == NULL && n != 0))
        return BCFG_ERR_INVALID;

    if (n % 2 != 0)
        return BCFG_ERR_INVALID;

    produced = n / 2;
    if (produced > dstcap)
        return BCFG_ERR_RANGE;

    for (i = 0; i < produced; i++) {
        int hi = hex_nibble((unsigned char)src[2 * i]);
        int lo = hex_nibble((unsigned char)src[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return BCFG_ERR_INVALID;
        dst[i] = (uint8_t)((hi << 4) | lo);
    }

    return (int)produced;
}

/* ------------------------------------------------------------------ */
/* unsigned LEB128                                                      */
/* ------------------------------------------------------------------ */

int bcfg_uvarint_encode(uint64_t v, uint8_t *dst, size_t dstcap)
{
    size_t i = 0;

    if (dst == NULL)
        return BCFG_ERR_INVALID;

    do {
        uint8_t byte = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v != 0)
            byte |= 0x80;
        if (i >= dstcap)
            return BCFG_ERR_RANGE;
        dst[i++] = byte;
    } while (v != 0);

    return (int)i;
}

int bcfg_uvarint_decode(const uint8_t *src, size_t n, uint64_t *out)
{
    uint64_t result = 0;
    unsigned shift = 0;
    size_t i = 0;

    if (src == NULL || out == NULL)
        return BCFG_ERR_INVALID;

    for (;;) {
        uint8_t byte;
        if (i >= n)
            return BCFG_ERR_TRUNCATED;
        byte = src[i++];

        /*
         * The 10th continuation byte may carry only a single payload bit
         * (64 = 9*7 + 1); reject anything wider than 64 bits.
         */
        if (shift >= 64)
            return BCFG_ERR_RANGE;
        if (shift == 63 && (byte & 0x7E) != 0)
            return BCFG_ERR_RANGE;

        result |= (uint64_t)(byte & 0x7F) << shift;

        if ((byte & 0x80) == 0)
            break;
        shift += 7;
    }

    *out = result;
    return (int)i;
}

/* ------------------------------------------------------------------ */
/* zigzag signed LEB128                                                 */
/* ------------------------------------------------------------------ */

static uint64_t zigzag_encode(int64_t v)
{
    /* arithmetic shift of negatives is implementation-defined, so cast
       through unsigned to get the well-defined two's-complement result */
    return ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
}

static int64_t zigzag_decode(uint64_t v)
{
    return (int64_t)((v >> 1) ^ (~(v & 1) + 1));
}

int bcfg_svarint_encode(int64_t v, uint8_t *dst, size_t dstcap)
{
    return bcfg_uvarint_encode(zigzag_encode(v), dst, dstcap);
}

int bcfg_svarint_decode(const uint8_t *src, size_t n, int64_t *out)
{
    uint64_t u = 0;
    int rc;

    if (out == NULL)
        return BCFG_ERR_INVALID;

    rc = bcfg_uvarint_decode(src, n, &u);
    if (rc < 0)
        return rc;

    *out = zigzag_decode(u);
    return rc;
}
