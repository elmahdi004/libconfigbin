/*
 * iter.h -- visitor / walk API
 *
 * bcfg_walk performs a depth-first traversal of a parsed document, invoking
 * the supplied callbacks for each section, each entry, and each element of
 * an array-typed entry.  Any callback may be NULL to skip that level.  A
 * callback returning non-zero stops the walk immediately and that value is
 * propagated back to the caller.
 */

#ifndef CONFIGBIN_ITER_H
#define CONFIGBIN_ITER_H

#include "configbin.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Set of optional callbacks.  Each receives the user context verbatim.
 *   on_section    -- once per section, before its entries
 *   on_entry      -- once per entry
 *   on_array_elem -- once per element of a BCFG_TYPE_ARRAY entry; elem points
 *                    at that element within the array payload, index is its
 *                    position (0-based)
 * Return non-zero from any callback to stop the walk early.
 */
typedef struct {
    int (*on_section)(const bcfg_section *sec, void *ctx);
    int (*on_entry)(const bcfg_section *sec, const bcfg_entry *ent, void *ctx);
    int (*on_array_elem)(const bcfg_entry *ent, uint16_t index,
                         const void *elem, void *ctx);
} bcfg_visitor;

/*
 * Walk the whole document.  Returns the first non-zero stop code returned by
 * a callback, or 0 if the traversal completed.
 */
int bcfg_walk(const bcfg_doc *doc, const bcfg_visitor *vis, void *ctx);

/* Total number of entries across every section. */
size_t bcfg_count_entries(const bcfg_doc *doc);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_ITER_H */
