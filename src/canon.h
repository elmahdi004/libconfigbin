/*
 * canon.h -- canonicalizing merge and canonical encoding
 *
 * Canonicalization re-serializes a document while folding away sections that
 * carry no effective settings (no non-null entry).  bcfg_canon_encode exposes
 * just the encoding step; bcfg_canon_merge canonicalizes a base document (via a
 * serialize / re-parse round trip) and merges a patch document against it,
 * reporting how each patch section relates to the canonical form.
 *
 *   bcfg_buf out; bcfg_buf_init(&out);
 *   bcfg_canon_merge(base, blen, patch, plen, &out);
 *   bcfg_buf_free(&out);
 *
 * Output is a line per patch section: "name=patchname" when it overrides a
 * canonical section, or "+name" when it is new.  Both inputs are independent
 * documents in the v1 wire format; an input that does not parse contributes
 * nothing.
 */

#ifndef CONFIGBIN_CANON_H
#define CONFIGBIN_CANON_H

#include "configbin.h"
#include "buffer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Encode doc in canonical form, appending to out: the v1 wire format with
 * sections that hold no non-null entry omitted.  Returns BCFG_OK, BCFG_ERR_NOMEM
 * or BCFG_ERR_INVALID.
 */
int bcfg_canon_encode(const bcfg_doc *doc, bcfg_buf *out);

/*
 * Canonicalize base and merge patch into it, appending the report to out.
 * Returns BCFG_OK or a negative BCFG_ERR_* code.
 */
int bcfg_canon_merge(const uint8_t *base, size_t blen,
                     const uint8_t *patch, size_t plen,
                     bcfg_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_CANON_H */
