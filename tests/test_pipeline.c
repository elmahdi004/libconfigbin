/*
 * test_pipeline.c -- end-to-end transform tests
 *
 * Exercises the ordinary path: a document with an in-range reference flattens
 * correctly, and a document with an empty section that is dropped during
 * canonicalization still flattens correctly as long as its reference does not
 * point past the surviving sections.
 */

#include "../src/pipeline.h"
#include "../src/buffer.h"
#include "../src/configbin.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int contains(const bcfg_buf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len < n) return 0;
    for (size_t i = 0; i + n <= b->len; i++)
        if (memcmp(b->data + i, s, n) == 0) return 1;
    return 0;
}

static void run(const char *text, bcfg_buf *out)
{
    bcfg_buf_init(out);
    assert(bcfg_pipeline_run(text, strlen(text), out) == BCFG_OK);
}

static void test_basic_flatten(void)
{
    /* "ref" points at section index 0 ("net") */
    const char *txt = "[net]\nport = 1\n[ref]\nr = @0\n";
    bcfg_buf out;
    run(txt, &out);
    assert(contains(&out, "net"));
    assert(contains(&out, "ref"));
    assert(contains(&out, "-> net"));
    bcfg_buf_free(&out);
    puts("PASS test_basic_flatten");
}

static void test_dropped_empty_still_safe(void)
{
    /* an empty section is folded away, but the reference still points in range */
    const char *txt = "[net]\nport = 1\n[empty]\n[ref]\nr = @0\n";
    bcfg_buf out;
    run(txt, &out);
    assert(contains(&out, "net"));
    assert(contains(&out, "-> net"));
    bcfg_buf_free(&out);
    puts("PASS test_dropped_empty_still_safe");
}

int main(void)
{
    test_basic_flatten();
    test_dropped_empty_still_safe();
    puts("All tests passed.");
    return 0;
}
