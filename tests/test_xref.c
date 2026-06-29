/*
 * test_xref.c -- two-document cross-reference merge tests
 *
 * Merge a base and patch document where a base section references another and
 * the patch contributes an extra section; confirm the rendered listing folds
 * in the referenced section and that the patch section appears.  Inputs are
 * small so the merge stays on the ordinary path.
 */

#include "../src/xref.h"
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

/* base: section "a" with a SECREF -> section index 1 ("b"); section "b" */
static size_t build_base(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 2);

    w8(&p, 1); wn(&p, "a", 1); w32(&p, 1);
    w8(&p, 1); wn(&p, "r", 1); w8(&p, BCFG_TYPE_SECREF); w16(&p, 1);

    w8(&p, 1); wn(&p, "b", 1); w32(&p, 1);
    w8(&p, 1); wn(&p, "k", 1); w8(&p, BCFG_TYPE_NULL);

    return (size_t)(p - buf);
}

/* patch: a single new section "c" */
static size_t build_patch(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 1);
    w8(&p, 1); wn(&p, "c", 1); w32(&p, 1);
    w8(&p, 1); wn(&p, "k", 1); w8(&p, BCFG_TYPE_NULL);
    return (size_t)(p - buf);
}

static int contains(const bcfg_buf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len < n) return 0;
    for (size_t i = 0; i + n <= b->len; i++)
        if (memcmp(b->data + i, s, n) == 0) return 1;
    return 0;
}

static void test_merge_expand(void)
{
    uint8_t base[128], patch[128];
    size_t bn = build_base(base);
    size_t pn = build_patch(patch);

    bcfg_buf out;
    bcfg_buf_init(&out);
    assert(bcfg_xref_merge(base, bn, patch, pn, &out) == BCFG_OK);

    assert(contains(&out, "a"));
    assert(contains(&out, "b"));   /* folded in via the reference */
    assert(contains(&out, "c"));   /* contributed by the patch    */

    bcfg_buf_free(&out);
    puts("PASS test_merge_expand");
}

static void test_unparsable_inputs(void)
{
    bcfg_buf out;
    bcfg_buf_init(&out);
    /* garbage inputs contribute nothing but must not fault */
    assert(bcfg_xref_merge((const uint8_t *)"xx", 2,
                           (const uint8_t *)"yy", 2, &out) == BCFG_OK);
    bcfg_buf_free(&out);
    puts("PASS test_unparsable_inputs");
}

int main(void)
{
    test_merge_expand();
    test_unparsable_inputs();
    puts("All tests passed.");
    return 0;
}
