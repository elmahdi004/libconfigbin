/*
 * czip.h -- lossless byte compression for the COMPRESSED frame path
 *
 * Two self-contained codecs used to shrink raw frame payloads:
 *
 *   RLE  -- a simple run-length scheme over arbitrary bytes.
 *   LZSS -- a small sliding-window coder: 12-bit back-offset + 4-bit length
 *           match tokens, flagged by a bit per token, against a window whose
 *           geometry is recorded in a 16-byte stream header.
 *
 * Each function appends its output to a bcfg_buf and returns BCFG_OK or a
 * negative BCFG_ERR_* code.  Decompressors are bounds-safe: malformed or
 * truncated input yields BCFG_ERR_TRUNCATED / BCFG_ERR_INVALID, never a read
 * or write past the buffers.
 */

#ifndef CONFIGBIN_CZIP_H
#define CONFIGBIN_CZIP_H

#include "configbin.h"
#include "buffer.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- RLE ------------------------------------------------------------------ */

/*
 * RLE-compress n bytes at src, appending the encoded stream to out.
 * Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_rle_compress(const uint8_t *src, size_t n, bcfg_buf *out);

/*
 * Decompress an RLE stream of n bytes at src, appending the original bytes to
 * out.  Returns BCFG_OK, BCFG_ERR_TRUNCATED, or BCFG_ERR_INVALID.
 */
int bcfg_rle_decompress(const uint8_t *src, size_t n, bcfg_buf *out);

/* --- LZSS ----------------------------------------------------------------- */

/*
 * LZSS-compress n bytes at src, appending a 16-byte header followed by the
 * token stream to out.  Matches use a 12-bit offset and 4-bit length.
 * Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_lzss_compress(const uint8_t *src, size_t n, bcfg_buf *out);

/*
 * Decompress an LZSS stream of n bytes at src (header + tokens), appending the
 * original bytes to out.  Every back-reference is validated against the bytes
 * already produced.  Returns BCFG_OK, BCFG_ERR_TRUNCATED, or BCFG_ERR_INVALID.
 */
int bcfg_lzss_decompress(const uint8_t *src, size_t n, bcfg_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_CZIP_H */
