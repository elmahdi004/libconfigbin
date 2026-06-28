/*
 * expr.h -- query engine over a parsed document
 *
 * A richer addressing scheme than path.h.  A query selects zero or more
 * (section, entry, array-index) matches:
 *
 *   "section.key"   exact section, exact key
 *   "*.key"         the key in every section that has it (wildcard section)
 *   "section.*"     every key in the section (wildcard key)
 *   "key[i]"        as above, with an array element selected; array_index is
 *                   set to i for the match (and to -1 when no index applies)
 *
 * The two wildcard forms and the "[i]" suffix may combine (e.g. "*.ports[0]").
 * Results are returned as a heap array the caller frees with bcfg_expr_free().
 * All bcfg_section/bcfg_entry pointers borrow from the doc and stay valid for
 * the doc's lifetime.
 */

#ifndef CONFIGBIN_EXPR_H
#define CONFIGBIN_EXPR_H

#include "configbin.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* one query result: borrowed section/entry plus an optional array index */
typedef struct {
    const bcfg_section *section;     /* matched section (borrowed)          */
    const bcfg_entry   *entry;       /* matched entry (borrowed)            */
    int                 array_index; /* selected array index, or -1 if none */
} bcfg_match;

/*
 * Evaluate query against doc.  On success *out points to a heap array of
 * *n_out matches (possibly zero) and BCFG_OK is returned; free *out with
 * bcfg_expr_free().  Returns a negative BCFG_ERR_* code on a malformed query
 * (BCFG_ERR_INVALID) or allocation failure (BCFG_ERR_NOMEM).
 */
int bcfg_expr_eval(const bcfg_doc *doc, const char *query,
                   bcfg_match **out, size_t *n_out);

/* Free a match array returned by bcfg_expr_eval (NULL is a no-op). */
void bcfg_expr_free(bcfg_match *matches);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_EXPR_H */
