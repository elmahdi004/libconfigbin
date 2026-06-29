/*
 * link.h -- section-reference validation pass
 *
 * Walks a document and brings every section reference (BCFG_TYPE_SECREF) into
 * range for that document: a reference whose target index is past the section
 * count is clamped to a safe in-range value.  After this pass every SECREF in
 * the document points at a section that exists, so consumers may resolve them
 * by direct index without re-checking.
 */

#ifndef CONFIGBIN_LINK_H
#define CONFIGBIN_LINK_H

#include "configbin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validate and in-range-clamp every section reference in doc.
 * Returns the number of references that had to be clamped (0 if all were
 * already valid), or a negative BCFG_ERR_* code on bad arguments.
 */
int bcfg_link_validate(bcfg_doc *doc);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_LINK_H */
