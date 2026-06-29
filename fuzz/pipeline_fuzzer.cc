/*
 * pipeline_fuzzer.cc -- fuzz the end-to-end text transform
 *
 * Feeds the input as ".bct" text through bcfg_pipeline_run, exercising the text
 * frontend, the reference linker, the canonical encoder, the binary parser and
 * the flattener together.
 */

#include <cstdint>
#include <cstddef>

extern "C" {
#include "../src/configbin.h"
#include "../src/pipeline.h"
#include "../src/buffer.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bcfg_buf out;
    bcfg_buf_init(&out);
    bcfg_pipeline_run(reinterpret_cast<const char *>(data), size, &out);
    bcfg_buf_free(&out);
    return 0;
}
