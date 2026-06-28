/*
 * validate.c -- constraint validation independent of schema.c
 *
 * A rule set is a flat, growable list of per-(section,key) constraint specs.
 * Section names, keys and any enum strings are deep-copied on insertion so the
 * caller's storage need not outlive the rule set.  Checking walks every rule,
 * resolves the addressed value, and applies whichever constraints the rule's
 * flag word selects, emitting one diagnostic per violation.
 */

#include "validate.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char           *section;
    char           *key;
    bcfg_rule_spec  spec;       /* enum_vals re-pointed at owned copies */
    char          **enum_copy;  /* owned copies of the enum strings     */
} bcfg_rule;

struct bcfg_ruleset {
    bcfg_rule *rules;
    size_t     count;
    size_t     cap;
};

bcfg_ruleset *bcfg_rules_new(void)
{
    return (bcfg_ruleset *)calloc(1, sizeof(bcfg_ruleset));
}

static char *dup_str(const char *s)
{
    size_t n = strlen(s);
    char  *c = (char *)malloc(n + 1);
    if (c) memcpy(c, s, n + 1);
    return c;
}

static void free_rule(bcfg_rule *r)
{
    free(r->section);
    free(r->key);
    if (r->enum_copy) {
        for (size_t i = 0; i < r->spec.n_enum_vals; i++)
            free(r->enum_copy[i]);
        free(r->enum_copy);
    }
}

int bcfg_rules_add(bcfg_ruleset *rs, const char *section, const char *key,
                   const bcfg_rule_spec *spec)
{
    if (!rs || !section || !key || !spec)
        return BCFG_ERR_INVALID;

    if (rs->count == rs->cap) {
        size_t ncap = rs->cap ? rs->cap * 2 : 8;
        bcfg_rule *nr = (bcfg_rule *)realloc(rs->rules, ncap * sizeof(*nr));
        if (!nr) return BCFG_ERR_NOMEM;
        rs->rules = nr;
        rs->cap   = ncap;
    }

    bcfg_rule *r = &rs->rules[rs->count];
    memset(r, 0, sizeof(*r));
    r->spec = *spec;

    r->section = dup_str(section);
    r->key     = dup_str(key);
    if (!r->section || !r->key) { free_rule(r); return BCFG_ERR_NOMEM; }

    if ((spec->flags & BCFG_RULE_ENUM) && spec->n_enum_vals) {
        r->enum_copy = (char **)calloc(spec->n_enum_vals, sizeof(char *));
        if (!r->enum_copy) { free_rule(r); return BCFG_ERR_NOMEM; }
        for (size_t i = 0; i < spec->n_enum_vals; i++) {
            r->enum_copy[i] = dup_str(spec->enum_vals[i]);
            if (!r->enum_copy[i]) { free_rule(r); return BCFG_ERR_NOMEM; }
        }
        r->spec.enum_vals = (const char **)r->enum_copy;
    } else {
        r->spec.flags      &= ~(uint32_t)BCFG_RULE_ENUM;
        r->spec.enum_vals   = NULL;
        r->spec.n_enum_vals = 0;
    }

    rs->count++;
    return BCFG_OK;
}

/* pull an integer out of any integer-like value; 0 on success */
static int as_int(const bcfg_value *v, int64_t *out)
{
    switch (v->type) {
    case BCFG_TYPE_UINT32:    *out = (int64_t)v->v.u32; return 0;
    case BCFG_TYPE_INT64:     *out = v->v.i64;          return 0;
    case BCFG_TYPE_TIMESTAMP: *out = v->v.ts;           return 0;
    case BCFG_TYPE_BOOL:      *out = v->v.boolean;      return 0;
    default:                  return -1;
    }
}

static int check_one(const bcfg_rule *r, const bcfg_entry *e, bcfg_diag *diag)
{
    const bcfg_value *v = &e->value;
    int violations = 0;
    uint32_t f = r->spec.flags;

    if ((f & BCFG_RULE_TYPE) && v->type != r->spec.want_type) {
        violations++;
        if (diag)
            bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_TYPE, 0,
                          "%s.%s: expected type %s, got %s",
                          r->section, r->key,
                          bcfg_type_name(r->spec.want_type),
                          bcfg_type_name(v->type));
    }

    if (f & (BCFG_RULE_INT_MIN | BCFG_RULE_INT_MAX)) {
        int64_t iv;
        if (as_int(v, &iv) == 0) {
            if ((f & BCFG_RULE_INT_MIN) && iv < r->spec.imin) {
                violations++;
                if (diag)
                    bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_RANGE, 0,
                                  "%s.%s: %lld below minimum %lld",
                                  r->section, r->key,
                                  (long long)iv, (long long)r->spec.imin);
            }
            if ((f & BCFG_RULE_INT_MAX) && iv > r->spec.imax) {
                violations++;
                if (diag)
                    bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_RANGE, 0,
                                  "%s.%s: %lld above maximum %lld",
                                  r->section, r->key,
                                  (long long)iv, (long long)r->spec.imax);
            }
        }
    }

    if ((f & BCFG_RULE_STR_MAXLEN) && v->type == BCFG_TYPE_STRING) {
        if (v->v.str.len > r->spec.str_maxlen) {
            violations++;
            if (diag)
                bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_RANGE, 0,
                              "%s.%s: string length %u exceeds %zu",
                              r->section, r->key,
                              (unsigned)v->v.str.len, r->spec.str_maxlen);
        }
    }

    if ((f & BCFG_RULE_ENUM) && v->type == BCFG_TYPE_STRING) {
        int ok = 0;
        for (size_t i = 0; i < r->spec.n_enum_vals && !ok; i++) {
            const char *cand = r->spec.enum_vals[i];
            if (strlen(cand) == v->v.str.len &&
                memcmp(cand, v->v.str.data, v->v.str.len) == 0)
                ok = 1;
        }
        if (!ok) {
            violations++;
            if (diag)
                bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_INVALID, 0,
                              "%s.%s: value not in allowed enum set",
                              r->section, r->key);
        }
    }

    if (f & (BCFG_RULE_ARR_MIN | BCFG_RULE_ARR_MAX)) {
        if (v->type == BCFG_TYPE_ARRAY) {
            uint16_t c = v->v.array.count;
            if ((f & BCFG_RULE_ARR_MIN) && c < r->spec.arr_min) {
                violations++;
                if (diag)
                    bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_RANGE, 0,
                                  "%s.%s: array count %u below minimum %u",
                                  r->section, r->key,
                                  (unsigned)c, (unsigned)r->spec.arr_min);
            }
            if ((f & BCFG_RULE_ARR_MAX) && c > r->spec.arr_max) {
                violations++;
                if (diag)
                    bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_RANGE, 0,
                                  "%s.%s: array count %u above maximum %u",
                                  r->section, r->key,
                                  (unsigned)c, (unsigned)r->spec.arr_max);
            }
        }
    }

    return violations;
}

int bcfg_rules_check(const bcfg_ruleset *rs, const bcfg_doc *doc,
                     bcfg_diag *diag)
{
    int violations = 0;

    if (!rs || !doc) return BCFG_ERR_INVALID;

    for (size_t i = 0; i < rs->count; i++) {
        const bcfg_rule *r = &rs->rules[i];
        bcfg_section *sec = bcfg_get_section(doc, r->section);
        bcfg_entry   *e   = sec ? bcfg_get_entry(sec, r->key) : NULL;

        if (!e) {
            if (r->spec.flags & BCFG_RULE_REQUIRED) {
                violations++;
                if (diag)
                    bcfg_diag_add(diag, BCFG_SEV_ERROR, BCFG_ERR_NOTFOUND, 0,
                                  "%s.%s: required key missing",
                                  r->section, r->key);
            }
            continue;
        }
        violations += check_one(r, e, diag);
    }
    return violations;
}

void bcfg_rules_free(bcfg_ruleset *rs)
{
    if (!rs) return;
    for (size_t i = 0; i < rs->count; i++)
        free_rule(&rs->rules[i]);
    free(rs->rules);
    free(rs);
}
