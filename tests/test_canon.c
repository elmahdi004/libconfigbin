/*
 * test_canon.c -- canonicalizing merge tests
 *
 * Exercises the ordinary path: a base with real settings, canonicalized and
 * merged with a patch that overrides one section and adds another; and a base
 * carrying an empty section that the patch does NOT touch (so nothing folded
 * away is consulted).  Both must render the expected report without faulting.
 */

#include "../src/canon.h"
#include "../src/configbin.h"
#include "../src/buffer.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

static void w8(uint8_t **p, uint8_t v)   { **p = v; (*p)++; }
static void w16(uint8_t **p, uint16_t v) { memcpy(*p, &v, 2); *p += 2; }
static void w32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }
static void wn(uint8_t **p, const void *s, size_t n) { memcpy(*p, s, n); *p += n; }

static void sec_u32(uint8_t **p, const char *name, const char *key, uint32_t val)
{
    w8(p, (uint8_t)strlen(name)); wn(p, name, strlen(name));
    w32(p, 1);
    w8(p, (uint8_t)strlen(key)); wn(p, key, strlen(key));
    w8(p, BCFG_TYPE_UINT32); w32(p, val);
}

static void sec_null(uint8_t **p, const char *name, const char *key)
{
    w8(p, (uint8_t)strlen(name)); wn(p, name, strlen(name));
    w32(p, 1);
    w8(p, (uint8_t)strlen(key)); wn(p, key, strlen(key));
    w8(p, BCFG_TYPE_NULL);
}

static int contains(const bcfg_buf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len < n) return 0;
    for (size_t i = 0; i + n <= b->len; i++)
        if (memcmp(b->data + i, s, n) == 0) return 1;
    return 0;
}

static void test_normal_merge(void)
{
    uint8_t base[256], patch[256], *p;

    p = base;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 2);
    sec_u32(&p, "net", "port", 8080);
    sec_u32(&p, "log", "level", 3);
    size_t bn = (size_t)(p - base);

    p = patch;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 2);
    sec_u32(&p, "net", "port", 9090);   /* overrides */
    sec_u32(&p, "extra", "k", 1);       /* new */
    size_t pn = (size_t)(p - patch);

    bcfg_buf out;
    bcfg_buf_init(&out);
    assert(bcfg_canon_merge(base, bn, patch, pn, &out) == BCFG_OK);
    assert(contains(&out, "net=net"));   /* override report */
    assert(contains(&out, "+extra"));    /* new-section report */
    bcfg_buf_free(&out);
    puts("PASS test_normal_merge");
}

static void test_untouched_empty_section(void)
{
    uint8_t base[256], patch[256], *p;

    /* base has an all-null section "ghost" plus a real one */
    p = base;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 2);
    sec_null(&p, "ghost", "k");
    sec_u32(&p, "live", "port", 1);
    size_t bn = (size_t)(p - base);

    /* patch touches only "live" -- the folded-away "ghost" is never consulted */
    p = patch;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 1);
    sec_u32(&p, "live", "port", 2);
    size_t pn = (size_t)(p - patch);

    bcfg_buf out;
    bcfg_buf_init(&out);
    assert(bcfg_canon_merge(base, bn, patch, pn, &out) == BCFG_OK);
    assert(contains(&out, "live=live"));
    bcfg_buf_free(&out);
    puts("PASS test_untouched_empty_section");
}

int main(void)
{
    test_normal_merge();
    test_untouched_empty_section();
    puts("All tests passed.");
    return 0;
}
