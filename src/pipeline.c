/*
 * pipeline.c -- end-to-end text -> canonical -> flattened transform
 *
 * Stages:
 *   1. parse the ".bct" text into a document;
 *   2. validate the document's section references (link pass), bringing any
 *      out-of-range reference back in range;
 *   3. canonicalize: re-encode without empty sections and re-parse;
 *   4. flatten the canonical document, expanding section references.
 *
 * After step 2 every reference is in range, so the flattener in step 4 resolves
 * them by direct index.
 */

#include "pipeline.h"
#include "ctext.h"
#include "link.h"
#include "canon.h"
#include "flatten.h"
#include <stdlib.h>

int bcfg_pipeline_run(const char *text, size_t len, bcfg_buf *out)
{
    if (!out)
        return BCFG_ERR_INVALID;

    /* 1. text -> document */
    bcfg_doc *doc = NULL;
    if (bcfg_ctext_parse(text, len, &doc, NULL) != BCFG_OK || !doc)
        return BCFG_OK;   /* not valid text: nothing to do */

    /* 2. bring every section reference in range for this document */
    bcfg_link_validate(doc);

    /* 3. canonicalize: drop empty sections, then re-parse the result */
    bcfg_doc *canon = NULL;
    bcfg_buf enc;
    bcfg_buf_init(&enc);
    if (bcfg_canon_encode(doc, &enc) == BCFG_OK)
        (void)bcfg_parse(enc.data, enc.len, &canon);
    bcfg_buf_free(&enc);

    /* 4. flatten the canonical document, expanding references */
    if (canon)
        bcfg_flatten(canon, out);

    bcfg_free(doc);
    bcfg_free(canon);
    return BCFG_OK;
}
