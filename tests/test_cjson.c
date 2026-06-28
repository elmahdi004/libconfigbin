/*
 * test_cjson.c -- JSON bridge tests
 *
 * Parse a JSON document into a bcfg_doc, assert the subset mapping produced
 * the right types and values, then write it back to JSON and re-parse,
 * confirming the supported types round-trip.  Also checks that a nested object
 * used as an entry value is rejected per the mapping rules.
 */

#include "../src/cjson.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static const char *SRC =
    "{"
    "\"net\":{"
        "\"port\":8080,"
        "\"host\":\"localhost\","
        "\"on\":true,"
        "\"off\":false,"
        "\"nothing\":null,"
        "\"ratio\":3.5,"
        "\"ids\":[1,2,3]"
    "},"
    "\"meta\":{\"name\":\"libconfigbin\"}"
    "}";

static void check_doc(const bcfg_doc *d)
{
    assert(d->num_sections == 2);
    bcfg_section *net = bcfg_get_section(d, "net");
    assert(net != NULL);

    bcfg_entry *port = bcfg_get_entry(net, "port");
    assert(port && port->value.type == BCFG_TYPE_UINT32 && port->value.v.u32 == 8080);

    bcfg_entry *host = bcfg_get_entry(net, "host");
    assert(host && host->value.type == BCFG_TYPE_STRING);
    assert(strcmp(host->value.v.str.data, "localhost") == 0);

    bcfg_entry *on = bcfg_get_entry(net, "on");
    assert(on && on->value.type == BCFG_TYPE_BOOL && on->value.v.boolean == 1);

    bcfg_entry *off = bcfg_get_entry(net, "off");
    assert(off && off->value.type == BCFG_TYPE_BOOL && off->value.v.boolean == 0);

    bcfg_entry *no = bcfg_get_entry(net, "nothing");
    assert(no && no->value.type == BCFG_TYPE_NULL);

    bcfg_entry *ra = bcfg_get_entry(net, "ratio");
    assert(ra && ra->value.type == BCFG_TYPE_FLOAT64 && ra->value.v.f64 == 3.5);

    bcfg_entry *ids = bcfg_get_entry(net, "ids");
    assert(ids && ids->value.type == BCFG_TYPE_ARRAY);
    assert(ids->value.v.array.count == 3);
    uint32_t *a = (uint32_t *)ids->value.v.array.data;
    assert(a[0] == 1 && a[1] == 2 && a[2] == 3);
}

static void test_parse(void)
{
    bcfg_diag diag;
    bcfg_diag_init(&diag);
    bcfg_doc *d = NULL;
    int rc = bcfg_json_parse(SRC, strlen(SRC), &d, &diag);
    assert(rc == BCFG_OK);
    check_doc(d);
    bcfg_free(d);
    bcfg_diag_free(&diag);
    puts("PASS test_parse");
}

static void test_roundtrip(void)
{
    bcfg_doc *d = NULL;
    assert(bcfg_json_parse(SRC, strlen(SRC), &d, NULL) == BCFG_OK);

    bcfg_buf out;
    bcfg_buf_init(&out);
    assert(bcfg_json_write(d, &out, 0) == BCFG_OK);
    assert(out.len > 0);

    bcfg_doc *d2 = NULL;
    assert(bcfg_json_parse((const char *)out.data, out.len, &d2, NULL) == BCFG_OK);
    check_doc(d2);

    bcfg_buf_free(&out);
    bcfg_free(d);
    bcfg_free(d2);
    puts("PASS test_roundtrip");
}

static void test_reject_nested(void)
{
    const char *bad = "{\"s\":{\"k\":{\"deep\":1}}}";
    bcfg_doc *d = NULL;
    int rc = bcfg_json_parse(bad, strlen(bad), &d, NULL);
    assert(rc != BCFG_OK);
    assert(d == NULL);
    puts("PASS test_reject_nested");
}

int main(void)
{
    test_parse();
    test_roundtrip();
    test_reject_nested();
    puts("All tests passed.");
    return 0;
}
