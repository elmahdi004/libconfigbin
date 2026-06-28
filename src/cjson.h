/*
 * cjson.h -- JSON bridge to/from bcfg_doc
 *
 * A deterministic, subset JSON binding onto the binary document model.
 * bcfg_json_parse() builds a heap doc (free with bcfg_free); bcfg_json_write()
 * emits the inverse and round-trips the supported types.
 *
 * Mapping (subset, deterministic):
 *   top-level JSON object -> the document; each member whose value is an
 *     OBJECT becomes a section.
 *   within a section object, each member is an entry; value mapping:
 *     integer in u32 range -> UINT32 ; other integers -> INT64 ;
 *     real -> FLOAT64 ; string -> STRING ; true/false -> BOOL ; null -> NULL ;
 *     array of numbers (homogeneous) -> ARRAY (uint32/int64/float64 by content).
 *   Nested objects deeper than section level, or arrays of non-numbers, are an
 *   error (recorded in diag).
 */

#ifndef CONFIGBIN_CJSON_H
#define CONFIGBIN_CJSON_H

#include "configbin.h"
#include "buffer.h"
#include "diag.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse len bytes of JSON into a new document per the mapping above.
 * On success *out is heap-allocated (free with bcfg_free) and BCFG_OK is
 * returned.  On failure a negative BCFG_ERR_* code is returned and, if diag
 * is non-NULL, diagnostics are appended describing the offending construct.
 */
int bcfg_json_parse(const char *json, size_t len, bcfg_doc **out,
                    bcfg_diag *diag);

/*
 * Serialise doc as JSON, appending it to out.  When pretty is non-zero the
 * output is indented with newlines; otherwise it is compact.
 * Returns BCFG_OK or a negative BCFG_ERR_* code (e.g. BCFG_ERR_NOMEM).
 */
int bcfg_json_write(const bcfg_doc *doc, bcfg_buf *out, int pretty);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_CJSON_H */
