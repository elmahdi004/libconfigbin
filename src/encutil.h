/*
 * encutil.h -- byte-encoding helpers
 *
 * Encoders/decoders for base64 (RFC 4648, '+' '/' '='), base16/hex
 * (uppercase on encode, either case on decode), unsigned LEB128 varints, and
 * zigzag-mapped signed varints.  Buffer-appending variants write into a
 * bcfg_buf; fixed-buffer variants write into a caller-supplied region and
 * never overrun it.  Return values are byte counts produced/consumed, or a
 * negative BCFG_ERR_* code on malformed input or insufficient space.
 */

#ifndef CONFIGBIN_ENCUTIL_H
#define CONFIGBIN_ENCUTIL_H

#include "configbin.h"
#include "buffer.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- base64 --------------------------------------------------------------- */

/* Bytes a base64 encoding of n input bytes occupies (no NUL). */
size_t bcfg_base64_enc_len(size_t n);

/* Worst-case bytes a base64 decode of n input chars can yield. */
size_t bcfg_base64_dec_len(size_t n);

/*
 * Base64-encode n bytes at src, appending the ASCII output to out.
 * Returns the number of characters appended, or a negative BCFG_ERR_* code.
 */
int bcfg_base64_encode(const uint8_t *src, size_t n, bcfg_buf *out);

/*
 * Base64-decode n characters at src, appending raw bytes to out.  Whitespace
 * is rejected; padding must be well-formed.  Returns the number of bytes
 * appended, or a negative BCFG_ERR_* code on malformed input.
 */
int bcfg_base64_decode(const char *src, size_t n, bcfg_buf *out);

/* --- base16 / hex --------------------------------------------------------- */

/*
 * Hex-encode n bytes at src as 2n uppercase chars into dst (capacity dstcap).
 * Returns the number of chars written, or BCFG_ERR_RANGE if they do not fit.
 */
int bcfg_hex_encode(const uint8_t *src, size_t n, char *dst, size_t dstcap);

/*
 * Hex-decode n chars at src (n must be even; A-F/a-f/0-9 only) into dst
 * (capacity dstcap).  Returns the number of bytes written, BCFG_ERR_INVALID
 * on a non-hex/odd-length input, or BCFG_ERR_RANGE if dst is too small.
 */
int bcfg_hex_decode(const char *src, size_t n, uint8_t *dst, size_t dstcap);

/* --- varints -------------------------------------------------------------- */

/*
 * Write v as unsigned LEB128 into dst (capacity dstcap, up to 10 bytes).
 * Returns the number of bytes written, or BCFG_ERR_RANGE if it does not fit.
 */
int bcfg_uvarint_encode(uint64_t v, uint8_t *dst, size_t dstcap);

/*
 * Read an unsigned LEB128 from up to n bytes at src into *out.
 * Returns the number of bytes consumed, BCFG_ERR_TRUNCATED if the value runs
 * past src+n, or BCFG_ERR_RANGE on overflow of 64 bits.
 */
int bcfg_uvarint_decode(const uint8_t *src, size_t n, uint64_t *out);

/*
 * Write v as a zigzag-mapped signed LEB128 into dst (capacity dstcap).
 * Returns the number of bytes written, or BCFG_ERR_RANGE if it does not fit.
 */
int bcfg_svarint_encode(int64_t v, uint8_t *dst, size_t dstcap);

/*
 * Read a zigzag-mapped signed LEB128 from up to n bytes at src into *out.
 * Returns the number of bytes consumed, or a negative BCFG_ERR_* code.
 */
int bcfg_svarint_decode(const uint8_t *src, size_t n, int64_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_ENCUTIL_H */
