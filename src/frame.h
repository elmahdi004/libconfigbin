/*
 * frame.h -- streaming frame layer wrapping configbin documents
 *
 * A framed stream consists of one or more frames concatenated.
 * Each frame:
 *   [2B sync = 0xBCBE][1B flags][1B pad_len][4B doc_len][doc_len bytes][pad_len bytes]
 *
 * Flags:
 *   0x01  COMPRESSED -- doc payload is zlib-deflate compressed (not implemented, reserved)
 *   0x02  ENCRYPTED  -- doc payload is encrypted (not implemented, reserved)
 *   0x04  LAST       -- this is the final frame in the stream
 *
 * pad_len bytes of arbitrary padding follow the doc payload (for alignment).
 * The frame total on-wire size: 8 + doc_len + pad_len bytes.
 */

#ifndef CONFIGBIN_FRAME_H
#define CONFIGBIN_FRAME_H

#include "configbin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_SYNC       0xBEBC   /* little-endian: byte[0]=0xBC, byte[1]=0xBE */
#define FRAME_HDR_SIZE   8

#define FRAME_FLAG_COMPRESSED 0x01
#define FRAME_FLAG_ENCRYPTED  0x02
#define FRAME_FLAG_LAST       0x04

typedef struct {
    uint8_t  flags;
    uint8_t  pad_len;
    uint32_t doc_len;
    const uint8_t *doc_payload;  /* points into the source buffer */
} bcfg_frame;

/*
 * Decode one frame from *pp.  On success *pp advances past the frame
 * (header + doc_len + pad_len) and *out_frame is populated with pointers
 * into the original buffer.
 *
 * Returns BCFG_OK, BCFG_ERR_TRUNCATED, or BCFG_ERR_INVALID.
 */
int bcfg_frame_read(const uint8_t **pp, const uint8_t *end,
                    bcfg_frame *out_frame);

/*
 * Iterate over all frames in [data, data+len), calling doc_cb for each
 * decoded bcfg_doc.  Stops at the first frame with FRAME_FLAG_LAST or
 * after exhausting the buffer.
 *
 * cb_ctx is passed verbatim to doc_cb.
 * Returns the number of frames decoded, or a negative error code.
 */
typedef int (*bcfg_frame_cb)(bcfg_doc *doc, void *ctx);

int bcfg_stream_decode(const uint8_t *data, size_t len,
                       bcfg_frame_cb doc_cb, void *cb_ctx);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_FRAME_H */
