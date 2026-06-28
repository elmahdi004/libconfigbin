/*
 * schema.c -- declarative schema and validation
 *
 * A bcfg_schema is a flat collection of declarations.  Two kinds exist:
 *   - section rules: a permitted section name (no key constraints)
 *   - key rules:     a (section, key, type, required) tuple
 *
 * A key rule implicitly permits its section, so an allow-list of legal
 * section names is the union of every section mentioned by either rule kind.
 *
 * Validation walks the schema's key rules against a parsed document and
 * records one diagnostic per violation -- a required key that is absent, or a
 * present key whose value type does not match what the schema declared.
 */

#include "schema.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* internal representation                                              */
/* ------------------------------------------------------------------ */

/* one declared key constraint */
typedef struct {
    char    *section;   /* owned, NUL-terminated */
    char    *key;       /* owned, NUL-terminated */
    uint8_t  type;      /* expected BCFG_TYPE_* tag */
    int      required;  /* non-zero if the key must be present */
} key_rule;

/* one permitted section name */
typedef struct {
    char *section;      /* owned, NUL-terminated */
} sec_rule;

struct bcfg_schema {
    key_rule *keys;
    size_t    nkeys;
    size_t    keys_cap;

    sec_rule *secs;
    size_t    nsecs;
    size_t    secs_cap;
};

/* ------------------------------------------------------------------ */
/* small helpers                                                        */
/* ------------------------------------------------------------------ */

/* duplicate a NUL-terminated string; NULL on OOM (or for a NULL input) */
static char *dup_str(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* grow a dynamic array (of `elem` bytes each) to hold at least one more
   item.  Updates *cap and *base on success.  Returns BCFG_OK / NOMEM. */
static int ensure_cap(void **base, size_t *cap, size_t used, size_t elem)
{
    if (used < *cap)
        return BCFG_OK;
    size_t newcap = (*cap == 0) ? 4 : *cap * 2;
    /* guard against multiplicative overflow on absurd inputs */
    if (newcap < *cap || newcap > (size_t)-1 / elem)
        return BCFG_ERR_NOMEM;
    void *p = realloc(*base, newcap * elem);
    if (!p)
        return BCFG_ERR_NOMEM;
    *base = p;
    *cap  = newcap;
    return BCFG_OK;
}

/* is `name` already in the permitted-section list? */
static int has_section(const bcfg_schema *s, const char *name)
{
    for (size_t i = 0; i < s->nsecs; i++)
        if (strcmp(s->secs[i].section, name) == 0)
            return 1;
    return 0;
}

/* append `name` to the permitted-section list unless already present */
static int add_section(bcfg_schema *s, const char *name)
{
    if (has_section(s, name))
        return BCFG_OK;

    int rc = ensure_cap((void **)&s->secs, &s->secs_cap, s->nsecs,
                        sizeof(*s->secs));
    if (rc != BCFG_OK)
        return rc;

    char *dup = dup_str(name);
    if (!dup)
        return BCFG_ERR_NOMEM;

    s->secs[s->nsecs].section = dup;
    s->nsecs++;
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* public: construction                                                 */
/* ------------------------------------------------------------------ */

bcfg_schema *bcfg_schema_new(void)
{
    /* calloc zero-inits every field, which is the valid empty state */
    return (bcfg_schema *)calloc(1, sizeof(bcfg_schema));
}

int bcfg_schema_require(bcfg_schema *s, const char *section, const char *key,
                        uint8_t type, int required)
{
    if (!s || !section || !key)
        return BCFG_ERR_INVALID;

    /* a key rule implicitly permits its section */
    int rc = add_section(s, section);
    if (rc != BCFG_OK)
        return rc;

    rc = ensure_cap((void **)&s->keys, &s->keys_cap, s->nkeys,
                    sizeof(*s->keys));
    if (rc != BCFG_OK)
        return rc;

    char *sdup = dup_str(section);
    char *kdup = dup_str(key);
    if (!sdup || !kdup) {
        free(sdup);
        free(kdup);
        return BCFG_ERR_NOMEM;
    }

    key_rule *r = &s->keys[s->nkeys];
    r->section  = sdup;
    r->key      = kdup;
    r->type     = type;
    r->required = required ? 1 : 0;
    s->nkeys++;
    return BCFG_OK;
}

int bcfg_schema_allow_section(bcfg_schema *s, const char *section)
{
    if (!s || !section)
        return BCFG_ERR_INVALID;
    return add_section(s, section);
}

/* ------------------------------------------------------------------ */
/* public: validation                                                   */
/* ------------------------------------------------------------------ */

/*
 * Record a single violation into `out` (when non-NULL).  Failure to append a
 * diagnostic (out of memory) does not change the violation count: the problem
 * still happened, we just could not describe it.
 */
static void note(bcfg_diag *out, int code, const char *fmt,
                 const char *a, const char *b)
{
    if (!out)
        return;
    /* every variant here takes exactly two string arguments */
    bcfg_diag_add(out, BCFG_SEV_ERROR, code, 0, fmt, a, b);
}

int bcfg_schema_validate(const bcfg_schema *s, const bcfg_doc *doc,
                         bcfg_diag *out)
{
    if (!s || !doc)
        return 0;

    int violations = 0;

    for (size_t i = 0; i < s->nkeys; i++) {
        const key_rule *r = &s->keys[i];

        /* locate the section, then the key inside it */
        const bcfg_section *sec = bcfg_get_section(doc, r->section);
        const bcfg_entry   *e =
            sec ? bcfg_get_entry(sec, r->key) : NULL;

        if (!e) {
            /* absent key: a violation only when the rule is required */
            if (r->required) {
                violations++;
                note(out, BCFG_ERR_NOTFOUND,
                     "missing required key '%s' in section '%s'",
                     r->key, r->section);
            }
            continue;
        }

        /* present: the stored value type must match the declared type */
        if (e->value.type != r->type) {
            violations++;
            if (out) {
                bcfg_diag_add(out, BCFG_SEV_ERROR, BCFG_ERR_TYPE, 0,
                              "type mismatch for key '%s' in section '%s': "
                              "expected %s, got %s",
                              r->key, r->section,
                              bcfg_type_name(r->type),
                              bcfg_type_name(e->value.type));
            }
        }
    }

    return violations;
}

/* ------------------------------------------------------------------ */
/* public: teardown                                                     */
/* ------------------------------------------------------------------ */

void bcfg_schema_free(bcfg_schema *s)
{
    if (!s)
        return;

    for (size_t i = 0; i < s->nkeys; i++) {
        free(s->keys[i].section);
        free(s->keys[i].key);
    }
    free(s->keys);

    for (size_t i = 0; i < s->nsecs; i++)
        free(s->secs[i].section);
    free(s->secs);

    free(s);
}
