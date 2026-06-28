/*
 * test_iter.c -- visitor / walk traversal tests
 *
 * Walk a multi-section document with a bcfg_visitor, counting sections,
 * entries and array elements through the ctx, and assert the totals.  Then
 * verify early-stop: a callback that returns non-zero halts the walk and that
 * stop code propagates back to the caller.
 */

#include "../src/configbin.h"
#include "../src/iter.h"
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

/* Two sections, three entries total, one of which is a 3-element array:
 *   [a] x=uint32 1, ids=array<uint32>[3] {4,5,6}
 *   [b] y=string "hi"
 */
static size_t build_doc(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 2);

    w8(&p, 1); wn(&p, "a", 1);
    w32(&p, 2);
    w8(&p, 1); wn(&p, "x", 1);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 1);
    w8(&p, 3); wn(&p, "ids", 3);
    w8(&p, BCFG_TYPE_ARRAY);
    w8(&p, BCFG_TYPE_UINT32);
    w16(&p, 3);
    w32(&p, 4); w32(&p, 5); w32(&p, 6);

    w8(&p, 1); wn(&p, "b", 1);
    w32(&p, 1);
    w8(&p, 1); wn(&p, "y", 1);
    w8(&p, BCFG_TYPE_STRING);
    w16(&p, 2); wn(&p, "hi", 2);

    return (size_t)(p - buf);
}

/* counters threaded through the walk via ctx */
typedef struct {
    int sections;
    int entries;
    int elems;
    uint32_t elem_sum;
} counts;

static int count_section(const bcfg_section *sec, void *ctx)
{
    (void)sec;
    ((counts *)ctx)->sections++;
    return 0;
}

static int count_entry(const bcfg_section *sec, const bcfg_entry *ent,
                       void *ctx)
{
    (void)sec; (void)ent;
    ((counts *)ctx)->entries++;
    return 0;
}

static int count_elem(const bcfg_entry *ent, uint16_t index,
                      const void *elem, void *ctx)
{
    (void)ent; (void)index;
    counts *c = (counts *)ctx;
    c->elems++;
    c->elem_sum += *(const uint32_t *)elem;
    return 0;
}

static void test_walk_counts(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    bcfg_visitor vis;
    memset(&vis, 0, sizeof vis);
    vis.on_section    = count_section;
    vis.on_entry      = count_entry;
    vis.on_array_elem = count_elem;

    counts c;
    memset(&c, 0, sizeof c);

    int rc = bcfg_walk(doc, &vis, &c);
    assert(rc == 0);
    assert(c.sections == 2);
    assert(c.entries == 3);
    assert(c.elems == 3);
    assert(c.elem_sum == 4 + 5 + 6);

    /* cross-check against the convenience counter */
    assert(bcfg_count_entries(doc) == 3);

    bcfg_free(doc);
    puts("PASS test_walk_counts");
}

/* a visitor whose entry callback stops after N entries */
typedef struct {
    int seen;
    int limit;
} stopper;

static int stop_after_n(const bcfg_section *sec, const bcfg_entry *ent,
                        void *ctx)
{
    (void)sec; (void)ent;
    stopper *s = (stopper *)ctx;
    s->seen++;
    if (s->seen >= s->limit)
        return 42;   /* non-zero stop code */
    return 0;
}

static void test_walk_early_stop(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    bcfg_visitor vis;
    memset(&vis, 0, sizeof vis);
    vis.on_entry = stop_after_n;

    stopper s = { 0, 2 };
    int rc = bcfg_walk(doc, &vis, &s);

    /* the callback's non-zero code propagates, and we stopped at the limit */
    assert(rc == 42);
    assert(s.seen == 2);

    bcfg_free(doc);
    puts("PASS test_walk_early_stop");
}

/* a section callback that stops immediately means no entries are visited */
static int stop_on_first_section(const bcfg_section *sec, void *ctx)
{
    (void)sec;
    *(int *)ctx += 1;
    return 7;
}

static void test_walk_stop_section(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    bcfg_visitor vis;
    memset(&vis, 0, sizeof vis);
    vis.on_section = stop_on_first_section;

    int hits = 0;
    int rc = bcfg_walk(doc, &vis, &hits);
    assert(rc == 7);
    assert(hits == 1);   /* stopped on the very first section */

    bcfg_free(doc);
    puts("PASS test_walk_stop_section");
}

int main(void)
{
    test_walk_counts();
    test_walk_early_stop();
    test_walk_stop_section();
    puts("All tests passed.");
    return 0;
}
