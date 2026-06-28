/*
 * iter.c -- visitor / walk API
 *
 * Depth-first traversal of a parsed bcfg_doc.  For each section we invoke the
 * section callback, then for each of its entries the entry callback, and for
 * entries whose value is a fixed-width array, the array-element callback once
 * per element.  Any callback may be NULL.  A callback returning non-zero stops
 * the walk immediately, and that stop code is returned to the caller.
 */

#include "iter.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Wire size in bytes of a fixed-width element type, or 0 for variable-width or
 * unknown types.  This mirrors the table used by the array parser in value.c;
 * arrays only ever hold fixed-width element types, so a 0 here means we cannot
 * stride the payload and therefore skip per-element iteration defensively.
 */
static size_t elem_fixed_size(uint8_t t)
{
    switch (t) {
    case BCFG_TYPE_BOOL:      return 1;
    case BCFG_TYPE_UINT32:    return 4;
    case BCFG_TYPE_INT64:     return 8;
    case BCFG_TYPE_FLOAT64:   return 8;
    case BCFG_TYPE_TIMESTAMP: return 8;
    default:                  return 0;
    }
}

/*
 * Walk the elements of a single ARRAY-typed entry.  Returns the first non-zero
 * stop code from on_array_elem, or 0 if every element was visited (or there was
 * nothing to visit).
 */
static int walk_array(const bcfg_entry *ent, const bcfg_visitor *vis, void *ctx)
{
    const bcfg_value *val;
    const uint8_t    *base;
    size_t            stride;
    uint16_t          count;

    /* Caller guarantees vis and vis->on_array_elem are non-NULL. */
    val = &ent->value;
    if (val->type != BCFG_TYPE_ARRAY)
        return 0;

    base   = (const uint8_t *)val->v.array.data;
    count  = val->v.array.count;
    stride = elem_fixed_size(val->v.array.elem_type);

    /* No payload, no known element width, or an empty array: nothing to do. */
    if (base == NULL || stride == 0 || count == 0)
        return 0;

    for (uint16_t i = 0; i < count; i++) {
        const void *elem = base + (size_t)i * stride;
        int rc = vis->on_array_elem(ent, i, elem, ctx);
        if (rc != 0)
            return rc;
    }

    return 0;
}

/*
 * Walk every entry of one section: the entry callback first, then (for arrays)
 * the per-element callback.  Returns the first non-zero stop code, else 0.
 */
static int walk_section(const bcfg_section *sec, const bcfg_visitor *vis,
                        void *ctx)
{
    if (sec->entries == NULL || sec->num_entries == 0)
        return 0;

    for (uint32_t i = 0; i < sec->num_entries; i++) {
        const bcfg_entry *ent = &sec->entries[i];
        int rc;

        if (vis->on_entry != NULL) {
            rc = vis->on_entry(sec, ent, ctx);
            if (rc != 0)
                return rc;
        }

        if (vis->on_array_elem != NULL &&
            ent->value.type == BCFG_TYPE_ARRAY) {
            rc = walk_array(ent, vis, ctx);
            if (rc != 0)
                return rc;
        }
    }

    return 0;
}

int bcfg_walk(const bcfg_doc *doc, const bcfg_visitor *vis, void *ctx)
{
    /* With nothing to traverse or no callbacks to fire, the walk trivially
       completes with code 0. */
    if (doc == NULL || vis == NULL)
        return 0;

    if (doc->sections == NULL || doc->num_sections == 0)
        return 0;

    for (uint16_t i = 0; i < doc->num_sections; i++) {
        const bcfg_section *sec = &doc->sections[i];
        int rc;

        if (vis->on_section != NULL) {
            rc = vis->on_section(sec, ctx);
            if (rc != 0)
                return rc;
        }

        rc = walk_section(sec, vis, ctx);
        if (rc != 0)
            return rc;
    }

    return 0;
}

size_t bcfg_count_entries(const bcfg_doc *doc)
{
    size_t total = 0;

    if (doc == NULL || doc->sections == NULL)
        return 0;

    for (uint16_t i = 0; i < doc->num_sections; i++)
        total += doc->sections[i].num_entries;

    return total;
}
