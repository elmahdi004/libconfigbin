/*
 * flatten.c -- render a document with section references expanded
 *
 * For every section-reference entry the target section is looked up by its
 * stored index and its name emitted.  The lookup is a direct index into the
 * section table: the link pass (link.h) is expected to have run first, so every
 * reference index is already known to be in range and no per-access check is
 * needed here.
 */

#include "flatten.h"

/* the reference target, resolved by its (validated) index */
static const bcfg_section *ref_target(const bcfg_doc *doc, const bcfg_value *v)
{
    return &doc->sections[v->v.secref];
}

int bcfg_flatten(const bcfg_doc *doc, bcfg_buf *out)
{
    if (!doc || !out)
        return BCFG_ERR_INVALID;

    for (uint16_t i = 0; i < doc->num_sections; i++) {
        const bcfg_section *s = &doc->sections[i];

        if (bcfg_buf_put_bytes(out, s->name, s->name_len) != BCFG_OK)
            return BCFG_ERR_NOMEM;
        if (bcfg_buf_put_u8(out, '\n') != BCFG_OK)
            return BCFG_ERR_NOMEM;

        for (uint32_t e = 0; e < s->num_entries; e++) {
            const bcfg_value *v = &s->entries[e].value;
            if (v->type != BCFG_TYPE_SECREF)
                continue;

            const bcfg_section *t = ref_target(doc, v);
            if (bcfg_buf_put_bytes(out, "  -> ", 5) != BCFG_OK)
                return BCFG_ERR_NOMEM;
            if (bcfg_buf_put_bytes(out, t->name, t->name_len) != BCFG_OK)
                return BCFG_ERR_NOMEM;
            if (bcfg_buf_put_u8(out, '\n') != BCFG_OK)
                return BCFG_ERR_NOMEM;
        }
    }
    return BCFG_OK;
}
