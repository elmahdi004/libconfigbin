/*
 * frame.c -- framing layer: read/iterate framed configbin streams
 */

#include "frame.h"
#include <string.h>
#include <stdlib.h>

int bcfg_frame_read(const uint8_t **pp, const uint8_t *end,
                    bcfg_frame *out)
{
    const uint8_t *p = *pp;

    if ((size_t)(end - p) < FRAME_HDR_SIZE)
        return BCFG_ERR_TRUNCATED;

    /* sync word */
    uint16_t sync;
    memcpy(&sync, p, 2); p += 2;
    if (sync != FRAME_SYNC)
        return BCFG_ERR_INVALID;

    out->flags   = *p++;
    out->pad_len = *p++;

    memcpy(&out->doc_len, p, 4); p += 4;

    /* guard against obviously-bad doc_len before sizing check */
    if (out->doc_len > (uint32_t)(end - p))
        return BCFG_ERR_TRUNCATED;

    out->doc_payload = p;
    p += out->doc_len;

    /* skip padding */
    if ((size_t)(end - p) < out->pad_len)
        return BCFG_ERR_TRUNCATED;
    p += out->pad_len;

    *pp = p;
    return BCFG_OK;
}

int bcfg_stream_decode(const uint8_t *data, size_t len,
                       bcfg_frame_cb doc_cb, void *cb_ctx)
{
    const uint8_t *p   = data;
    const uint8_t *end = data + len;
    int frames = 0;

    while (p < end) {
        bcfg_frame frame;
        int rc = bcfg_frame_read(&p, end, &frame);
        if (rc != BCFG_OK)
            return rc;

        /* skip compressed/encrypted — not implemented */
        if (frame.flags & (FRAME_FLAG_COMPRESSED | FRAME_FLAG_ENCRYPTED))
            goto next;

        if (frame.doc_len > 0 && doc_cb) {
            bcfg_doc *doc = NULL;
            rc = bcfg_parse(frame.doc_payload, frame.doc_len, &doc);
            if (rc == BCFG_OK) {
                doc_cb(doc, cb_ctx);
                bcfg_free(doc);
            }
            /* silently skip malformed docs in stream context */
        }

next:
        frames++;
        if (frame.flags & FRAME_FLAG_LAST)
            break;
    }

    return frames;
}
