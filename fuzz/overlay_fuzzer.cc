/*
 * overlay_fuzzer.cc -- fuzz the streaming overlay accumulator
 *
 * Treats the input as a framed stream (frame.h): each frame's document payload
 * is applied to a bcfg_overlay, and once a final frame is seen the accumulated
 * section listing is rendered.  This exercises the frame layer, the document
 * parser, and the overlay's name pooling / rendering together.
 */

#include <cstdint>
#include <cstddef>

extern "C" {
#include "../src/configbin.h"
#include "../src/frame.h"
#include "../src/overlay.h"
#include "../src/buffer.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bcfg_overlay *ov = bcfg_overlay_new();
    if (!ov)
        return 0;

    const uint8_t *p   = data;
    const uint8_t *end = data + size;
    int last = 0;

    while (p < end) {
        bcfg_frame fr;
        if (bcfg_frame_read(&p, end, &fr) != BCFG_OK)
            break;

        if (!(fr.flags & (FRAME_FLAG_COMPRESSED | FRAME_FLAG_ENCRYPTED)) &&
            fr.doc_len > 0)
            bcfg_overlay_apply(ov, fr.doc_payload, fr.doc_len);

        if (fr.flags & FRAME_FLAG_LAST) {
            last = 1;
            break;
        }
    }

    if (last) {
        bcfg_buf out;
        bcfg_buf_init(&out);
        bcfg_overlay_render(ov, &out);
        bcfg_buf_free(&out);
    }

    bcfg_overlay_free(ov);
    return 0;
}
