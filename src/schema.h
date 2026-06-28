/*
 * schema.h -- declarative schema and validation
 *
 * A schema declares which sections are permitted and which keys must (or may)
 * appear, along with each key's expected value type.  Validation walks a
 * parsed document against the schema and records every violation -- missing
 * required key, type mismatch -- into a bcfg_diag, returning the count.
 */

#ifndef CONFIGBIN_SCHEMA_H
#define CONFIGBIN_SCHEMA_H

#include "configbin.h"
#include "diag.h"

#ifdef __cplusplus
extern "C" {
#endif

/* opaque schema handle */
typedef struct bcfg_schema bcfg_schema;

/* Create an empty schema, or NULL on allocation failure. */
bcfg_schema *bcfg_schema_new(void);

/*
 * Declare a key within a section, its expected BCFG_TYPE_* tag, and whether
 * it is required (non-zero) or optional (0).  Implicitly allows the section.
 * Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_schema_require(bcfg_schema *s, const char *section, const char *key,
                        uint8_t type, int required);

/*
 * Mark a section name as permitted without constraining its keys.
 * Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_schema_allow_section(bcfg_schema *s, const char *section);

/*
 * Validate doc against the schema.  Each violation is appended to `out` (when
 * non-NULL) with a descriptive message and the relevant BCFG_ERR_* code
 * (BCFG_ERR_NOTFOUND for a missing required key, BCFG_ERR_TYPE for a type
 * mismatch).  Returns the number of violations found (0 means valid).
 */
int bcfg_schema_validate(const bcfg_schema *s, const bcfg_doc *doc,
                         bcfg_diag *out);

/* Release the schema and all declarations. */
void bcfg_schema_free(bcfg_schema *s);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_SCHEMA_H */
