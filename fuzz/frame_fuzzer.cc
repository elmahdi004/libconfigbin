/*
 * frame_fuzzer.cc -- fuzz bcfg_stream_decode() (framing + doc parsing)
 *
 * This entry point exercises the frame-layer parser and the doc parser
 * together, covering different code paths than config_fuzzer.
 */

#include <cstdint>
#include <cstddef>

extern "C" {
#include "../src/configbin.h"
#include "../src/frame.h"
}

static int noop_cb(bcfg_doc *doc, void *ctx)
{
    (void)ctx;
    if (!doc) return 0;

    /* Walk the doc so we exercise value accessors under the frame path too */
    for (uint16_t si = 0; si < doc->num_sections; si++) {
        bcfg_section *sec = &doc->sections[si];
        for (uint32_t ei = 0; ei < sec->num_entries; ei++) {
            bcfg_entry *ent = &sec->entries[ei];
            if (ent->value.type == BCFG_TYPE_ARRAY) {
                volatile uint8_t *p =
                    reinterpret_cast<volatile uint8_t *>(
                        ent->value.v.array.data);
                if (p && ent->value.v.array.count > 0)
                    (void)*p;
            }
        }
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bcfg_stream_decode(data, size, noop_cb, nullptr);
    return 0;
}
