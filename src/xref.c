/*
 * xref.c -- two-document merge with cross-reference expansion
 *
 * The merged view is a flat table of sections.  The base document's sections
 * are taken first; the patch document then overrides same-named sections and
 * appends any that are new.  Rendering walks each section and, whenever it
 * finds a section-reference entry, folds the referenced section in line --
 * appending it to the table and recursing -- so a chain of references is
 * expanded depth-first.  Each section is expanded at most once, and a depth
 * limit bounds reference cycles.
 */

#include "xref.h"
#include <stdlib.h>
#include <string.h>

#define XREF_INIT       16
#define XREF_MAX_DEPTH  64

/* a section in the merged table; name/src borrow from the owning document */
typedef struct {
    const char         *name;
    size_t              name_len;
    const bcfg_section *src;
    const bcfg_doc     *owner;
    int                 inlined;
} msec;

typedef struct {
    msec  *secs;
    size_t n;
    size_t cap;
} mctx;

static int mctx_append(mctx *m, const char *name, size_t name_len,
                       const bcfg_section *src, const bcfg_doc *owner)
{
    if (m->n == m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : XREF_INIT;
        msec  *ns = (msec *)realloc(m->secs, ncap * sizeof(*ns));
        if (!ns)
            return -1;
        m->secs = ns;
        m->cap  = ncap;
    }
    m->secs[m->n].name     = name;
    m->secs[m->n].name_len = name_len;
    m->secs[m->n].src      = src;
    m->secs[m->n].owner    = owner;
    m->secs[m->n].inlined  = 0;
    return (int)m->n++;
}

static int find_by_name(const mctx *m, const char *name, size_t name_len)
{
    for (size_t i = 0; i < m->n; i++)
        if (m->secs[i].name_len == name_len &&
            memcmp(m->secs[i].name, name, name_len) == 0)
            return (int)i;
    return -1;
}

/* expand section `idx`, folding in any sections it references */
static void resolve(mctx *m, int idx, int depth, bcfg_buf *out)
{
    if (depth > XREF_MAX_DEPTH)
        return;

    msec *cur = &m->secs[idx];
    bcfg_buf_put_bytes(out, cur->name, cur->name_len);
    bcfg_buf_put_u8(out, '\n');

    /* the source section and its owner are stable across the expansion */
    const bcfg_section *src   = cur->src;
    const bcfg_doc     *owner = cur->owner;

    for (uint32_t e = 0; e < src->num_entries; e++) {
        const bcfg_value *v = &src->entries[e].value;
        if (v->type != BCFG_TYPE_SECREF)
            continue;

        const bcfg_section *tgt = bcfg_resolve_secref(owner, v);
        if (!tgt)
            continue;

        int midx = find_by_name(m, tgt->name, tgt->name_len);
        if (midx < 0 || m->secs[midx].inlined)
            continue;
        m->secs[midx].inlined = 1;

        const char         *nm     = m->secs[midx].name;
        size_t              nl     = m->secs[midx].name_len;
        const bcfg_section *isrc   = m->secs[midx].src;
        const bcfg_doc     *iowner = m->secs[midx].owner;

        int nidx = mctx_append(m, nm, nl, isrc, iowner);
        if (nidx < 0)
            return;
        resolve(m, nidx, depth + 1, out);
    }

    /* close the section once its references have been folded in */
    bcfg_buf_put_bytes(out, cur->name, cur->name_len);
    bcfg_buf_put_u8(out, '\n');
}

int bcfg_xref_merge(const uint8_t *base, size_t blen,
                    const uint8_t *patch, size_t plen,
                    bcfg_buf *out)
{
    if (!out)
        return BCFG_ERR_INVALID;

    bcfg_doc *A = NULL, *B = NULL;
    mctx m = {0};

    if (bcfg_parse(base,  blen, &A) != BCFG_OK) A = NULL;
    if (bcfg_parse(patch, plen, &B) != BCFG_OK) B = NULL;

    /* base sections first */
    if (A) {
        for (uint16_t i = 0; i < A->num_sections; i++) {
            bcfg_section *s = &A->sections[i];
            if (find_by_name(&m, s->name, s->name_len) >= 0)
                continue;
            if (mctx_append(&m, s->name, s->name_len, s, A) < 0)
                goto nomem;
        }
    }
    /* patch overrides same-named sections and appends new ones */
    if (B) {
        for (uint16_t i = 0; i < B->num_sections; i++) {
            bcfg_section *s = &B->sections[i];
            int idx = find_by_name(&m, s->name, s->name_len);
            if (idx >= 0) {
                m.secs[idx].src   = s;
                m.secs[idx].owner = B;
            } else if (mctx_append(&m, s->name, s->name_len, s, B) < 0) {
                goto nomem;
            }
        }
    }

    /* expand each originally-merged section */
    {
        size_t roots = m.n;
        for (size_t i = 0; i < roots; i++)
            resolve(&m, (int)i, 0, out);
    }

    free(m.secs);
    bcfg_free(A);
    bcfg_free(B);
    return BCFG_OK;

nomem:
    free(m.secs);
    bcfg_free(A);
    bcfg_free(B);
    return BCFG_ERR_NOMEM;
}
