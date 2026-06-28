/*
 * validate.h -- constraint validation independent of schema.h
 *
 * Build a rule set of per-(section,key) constraints, then check a parsed
 * document against it.  Each rule may assert any subset of: the key is
 * required, the value has an expected type, an integer falls within
 * [min,max], a string is at most a maximum length, a string is drawn from an
 * allowed enum set, or an array's element count is within [min,max].  Checking
 * appends one diagnostic per violation and returns the violation count.
 */

#ifndef CONFIGBIN_VALIDATE_H
#define CONFIGBIN_VALIDATE_H

#include "configbin.h"
#include "diag.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* which constraints a rule actually asserts (OR together) */
#define BCFG_RULE_REQUIRED    0x01  /* the key must be present              */
#define BCFG_RULE_TYPE        0x02  /* value type must equal want_type      */
#define BCFG_RULE_INT_MIN     0x04  /* integer value >= imin                */
#define BCFG_RULE_INT_MAX     0x08  /* integer value <= imax                */
#define BCFG_RULE_STR_MAXLEN  0x10  /* string length <= str_maxlen          */
#define BCFG_RULE_ENUM        0x20  /* string is one of enum_vals           */
#define BCFG_RULE_ARR_MIN     0x40  /* array count >= arr_min               */
#define BCFG_RULE_ARR_MAX     0x80  /* array count <= arr_max               */

typedef struct bcfg_ruleset bcfg_ruleset;

/*
 * Description of the constraints for one (section, key) pair.  The `flags`
 * field selects which of the remaining fields are active; unselected fields
 * are ignored.  enum_vals is an array of n_enum_vals NUL-terminated strings;
 * the strings are copied when added.
 */
typedef struct {
    uint32_t      flags;        /* OR of BCFG_RULE_* selecting active fields */
    uint8_t       want_type;    /* BCFG_TYPE_* for BCFG_RULE_TYPE            */
    int64_t       imin;         /* BCFG_RULE_INT_MIN bound                   */
    int64_t       imax;         /* BCFG_RULE_INT_MAX bound                   */
    size_t        str_maxlen;   /* BCFG_RULE_STR_MAXLEN bound                */
    const char  **enum_vals;    /* BCFG_RULE_ENUM allowed strings           */
    size_t        n_enum_vals;  /* count of enum_vals                        */
    uint16_t      arr_min;      /* BCFG_RULE_ARR_MIN bound                   */
    uint16_t      arr_max;      /* BCFG_RULE_ARR_MAX bound                   */
} bcfg_rule_spec;

/* Allocate an empty rule set, or NULL on allocation failure. */
bcfg_ruleset *bcfg_rules_new(void);

/*
 * Add a rule for section/key described by spec.  Section, key, and any enum
 * strings are copied into the rule set.  Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_rules_add(bcfg_ruleset *rs, const char *section, const char *key,
                   const bcfg_rule_spec *spec);

/*
 * Check doc against every rule in rs.  For each violation a diagnostic is
 * appended to diag (when non-NULL).  Returns the number of violations found
 * (0 means fully valid), or a negative BCFG_ERR_* code on internal error.
 */
int bcfg_rules_check(const bcfg_ruleset *rs, const bcfg_doc *doc,
                     bcfg_diag *diag);

/* Release a rule set and all copied strings (NULL is a no-op). */
void bcfg_rules_free(bcfg_ruleset *rs);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_VALIDATE_H */
