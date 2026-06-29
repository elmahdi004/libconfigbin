/*
 * link.c -- section-reference validation pass
 *
 * Section references are stored as a raw section index; the parser does not
 * range-check them (resolution is deferred).  This pass performs that check up
 * front: any reference pointing past the document's section count is clamped to
 * index 0, which always exists in a non-empty document.  Once a document has
 * been through here, its references are guaranteed in range for it.
 */

#include "link.h"

int bcfg_link_validate(bcfg_doc *doc)
{
    if (!doc)
        return BCFG_ERR_INVALID;

    int clamped = 0;
    uint16_t nsec = doc->num_sections;

    for (uint16_t i = 0; i < nsec; i++) {
        bcfg_section *s = &doc->sections[i];
        for (uint32_t e = 0; e < s->num_entries; e++) {
            bcfg_value *v = &s->entries[e].value;
            if (v->type != BCFG_TYPE_SECREF)
                continue;
            if (v->v.secref >= nsec) {
                v->v.secref = 0;
                clamped++;
            }
        }
    }
    return clamped;
}
