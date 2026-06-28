/*
 * test_path.c -- dotted-path query tests
 *
 * Parse a multi-section document and assert that bcfg_query,
 * bcfg_query_uint32 and bcfg_query_string resolve the correct values, that a
 * valid trailing "[index]" is range-checked, and that missing/malformed paths
 * yield NULL or the documented non-zero error codes.
 */

#include "../src/configbin.h"
#include "../src/path.h"
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

/* Two sections:
 *   [server] port=uint32 9090, name=string "alpha"
 *   [data]   ids=array<uint32>[3] {7,8,9}
 */
static size_t build_doc(uint8_t *buf)
{
    uint8_t *p = buf;

    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 2);   /* 2 sections */

    /* section 0: server */
    w8(&p, 6); wn(&p, "server", 6);
    w32(&p, 2);
    w8(&p, 4); wn(&p, "port", 4);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 9090);
    w8(&p, 4); wn(&p, "name", 4);
    w8(&p, BCFG_TYPE_STRING);
    w16(&p, 5); wn(&p, "alpha", 5);

    /* section 1: data */
    w8(&p, 4); wn(&p, "data", 4);
    w32(&p, 1);
    w8(&p, 3); wn(&p, "ids", 3);
    w8(&p, BCFG_TYPE_ARRAY);
    w8(&p, BCFG_TYPE_UINT32);
    w16(&p, 3);
    w32(&p, 7); w32(&p, 8); w32(&p, 9);

    return (size_t)(p - buf);
}

static void test_query_scalar(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    /* generic query returns the entry value */
    const bcfg_value *v = bcfg_query(doc, "server.port");
    assert(v != NULL);
    assert(v->type == BCFG_TYPE_UINT32);
    assert(v->v.u32 == 9090);

    const bcfg_value *vs = bcfg_query(doc, "server.name");
    assert(vs != NULL);
    assert(vs->type == BCFG_TYPE_STRING);
    assert(strcmp(vs->v.str.data, "alpha") == 0);

    /* section resolution */
    const bcfg_section *sec = bcfg_query_section(doc, "data");
    assert(sec != NULL);
    assert(memcmp(sec->name, "data", 4) == 0);
    assert(bcfg_query_section(doc, "nope") == NULL);

    bcfg_free(doc);
    puts("PASS test_query_scalar");
}

static void test_query_typed(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    uint32_t u = 0;
    assert(bcfg_query_uint32(doc, "server.port", &u) == BCFG_OK);
    assert(u == 9090);

    const char *s = NULL;
    uint16_t slen = 0;
    assert(bcfg_query_string(doc, "server.name", &s, &slen) == BCFG_OK);
    assert(slen == 5);
    assert(memcmp(s, "alpha", 5) == 0);

    /* type mismatches: port is not a string, name is not a uint32 */
    assert(bcfg_query_uint32(doc, "server.name", &u) == BCFG_ERR_TYPE);
    assert(bcfg_query_string(doc, "server.port", &s, &slen) == BCFG_ERR_TYPE);

    bcfg_free(doc);
    puts("PASS test_query_typed");
}

static void test_query_array_index(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    /* in-range index resolves to the array value itself */
    const bcfg_value *v = bcfg_query(doc, "data.ids[2]");
    assert(v != NULL);
    assert(v->type == BCFG_TYPE_ARRAY);
    assert(v->v.array.count == 3);

    /* out-of-range index -> NULL */
    assert(bcfg_query(doc, "data.ids[3]") == NULL);

    bcfg_free(doc);
    puts("PASS test_query_array_index");
}

static void test_query_missing(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    /* missing section / key -> NULL and NOTFOUND */
    assert(bcfg_query(doc, "missing.key") == NULL);
    assert(bcfg_query(doc, "server.missing") == NULL);

    /* malformed paths -> NULL */
    assert(bcfg_query(doc, "noseparator") == NULL);
    assert(bcfg_query(doc, ".key") == NULL);
    assert(bcfg_query(doc, "server.") == NULL);

    uint32_t u = 123;
    assert(bcfg_query_uint32(doc, "server.missing", &u) == BCFG_ERR_NOTFOUND);
    assert(u == 123);   /* untouched on failure */

    const char *s = (const char *)0x1;
    uint16_t slen = 7;
    assert(bcfg_query_string(doc, "missing.key", &s, &slen) ==
           BCFG_ERR_NOTFOUND);

    bcfg_free(doc);
    puts("PASS test_query_missing");
}

int main(void)
{
    test_query_scalar();
    test_query_typed();
    test_query_array_index();
    test_query_missing();
    puts("All tests passed.");
    return 0;
}
