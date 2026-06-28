/*
 * test_merge.c -- overlay merge + deep-clone ownership tests
 *
 * Build a base and an overlay document, bcfg_merge them, and assert:
 *   - overlay entries win on conflict (by section + key),
 *   - base-only and overlay-only entries both survive,
 *   - deep values (string + array) in the merged doc are independently owned,
 *     so freeing the base leaves the merged doc fully valid.
 */

#include "../src/configbin.h"
#include "../src/merge.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

/* helper: write little-endian values */
static void w8(uint8_t **p, uint8_t v)  { **p = v; (*p)++; }
static void w16(uint8_t **p, uint16_t v){ memcpy(*p, &v, 2); *p+=2; }
static void w32(uint8_t **p, uint32_t v){ memcpy(*p, &v, 4); *p+=4; }
static void wn(uint8_t **p, const void *s, size_t n) { memcpy(*p,s,n); *p+=n; }

/* Base: [server] port=uint32 80, host=string "base-host"
 *       [extra]  only=uint32 1
 */
static size_t build_base(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 2);

    w8(&p, 6); wn(&p, "server", 6);
    w32(&p, 2);
    w8(&p, 4); wn(&p, "port", 4);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 80);
    w8(&p, 4); wn(&p, "host", 4);
    w8(&p, BCFG_TYPE_STRING);
    w16(&p, 9); wn(&p, "base-host", 9);

    w8(&p, 5); wn(&p, "extra", 5);
    w32(&p, 1);
    w8(&p, 4); wn(&p, "only", 4);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 1);

    return (size_t)(p - buf);
}

/* Overlay: [server] port=uint32 443 (overrides),
 *                   tags=array<uint32>[3] {1,2,3} (overlay-only key)
 *          [new]    flag=string "fresh" (overlay-only section)
 */
static size_t build_overlay(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 2);

    w8(&p, 6); wn(&p, "server", 6);
    w32(&p, 2);
    w8(&p, 4); wn(&p, "port", 4);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 443);
    w8(&p, 4); wn(&p, "tags", 4);
    w8(&p, BCFG_TYPE_ARRAY);
    w8(&p, BCFG_TYPE_UINT32);
    w16(&p, 3);
    w32(&p, 1); w32(&p, 2); w32(&p, 3);

    w8(&p, 3); wn(&p, "new", 3);
    w32(&p, 1);
    w8(&p, 4); wn(&p, "flag", 4);
    w8(&p, BCFG_TYPE_STRING);
    w16(&p, 5); wn(&p, "fresh", 5);

    return (size_t)(p - buf);
}

static void test_merge(void)
{
    uint8_t bb[256], ob[256];
    size_t blen = build_base(bb);
    size_t olen = build_overlay(ob);

    bcfg_doc *base = NULL, *overlay = NULL;
    assert(bcfg_parse(bb, blen, &base) == BCFG_OK);
    assert(bcfg_parse(ob, olen, &overlay) == BCFG_OK);

    bcfg_doc *merged = NULL;
    assert(bcfg_merge(base, overlay, &merged) == BCFG_OK);
    assert(merged != NULL);

    /* overlay wins on conflict */
    bcfg_section *ms = bcfg_get_section(merged, "server");
    assert(ms != NULL);
    bcfg_entry *port = bcfg_get_entry(ms, "port");
    assert(port != NULL);
    assert(port->value.type == BCFG_TYPE_UINT32);
    assert(port->value.v.u32 == 443);

    /* base-only key survives */
    bcfg_entry *host = bcfg_get_entry(ms, "host");
    assert(host != NULL);
    assert(host->value.type == BCFG_TYPE_STRING);
    assert(strcmp(host->value.v.str.data, "base-host") == 0);

    /* overlay-only key added to the shared section */
    bcfg_entry *tags = bcfg_get_entry(ms, "tags");
    assert(tags != NULL);
    assert(tags->value.type == BCFG_TYPE_ARRAY);
    assert(tags->value.v.array.count == 3);

    /* base-only section survives */
    bcfg_section *extra = bcfg_get_section(merged, "extra");
    assert(extra != NULL);
    assert(bcfg_get_entry(extra, "only") != NULL);

    /* overlay-only section added */
    bcfg_section *neu = bcfg_get_section(merged, "new");
    assert(neu != NULL);
    bcfg_entry *flag = bcfg_get_entry(neu, "flag");
    assert(flag != NULL);
    assert(strcmp(flag->value.v.str.data, "fresh") == 0);

    /* deep ownership: free base + overlay, the merged doc stays valid */
    bcfg_free(base);
    bcfg_free(overlay);

    ms = bcfg_get_section(merged, "server");
    host = bcfg_get_entry(ms, "host");
    assert(strcmp(host->value.v.str.data, "base-host") == 0);   /* string ok */

    tags = bcfg_get_entry(ms, "tags");
    uint32_t *tv = (uint32_t *)tags->value.v.array.data;
    assert(tv[0] == 1 && tv[1] == 2 && tv[2] == 3);             /* array ok */

    neu = bcfg_get_section(merged, "new");
    flag = bcfg_get_entry(neu, "flag");
    assert(strcmp(flag->value.v.str.data, "fresh") == 0);

    bcfg_free(merged);
    puts("PASS test_merge");
}

/* bcfg_doc_clone yields a fully independent copy: mutate/free the original and
   the clone is unaffected. */
static void test_clone_independent(void)
{
    uint8_t bb[256];
    size_t blen = build_base(bb);

    bcfg_doc *base = NULL;
    assert(bcfg_parse(bb, blen, &base) == BCFG_OK);

    bcfg_doc *copy = bcfg_doc_clone(base);
    assert(copy != NULL);
    assert(copy->num_sections == base->num_sections);

    /* the clone's string payload is a distinct allocation */
    bcfg_section *bs = bcfg_get_section(base, "server");
    bcfg_section *cs = bcfg_get_section(copy, "server");
    bcfg_entry *bh = bcfg_get_entry(bs, "host");
    bcfg_entry *ch = bcfg_get_entry(cs, "host");
    assert(bh->value.v.str.data != ch->value.v.str.data);

    bcfg_free(base);

    /* clone still readable after base is gone */
    cs = bcfg_get_section(copy, "server");
    ch = bcfg_get_entry(cs, "host");
    assert(strcmp(ch->value.v.str.data, "base-host") == 0);

    bcfg_free(copy);
    puts("PASS test_clone_independent");
}

int main(void)
{
    test_merge();
    test_clone_independent();
    puts("All tests passed.");
    return 0;
}
