/*
 * merge.h -- deep clone and overlay merge of documents
 *
 * bcfg_doc_clone produces a fully independent deep copy of a document, down
 * to string, blob and array payloads.  bcfg_merge layers an overlay on top
 * of a base: every base section/entry is present, overlay entries override
 * matching ones by section name and key, and overlay-only sections/entries
 * are appended.  Identical values are short-circuited via bcfg_value_hash.
 * Both produce a freshly heap-allocated document freeable with bcfg_free.
 */

#ifndef CONFIGBIN_MERGE_H
#define CONFIGBIN_MERGE_H

#include "configbin.h"
#include "hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Deep-copy a document.  Array payloads are copied as (size_t)count*elem_size
 * bytes.  Returns a new document (free with bcfg_free) or NULL on allocation
 * failure or a NULL argument.
 */
bcfg_doc *bcfg_doc_clone(const bcfg_doc *doc);

/*
 * Merge overlay onto base into a new document at *out.  Sections and entries
 * are matched by name/key: overlay wins on conflict, base-only and
 * overlay-only items are both retained.  *out is a fresh document freeable
 * with bcfg_free.  Returns BCFG_OK, BCFG_ERR_NOMEM, or BCFG_ERR_INVALID.
 */
int bcfg_merge(const bcfg_doc *base, const bcfg_doc *overlay, bcfg_doc **out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_MERGE_H */
