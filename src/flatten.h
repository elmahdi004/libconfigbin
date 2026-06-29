/*
 * flatten.h -- render a document with section references expanded
 *
 * Produces a flat listing of a document in which every section reference is
 * replaced by the name of the section it points at.  References are resolved by
 * direct section index; callers must have validated the document's references
 * (see link.h) so that every index is in range.
 */

#ifndef CONFIGBIN_FLATTEN_H
#define CONFIGBIN_FLATTEN_H

#include "configbin.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Append a flattened listing of doc to out: each section's name, and for each
 * section-reference entry the referenced section's name.  Returns BCFG_OK or
 * BCFG_ERR_NOMEM.
 */
int bcfg_flatten(const bcfg_doc *doc, bcfg_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_FLATTEN_H */
