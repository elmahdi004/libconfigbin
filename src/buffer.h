/*
 * buffer.h -- growable little-endian byte buffer
 *
 * A bcfg_buf is an append-only byte sink used by the encoder and the dump
 * helpers.  Multi-byte integers are written little-endian to match the v1
 * wire format.  Capacity grows geometrically (doubling) as needed.  The put
 * helpers return BCFG_OK or BCFG_ERR_NOMEM.
 */

#ifndef CONFIGBIN_BUFFER_H
#define CONFIGBIN_BUFFER_H

#include "configbin.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* growable byte buffer; data/len/cap are public for direct reads */
typedef struct {
    uint8_t *data;   /* backing storage, or NULL when empty   */
    size_t   len;    /* bytes written so far                  */
    size_t   cap;    /* allocated capacity of data            */
} bcfg_buf;

/* Initialise an empty buffer (no allocation until the first write). */
void bcfg_buf_init(bcfg_buf *b);

/*
 * Ensure at least `extra` more bytes can be appended without reallocating.
 * Doubles capacity as required.  Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_buf_reserve(bcfg_buf *b, size_t extra);

/* Append a single byte. */
int bcfg_buf_put_u8(bcfg_buf *b, uint8_t v);

/* Append a 16-bit value, little-endian. */
int bcfg_buf_put_u16(bcfg_buf *b, uint16_t v);

/* Append a 32-bit value, little-endian. */
int bcfg_buf_put_u32(bcfg_buf *b, uint32_t v);

/* Append a 64-bit value, little-endian. */
int bcfg_buf_put_u64(bcfg_buf *b, uint64_t v);

/* Append n raw bytes from src. */
int bcfg_buf_put_bytes(bcfg_buf *b, const void *src, size_t n);

/* Append an unsigned LEB128 (varint) encoding of v. */
int bcfg_buf_put_varint(bcfg_buf *b, uint64_t v);

/* Append a C string's bytes, excluding the terminating NUL. */
int bcfg_buf_put_cstr(bcfg_buf *b, const char *s);

/* Discard contents but keep the allocated capacity. */
void bcfg_buf_reset(bcfg_buf *b);

/* Free the backing storage and return to the empty state. */
void bcfg_buf_free(bcfg_buf *b);

/*
 * Hand ownership of the backing storage to the caller.  *out_data receives
 * the (possibly NULL) pointer, *out_len its length; the buffer is reset to
 * empty without freeing.  The caller must free(*out_data).
 */
void bcfg_buf_steal(bcfg_buf *b, uint8_t **out_data, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_BUFFER_H */
