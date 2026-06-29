/*
 * canon_fuzzer.cc -- fuzz the canonicalizing merge
 *
 * The input is read as a framed stream; the first two frames provide the base
 * and patch documents.  This exercises the parser, the encoder (during
 * canonicalization), the re-parse of the canonical form, and the patch merge
 * together.
 */

#include <cstdint>
#include <cstddef>

extern "C" {
#include "../src/configbin.h"
#include "../src/frame.h"
#include "../src/canon.h"
#include "../src/buffer.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const uint8_t *p   = data;
    const uint8_t *end = data + size;

    bcfg_frame base, patch;
    if (bcfg_frame_read(&p, end, &base) != BCFG_OK)
        return 0;
    if (bcfg_frame_read(&p, end, &patch) != BCFG_OK)
        return 0;

    bcfg_buf out;
    bcfg_buf_init(&out);
    bcfg_canon_merge(base.doc_payload, base.doc_len,
                     patch.doc_payload, patch.doc_len, &out);
    bcfg_buf_free(&out);
    return 0;
}
