/*
 * pipeline.h -- end-to-end text -> canonical -> flattened transform
 *
 * Drives a document through the full toolchain: parse the textual ".bct" form,
 * validate its section references, canonicalize it (re-serialize without empty
 * sections and re-parse), and emit the flattened listing with references
 * expanded.  This stitches together the text frontend, the reference linker,
 * the canonical encoder, the binary parser and the flattener.
 */

#ifndef CONFIGBIN_PIPELINE_H
#define CONFIGBIN_PIPELINE_H

#include "configbin.h"
#include "buffer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run len bytes of ".bct" text through the transform, appending the flattened
 * listing to out.  Input that does not parse contributes nothing.
 * Returns BCFG_OK or a negative BCFG_ERR_* code.
 */
int bcfg_pipeline_run(const char *text, size_t len, bcfg_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_PIPELINE_H */
