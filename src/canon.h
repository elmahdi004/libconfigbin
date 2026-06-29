/*
 * canon.h -- canonicalizing merge
 *
 * Produces a canonical form of a base document by round-tripping it through the
 * wire format -- sections that carry no effective settings (no non-null entry)
 * are folded away during canonicalization -- and then merges a patch document
 * against that canonical form, reporting how each patch section relates to it.
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
