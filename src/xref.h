/*
 * xref.h -- two-document merge with cross-reference expansion
 *
 * Merges a base document and a patch document into a single view and expands
 * section cross-references (BCFG_TYPE_SECREF) inline, so that a section which
 * points at another section is rendered with the referenced section's contents
 * folded in.  Patch sections override base sections of the same name; sections
 * present only in the patch are appended.
 *
 *   bcfg_buf out; bcfg_buf_init(&out);
 *   bcfg_xref_merge(base, blen, patch, plen, &out);   // out holds the listing
 *   bcfg_buf_free(&out);
 *
 * Both inputs are independent documents in the v1 wire format.  Cross-reference
 * cycles are bounded by an internal depth limit.
 */

#ifndef CONFIGBIN_XREF_H
#define CONFIGBIN_XREF_H

#include "configbin.h"
#include "buffer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Merge two documents and render the cross-reference-expanded section listing
 * into out.  Returns BCFG_OK on success (including when an input does not parse,
 * in which case it contributes nothing), or BCFG_ERR_NOMEM on allocation
 * failure.
 */
int bcfg_xref_merge(const uint8_t *base, size_t blen,
                    const uint8_t *patch, size_t plen,
                    bcfg_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_XREF_H */
