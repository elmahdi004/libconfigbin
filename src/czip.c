/*
 * czip.c -- lossless byte compression for the COMPRESSED frame path
 *
 * Two small, self-contained codecs:
 *
 *   RLE   a PackBits-style run/literal coder.  A control byte introduces
 *         either a run (high bit set: (cb & 0x7f) + 3 copies of one byte) or
 *         a literal span (high bit clear: (cb + 1) raw bytes).
 *
 *   LZSS  a 4 KiB sliding window coder.  Tokens are grouped in eights behind a
 *         flag byte whose bits select literal (1) or match (0).  A match is two
 *         bytes: a 12-bit back-offset and a 4-bit length (encoding 3..18).  The
 *         stream opens with a 16-byte header recording the magic, the original
 *         length and the window geometry so the decoder can size its checks.
 *
 * The decoders never trust their input: every length and back-reference is
 * validated against what has been produced so far, so malformed or truncated
 * streams return an error rather than reading or writing out of bounds.
 */

#include "czip.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* RLE                                                                  */
/* ------------------------------------------------------------------ */

#define RLE_MIN_RUN   3
#define RLE_MAX_RUN   130   /* (0x7f) + 3 */
#define RLE_MAX_LIT   128   /* (0x7f) + 1 */

/* length of the identical-byte run starting at src[i], capped at cap */
static size_t run_length(const uint8_t *src, size_t i, size_t n, size_t cap)
{
    size_t len = 1;
    while (i + len < n && src[i + len] == src[i] && len < cap)
        len++;
    return len;
}

int bcfg_rle_compress(const uint8_t *src, size_t n, bcfg_buf *out)
{
    size_t i = 0;

    if (!out) return BCFG_ERR_INVALID;
    if (n && !src) return BCFG_ERR_INVALID;

    while (i < n) {
        size_t run = run_length(src, i, n, RLE_MAX_RUN);

        if (run >= RLE_MIN_RUN) {
            if (bcfg_buf_put_u8(out, (uint8_t)(0x80 | (run - RLE_MIN_RUN))))
                return BCFG_ERR_NOMEM;
            if (bcfg_buf_put_u8(out, src[i]))
                return BCFG_ERR_NOMEM;
            i += run;
            continue;
        }

        /* gather a literal span up to the next worthwhile run */
        size_t start = i;
        size_t lit   = 0;
        while (i < n && lit < RLE_MAX_LIT) {
            if (run_length(src, i, n, RLE_MIN_RUN) >= RLE_MIN_RUN)
                break;
            i++;
            lit++;
        }
        if (bcfg_buf_put_u8(out, (uint8_t)(lit - 1)))
            return BCFG_ERR_NOMEM;
        if (bcfg_buf_put_bytes(out, src + start, lit))
            return BCFG_ERR_NOMEM;
    }
    return BCFG_OK;
}

int bcfg_rle_decompress(const uint8_t *src, size_t n, bcfg_buf *out)
{
    size_t i = 0;

    if (!out) return BCFG_ERR_INVALID;
    if (n && !src) return BCFG_ERR_INVALID;

    while (i < n) {
        uint8_t cb = src[i++];

        if (cb & 0x80) {
            size_t run = (size_t)(cb & 0x7f) + RLE_MIN_RUN;
            if (i >= n) return BCFG_ERR_TRUNCATED;
            uint8_t b = src[i++];
            if (bcfg_buf_reserve(out, run)) return BCFG_ERR_NOMEM;
            memset(out->data + out->len, b, run);
            out->len += run;
        } else {
            size_t lit = (size_t)cb + 1;
            if ((size_t)(n - i) < lit) return BCFG_ERR_TRUNCATED;
            if (bcfg_buf_put_bytes(out, src + i, lit)) return BCFG_ERR_NOMEM;
            i += lit;
        }
    }
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* LZSS                                                                 */
/* ------------------------------------------------------------------ */

#define LZSS_MAGIC      0x53535A4CU   /* 'LZSS' little-endian */
#define LZSS_WINDOW     4096
#define LZSS_MIN_MATCH  3
#define LZSS_MAX_MATCH  18            /* 15 + LZSS_MIN_MATCH */
#define LZSS_HEADER     16

static void put_header(bcfg_buf *out_hdr, uint8_t *hdr, uint64_t orig)
{
    (void)out_hdr;
    memset(hdr, 0, LZSS_HEADER);
    hdr[0] = (uint8_t)(LZSS_MAGIC      );
    hdr[1] = (uint8_t)(LZSS_MAGIC >> 8 );
    hdr[2] = (uint8_t)(LZSS_MAGIC >> 16);
    hdr[3] = (uint8_t)(LZSS_MAGIC >> 24);
    for (int k = 0; k < 8; k++)
        hdr[4 + k] = (uint8_t)(orig >> (8 * k));
    hdr[12] = (uint8_t)(LZSS_WINDOW      );
    hdr[13] = (uint8_t)(LZSS_WINDOW >> 8 );
    hdr[14] = LZSS_MIN_MATCH;
    hdr[15] = LZSS_MAX_MATCH;
}

/* greedy longest-match search in the window preceding pos */
static size_t find_match(const uint8_t *src, size_t pos, size_t n,
                         size_t *out_off)
{
    size_t best_len = 0, best_off = 0;
    size_t start = (pos > LZSS_WINDOW) ? pos - LZSS_WINDOW : 0;

    for (size_t j = start; j < pos; j++) {
        size_t len = 0;
        while (len < LZSS_MAX_MATCH &&
               pos + len < n &&
               src[j + len] == src[pos + len])
            len++;
        if (len > best_len) {
            best_len = len;
            best_off = pos - j;
            if (len == LZSS_MAX_MATCH)
                break;
        }
    }
    *out_off = best_off;
    return best_len;
}

int bcfg_lzss_compress(const uint8_t *src, size_t n, bcfg_buf *out)
{
    uint8_t  hdr[LZSS_HEADER];
    size_t   pos = 0;

    if (!out) return BCFG_ERR_INVALID;
    if (n && !src) return BCFG_ERR_INVALID;

    put_header(out, hdr, (uint64_t)n);
    if (bcfg_buf_put_bytes(out, hdr, LZSS_HEADER))
        return BCFG_ERR_NOMEM;

    while (pos < n) {
        /* reserve a slot for the flag byte and fill it as we go */
        size_t  flag_at = out->len;
        uint8_t flags   = 0;
        int     bit     = 0;

        if (bcfg_buf_put_u8(out, 0))
            return BCFG_ERR_NOMEM;

        for (bit = 0; bit < 8 && pos < n; bit++) {
            size_t off = 0;
            size_t len = find_match(src, pos, n, &off);

            if (len >= LZSS_MIN_MATCH) {
                uint16_t enc_off = (uint16_t)(off - 1);     /* 0..4095   */
                uint8_t  enc_len = (uint8_t)(len - LZSS_MIN_MATCH); /* 0..15 */
                if (bcfg_buf_put_u8(out, (uint8_t)(enc_off & 0xff)))
                    return BCFG_ERR_NOMEM;
                if (bcfg_buf_put_u8(out,
                        (uint8_t)(((enc_off >> 8) & 0x0f) << 4 | enc_len)))
                    return BCFG_ERR_NOMEM;
                pos += len;
            } else {
                flags |= (uint8_t)(1u << bit);   /* literal */
                if (bcfg_buf_put_u8(out, src[pos]))
                    return BCFG_ERR_NOMEM;
                pos++;
            }
        }
        out->data[flag_at] = flags;
    }
    return BCFG_OK;
}

int bcfg_lzss_decompress(const uint8_t *src, size_t n, bcfg_buf *out)
{
    size_t   i = 0;
    uint64_t orig = 0;
    uint32_t magic;

    if (!out) return BCFG_ERR_INVALID;
    if (n && !src) return BCFG_ERR_INVALID;
    if (n < LZSS_HEADER) return BCFG_ERR_TRUNCATED;

    magic = (uint32_t)src[0] | (uint32_t)src[1] << 8 |
            (uint32_t)src[2] << 16 | (uint32_t)src[3] << 24;
    if (magic != LZSS_MAGIC) return BCFG_ERR_INVALID;
    for (int k = 0; k < 8; k++)
        orig |= (uint64_t)src[4 + k] << (8 * k);
    i = LZSS_HEADER;

    while (out->len < orig) {
        if (i >= n) return BCFG_ERR_TRUNCATED;
        uint8_t flags = src[i++];

        for (int bit = 0; bit < 8 && out->len < orig; bit++) {
            if (flags & (1u << bit)) {
                if (i >= n) return BCFG_ERR_TRUNCATED;
                if (bcfg_buf_put_u8(out, src[i++])) return BCFG_ERR_NOMEM;
            } else {
                if ((size_t)(n - i) < 2) return BCFG_ERR_TRUNCATED;
                uint8_t b0 = src[i++];
                uint8_t b1 = src[i++];
                size_t  off = (size_t)(((b1 >> 4) & 0x0f) << 8 | b0) + 1;
                size_t  len = (size_t)(b1 & 0x0f) + LZSS_MIN_MATCH;

                if (off > out->len) return BCFG_ERR_INVALID;
                if (out->len + len < out->len) return BCFG_ERR_INVALID;
                if (bcfg_buf_reserve(out, len)) return BCFG_ERR_NOMEM;
                /* byte-by-byte to honour overlapping runs */
                for (size_t k = 0; k < len; k++) {
                    out->data[out->len] = out->data[out->len - off];
                    out->len++;
                }
            }
        }
    }
    return (out->len == orig) ? BCFG_OK : BCFG_ERR_INVALID;
}
