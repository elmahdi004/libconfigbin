/*
 * path.h -- dotted-path query over a parsed document
 *
 * Paths address a value as "section.key", with an optional trailing "[index]"
 * (e.g. "data.ids[2]").  The query splits on '.', treating the first segment
 * as the section name and the remainder as the entry key.  A trailing "[n]"
 * is parsed and range-checked against an array value, but the resolved value
 * returned is always the entry's value itself.
 */

#ifndef CONFIGBIN_PATH_H
#define CONFIGBIN_PATH_H

#include "configbin.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve just the section named by `name`.  NULL if absent. */
const bcfg_section *bcfg_query_section(const bcfg_doc *doc, const char *name);

/*
 * Resolve "section.key" (with optional "[index]") to the entry's value.
 * Returns NULL if the section or key is missing, the path is malformed, or
 * a present [index] is out of range for the array.
 */
const bcfg_value *bcfg_query(const bcfg_doc *doc, const char *path);

/*
 * Resolve `path` and, if it is a UINT32 value, store it in *out.
 * Returns BCFG_OK, BCFG_ERR_NOTFOUND, or BCFG_ERR_TYPE.
 */
int bcfg_query_uint32(const bcfg_doc *doc, const char *path, uint32_t *out);

/*
 * Resolve `path` and, if it is a STRING value, store a borrowed pointer to
 * its bytes in *out and its length in *out_len.
 * Returns BCFG_OK, BCFG_ERR_NOTFOUND, or BCFG_ERR_TYPE.
 */
int bcfg_query_string(const bcfg_doc *doc, const char *path,
                      const char **out, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_PATH_H */
