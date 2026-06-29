/*
 * canon.c -- canonicalizing merge
 *
 * Pipeline:
 *   1. parse the base document;
 *   2. index its sections by name;
 *   3. canonicalize: re-serialize the base, dropping sections that hold no
 *      effective settings, and re-parse the result;
 *   4. rebind the index onto the re-parsed canonical document, then merge the
 *      patch document against it.
 *
 * Canonicalization is what makes the base and its canonical form differ: a
 * section whose every entry is null carries no settings and is folded away, so
 * it is present in the original parse but not in the canonical re-parse.
 */

#include "canon.h"
#include "writer.h"
#include <stdlib.h>
#include <string.h>

/* a name -> section binding; name is an owned copy, sec borrows from a doc */
typedef struct {
    char               *name;
    size_t              name_len;
    const bcfg_section *sec;
} cref;

typedef struct {
    cref  *e;
    size_t n;
    size_t cap;
} ctab;

static int ctab_add(ctab *t, const char *name, size_t name_len,
                    const bcfg_section *sec)
{
    if (t->n == t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 8;
        cref  *ne = (cref *)realloc(t->e, ncap * sizeof(*ne));
        if (!ne)
            return BCFG_ERR_NOMEM;
        t->e = ne;
        t->cap = ncap;
    }
    char *copy = (char *)malloc(name_len + 1);
    if (!copy)
        return BCFG_ERR_NOMEM;
    memcpy(copy, name, name_len);
    copy[name_len] = '\0';

    t->e[t->n].name     = copy;
    t->e[t->n].name_len = name_len;
    t->e[t->n].sec      = sec;
    t->n++;
    return BCFG_OK;
}

static cref *ctab_find(ctab *t, const char *name, size_t name_len)
{
    for (size_t i = 0; i < t->n; i++)
        if (t->e[i].name_len == name_len &&
            memcmp(t->e[i].name, name, name_len) == 0)
            return &t->e[i];
    return NULL;
}

static void ctab_free(ctab *t)
{
    for (size_t i = 0; i < t->n; i++)
        free(t->e[i].name);
    free(t->e);
    t->e = NULL;
    t->n = t->cap = 0;
}

/* a section carries settings if at least one entry is non-null */
static int section_has_settings(const bcfg_section *s)
{
    for (uint32_t i = 0; i < s->num_entries; i++)
        if (s->entries[i].value.type != BCFG_TYPE_NULL)
            return 1;
    return 0;
}

/* serialize doc, dropping sections without settings (the canonical form) */
static int canon_serialize(const bcfg_doc *doc, bcfg_buf *out)
{
    uint16_t kept = 0;
    for (uint16_t i = 0; i < doc->num_sections; i++)
        if (section_has_settings(&doc->sections[i]))
            kept++;

    if (bcfg_buf_put_u32(out, BCFG_MAGIC) != BCFG_OK) return BCFG_ERR_NOMEM;
    if (bcfg_buf_put_u16(out, BCFG_VERSION) != BCFG_OK) return BCFG_ERR_NOMEM;
    if (bcfg_buf_put_u16(out, kept) != BCFG_OK) return BCFG_ERR_NOMEM;

    for (uint16_t i = 0; i < doc->num_sections; i++) {
        const bcfg_section *s = &doc->sections[i];
        if (!section_has_settings(s))
            continue;
        if (bcfg_buf_put_u8(out, s->name_len) != BCFG_OK) return BCFG_ERR_NOMEM;
        if (bcfg_buf_put_bytes(out, s->name, s->name_len) != BCFG_OK) return BCFG_ERR_NOMEM;
        if (bcfg_buf_put_u32(out, s->num_entries) != BCFG_OK) return BCFG_ERR_NOMEM;
        for (uint32_t e = 0; e < s->num_entries; e++) {
            const bcfg_entry *en = &s->entries[e];
            if (bcfg_buf_put_u8(out, en->key_len) != BCFG_OK) return BCFG_ERR_NOMEM;
            if (bcfg_buf_put_bytes(out, en->key, en->key_len) != BCFG_OK) return BCFG_ERR_NOMEM;
            if (bcfg_write_value(&en->value, out) != BCFG_OK) return BCFG_ERR_INVALID;
        }
    }
    return BCFG_OK;
}

/*
 * Rebind a section to the canonical document.  If the section was folded away
 * during canonicalization it is not present there, so the original binding is
 * kept as a fallback so callers still have a section to report.
 */
static const bcfg_section *rebind(const bcfg_doc *canon, const cref *c)
{
    bcfg_section *s = bcfg_get_section(canon, c->name);
    return s ? s : c->sec;
}

int bcfg_canon_merge(const uint8_t *base, size_t blen,
                     const uint8_t *patch, size_t plen,
                     bcfg_buf *out)
{
    if (!out)
        return BCFG_ERR_INVALID;

    bcfg_doc *A = NULL, *A2 = NULL, *B = NULL;
    ctab tab = {0};
    int rc = BCFG_OK;

    if (bcfg_parse(base, blen, &A) != BCFG_OK)
        A = NULL;

    if (A) {
        /* index every base section by name */
        for (uint16_t i = 0; i < A->num_sections; i++) {
            bcfg_section *s = &A->sections[i];
            if (ctab_add(&tab, s->name, s->name_len, s) != BCFG_OK) {
                rc = BCFG_ERR_NOMEM;
                goto cleanup;
            }
        }

        /* canonicalize: serialize without empty sections, then re-parse */
        bcfg_buf sa;
        bcfg_buf_init(&sa);
        if (canon_serialize(A, &sa) == BCFG_OK)
            (void)bcfg_parse(sa.data, sa.len, &A2);
        bcfg_buf_free(&sa);

        /* point the index at the canonical document */
        if (A2) {
            for (size_t k = 0; k < tab.n; k++)
                tab.e[k].sec = rebind(A2, &tab.e[k]);
        }

        /* the original parse is no longer needed now that we have the canon */
        bcfg_free(A);
        A = NULL;
    }

    /* merge the patch against the canonical index */
    if (bcfg_parse(patch, plen, &B) != BCFG_OK)
        B = NULL;

    if (B) {
        for (uint16_t i = 0; i < B->num_sections; i++) {
            bcfg_section *sb = &B->sections[i];
            cref *c = ctab_find(&tab, sb->name, sb->name_len);
            if (c) {
                /* overrides a canonical section: report canon=patch */
                if (bcfg_buf_put_bytes(out, c->sec->name, c->sec->name_len) != BCFG_OK ||
                    bcfg_buf_put_u8(out, '=') != BCFG_OK ||
                    bcfg_buf_put_bytes(out, sb->name, sb->name_len) != BCFG_OK ||
                    bcfg_buf_put_u8(out, '\n') != BCFG_OK) {
                    rc = BCFG_ERR_NOMEM;
                    goto cleanup;
                }
            } else {
                /* new section contributed by the patch */
                if (bcfg_buf_put_u8(out, '+') != BCFG_OK ||
                    bcfg_buf_put_bytes(out, sb->name, sb->name_len) != BCFG_OK ||
                    bcfg_buf_put_u8(out, '\n') != BCFG_OK) {
                    rc = BCFG_ERR_NOMEM;
                    goto cleanup;
                }
            }
        }
    }

cleanup:
    ctab_free(&tab);
    bcfg_free(A);
    bcfg_free(A2);
    bcfg_free(B);
    return rc;
}
