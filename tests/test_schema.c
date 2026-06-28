/*
 * test_schema.c -- declarative schema validation tests
 *
 * Define a schema with required keys and a declared type, then validate both a
 * conforming document (0 violations) and a non-conforming one (a missing
 * required key plus a type mismatch -> >0 violations, recorded in the diag).
 */

#include "../src/configbin.h"
#include "../src/schema.h"
#include "../src/diag.h"
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

/* Conforming doc: [server] port=uint32 80, host=string "x" */
static size_t build_good(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 1);

    w8(&p, 6); wn(&p, "server", 6);
    w32(&p, 2);

    w8(&p, 4); wn(&p, "port", 4);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 80);

    w8(&p, 4); wn(&p, "host", 4);
    w8(&p, BCFG_TYPE_STRING);
    w16(&p, 1); wn(&p, "x", 1);

    return (size_t)(p - buf);
}

/* Non-conforming doc: [server] port=string "oops"  (wrong type, host missing) */
static size_t build_bad(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 1);

    w8(&p, 6); wn(&p, "server", 6);
    w32(&p, 1);

    w8(&p, 4); wn(&p, "port", 4);
    w8(&p, BCFG_TYPE_STRING);          /* schema wants UINT32 */
    w16(&p, 4); wn(&p, "oops", 4);

    return (size_t)(p - buf);
}

/* Build the schema both tests share. */
static bcfg_schema *make_schema(void)
{
    bcfg_schema *s = bcfg_schema_new();
    assert(s != NULL);
    /* port must be present and a UINT32; host must be present and a STRING */
    assert(bcfg_schema_require(s, "server", "port",
                               BCFG_TYPE_UINT32, 1) == BCFG_OK);
    assert(bcfg_schema_require(s, "server", "host",
                               BCFG_TYPE_STRING, 1) == BCFG_OK);
    return s;
}

static void test_conforming(void)
{
    uint8_t buf[256];
    size_t total = build_good(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    bcfg_schema *s = make_schema();

    bcfg_diag d;
    bcfg_diag_init(&d);

    int violations = bcfg_schema_validate(s, doc, &d);
    assert(violations == 0);
    assert(bcfg_diag_count(&d) == 0);

    bcfg_diag_free(&d);
    bcfg_schema_free(s);
    bcfg_free(doc);
    puts("PASS test_conforming");
}

static void test_non_conforming(void)
{
    uint8_t buf[256];
    size_t total = build_bad(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    bcfg_schema *s = make_schema();

    bcfg_diag d;
    bcfg_diag_init(&d);

    int violations = bcfg_schema_validate(s, doc, &d);
    /* port has the wrong type, and required host is absent: two violations */
    assert(violations == 2);
    assert(bcfg_diag_count(&d) == 2);

    /* the recorded codes are the documented ones */
    int saw_type = 0, saw_notfound = 0;
    for (size_t i = 0; i < bcfg_diag_count(&d); i++) {
        const bcfg_diag_item *it = bcfg_diag_at(&d, i);
        assert(it != NULL);
        assert(it->severity == BCFG_SEV_ERROR);
        assert(it->message[0] != '\0');
        if (it->code == BCFG_ERR_TYPE)     saw_type = 1;
        if (it->code == BCFG_ERR_NOTFOUND) saw_notfound = 1;
    }
    assert(saw_type);
    assert(saw_notfound);

    bcfg_diag_free(&d);
    bcfg_schema_free(s);
    bcfg_free(doc);
    puts("PASS test_non_conforming");
}

/* Validation must still count violations even with a NULL diag sink. */
static void test_null_diag(void)
{
    uint8_t buf[256];
    size_t total = build_bad(buf);
    bcfg_doc *doc = NULL;
    assert(bcfg_parse(buf, total, &doc) == BCFG_OK);

    bcfg_schema *s = make_schema();
    int violations = bcfg_schema_validate(s, doc, NULL);
    assert(violations == 2);

    bcfg_schema_free(s);
    bcfg_free(doc);
    puts("PASS test_null_diag");
}

int main(void)
{
    test_conforming();
    test_non_conforming();
    test_null_diag();
    puts("All tests passed.");
    return 0;
}
