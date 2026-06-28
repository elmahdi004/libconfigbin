/*
 * test_expr.c -- query engine tests
 *
 * Build a two-section document by hand and exercise exact, wildcard-section,
 * wildcard-key, and array-index queries, asserting the match counts and the
 * selected sections/entries/indices.
 */

#include "../src/configbin.h"
#include "../src/expr.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

static void w8(uint8_t **p, uint8_t v)   { **p = v; (*p)++; }
static void w16(uint8_t **p, uint16_t v) { memcpy(*p, &v, 2); *p += 2; }
static void w32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }
static void wn(uint8_t **p, const void *s, size_t n) { memcpy(*p, s, n); *p += n; }

static size_t build(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 2);   /* 2 sections */

    /* [a] port=1, name="x", ids=[1,2,3] */
    w8(&p, 1); wn(&p, "a", 1); w32(&p, 3);
    w8(&p, 4); wn(&p, "port", 4); w8(&p, BCFG_TYPE_UINT32); w32(&p, 1);
    w8(&p, 4); wn(&p, "name", 4); w8(&p, BCFG_TYPE_STRING); w16(&p, 1); wn(&p, "x", 1);
    w8(&p, 3); wn(&p, "ids", 3); w8(&p, BCFG_TYPE_ARRAY);
        w8(&p, BCFG_TYPE_UINT32); w16(&p, 3); w32(&p, 1); w32(&p, 2); w32(&p, 3);

    /* [b] port=2, label="y" */
    w8(&p, 1); wn(&p, "b", 1); w32(&p, 2);
    w8(&p, 4); wn(&p, "port", 4); w8(&p, BCFG_TYPE_UINT32); w32(&p, 2);
    w8(&p, 5); wn(&p, "label", 5); w8(&p, BCFG_TYPE_STRING); w16(&p, 1); wn(&p, "y", 1);

    return (size_t)(p - buf);
}

static size_t run(const bcfg_doc *d, const char *q, bcfg_match **out)
{
    size_t n = 0;
    int rc = bcfg_expr_eval(d, q, out, &n);
    assert(rc == BCFG_OK);
    return n;
}

int main(void)
{
    uint8_t buf[256];
    size_t len = build(buf);
    bcfg_doc *d = NULL;
    assert(bcfg_parse(buf, len, &d) == BCFG_OK);

    bcfg_match *m = NULL;
    size_t n;

    n = run(d, "a.port", &m);
    assert(n == 1);
    assert(strcmp(m[0].section->name, "a") == 0);
    assert(strcmp(m[0].entry->key, "port") == 0);
    assert(m[0].array_index == -1);
    bcfg_expr_free(m); m = NULL;

    n = run(d, "*.port", &m);
    assert(n == 2);
    bcfg_expr_free(m); m = NULL;

    n = run(d, "a.*", &m);
    assert(n == 3);
    bcfg_expr_free(m); m = NULL;

    n = run(d, "a.ids[1]", &m);
    assert(n == 1);
    assert(m[0].array_index == 1);
    bcfg_expr_free(m); m = NULL;

    n = run(d, "a.ids[9]", &m);   /* out of range -> no match */
    assert(n == 0);
    bcfg_expr_free(m); m = NULL;

    n = run(d, "nope.port", &m);
    assert(n == 0);
    bcfg_expr_free(m); m = NULL;

    /* a malformed query is reported, not crashed */
    size_t z = 0;
    assert(bcfg_expr_eval(d, "", &m, &z) == BCFG_ERR_INVALID);

    bcfg_free(d);
    puts("PASS test_expr");
    puts("All tests passed.");
    return 0;
}
