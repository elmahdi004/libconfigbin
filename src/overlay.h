/*
 * overlay.h -- streaming overlay accumulator
 *
 * Applies a sequence of configuration documents ("overlay frames") onto a
 * running view.  As each document is applied, the names of its populated
 * sections are recorded so the merged set of section names can be rendered
 * once the stream is complete.  Section names are kept in a shared name pool
 * so repeated overlays do not each pay for their own copies.
 *
 * Typical lifecycle:
 *   bcfg_overlay *ov = bcfg_overlay_new();
 *   for each document d in the stream:
 *       bcfg_overlay_apply(ov, d.bytes, d.len);
 *   bcfg_overlay_render(ov, &out);   // the merged section listing
 *   bcfg_overlay_free(ov);
 */

#ifndef CONFIGBIN_OVERLAY_H
#define CONFIGBIN_OVERLAY_H

#include "configbin.h"
#include "buffer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bcfg_overlay bcfg_overlay;

/* Create an empty accumulator, or NULL on allocation failure. */
bcfg_overlay *bcfg_overlay_new(void);

/*
 * Parse one document from (doc, len) and fold its populated sections into the
 * accumulator.  Sections with no entries are ignored.  The parsed document is
 * released before returning; only the recorded section names persist.
 * Returns BCFG_OK, or a negative BCFG_ERR_* code (a document that does not
 * parse is skipped and reported as BCFG_OK so streaming continues).
 */
int bcfg_overlay_apply(bcfg_overlay *ov, const uint8_t *doc, size_t len);

/*
 * Render the accumulated section names, one per line, appending to out.
 * Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_overlay_render(bcfg_overlay *ov, bcfg_buf *out);

/* Number of section names accumulated so far. */
size_t bcfg_overlay_count(const bcfg_overlay *ov);

/* Release the accumulator and its name pool. */
void bcfg_overlay_free(bcfg_overlay *ov);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_OVERLAY_H */
