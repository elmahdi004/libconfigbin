/*
 * test_ctext.c -- text frontend tests
 *
 * Parse a ".bct" document covering every scalar plus an array, assert the
 * decoded values, then render it back to text and re-parse, confirming the
 * type and value of each entry survives a parse/write/parse cycle.
 */

#include "../src/ctext.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static const char *SRC =
    "# a sample config\n"
    "[net]\n"
    "port = 8080\n"
    "host = \"localhost\"\n"
    "enabled = true\n"
    "ratio = 3.5\n"
    "big = 5000000000\n"      /* > u32 -> INT64 */
    "ids = [10, 20, 30]\n"
    "\n"
    "[meta]\n"
    "name = \"libconfigbin\"\n";

static void check_doc(const bcfg_doc *d)
{
    assert(d->num_sections == 2);

    bcfg_section *net = bcfg_get_section(d, "net");
    assert(net != NULL);

    bcfg_entry *port = bcfg_get_entry(net, "port");
    assert(port && port->value.type == BCFG_TYPE_UINT32 && port->value.v.u32 == 8080);

    bcfg_entry *host = bcfg_get_entry(net, "host");
    assert(host && host->value.type == BCFG_TYPE_STRING);
    assert(host->value.v.str.len == 9 && strcmp(host->value.v.str.data, "localhost") == 0);

    bcfg_entry *en = bcfg_get_entry(net, "enabled");
    assert(en && en->value.type == BCFG_TYPE_BOOL && en->value.v.boolean == 1);

    bcfg_entry *ra = bcfg_get_entry(net, "ratio");
    assert(ra && ra->value.type == BCFG_TYPE_FLOAT64 && ra->value.v.f64 == 3.5);

    bcfg_entry *big = bcfg_get_entry(net, "big");
    assert(big && big->value.type == BCFG_TYPE_INT64 && big->value.v.i64 == 5000000000LL);

    bcfg_entry *ids = bcfg_get_entry(net, "ids");
    assert(ids && ids->value.type == BCFG_TYPE_ARRAY);
    assert(ids->value.v.array.elem_type == BCFG_TYPE_UINT32);
    assert(ids->value.v.array.count == 3);
    uint32_t *a = (uint32_t *)ids->value.v.array.data;
    assert(a[0] == 10 && a[1] == 20 && a[2] == 30);

    bcfg_section *meta = bcfg_get_section(d, "meta");
    assert(meta != NULL);
    bcfg_entry *nm = bcfg_get_entry(meta, "name");
    assert(nm && nm->value.type == BCFG_TYPE_STRING);
    assert(strcmp(nm->value.v.str.data, "libconfigbin") == 0);
}

static void test_parse(void)
{
    bcfg_diag diag;
    bcfg_diag_init(&diag);
    bcfg_doc *d = NULL;
    int rc = bcfg_ctext_parse(SRC, strlen(SRC), &d, &diag);
    assert(rc == BCFG_OK);
    check_doc(d);
    bcfg_free(d);
    bcfg_diag_free(&diag);
    puts("PASS test_parse");
}

static void test_roundtrip(void)
{
    bcfg_doc *d = NULL;
    assert(bcfg_ctext_parse(SRC, strlen(SRC), &d, NULL) == BCFG_OK);

    bcfg_buf out;
    bcfg_buf_init(&out);
    assert(bcfg_ctext_write(d, &out) == BCFG_OK);
    assert(out.len > 0);

    bcfg_doc *d2 = NULL;
    assert(bcfg_ctext_parse((const char *)out.data, out.len, &d2, NULL) == BCFG_OK);
    check_doc(d2);

    bcfg_buf_free(&out);
    bcfg_free(d);
    bcfg_free(d2);
    puts("PASS test_roundtrip");
}

static void test_error(void)
{
    bcfg_diag diag;
    bcfg_diag_init(&diag);
    const char *bad = "port = 1\n";   /* key before any [section] */
    bcfg_doc *d = NULL;
    int rc = bcfg_ctext_parse(bad, strlen(bad), &d, &diag);
    assert(rc != BCFG_OK);
    assert(d == NULL);
    assert(bcfg_diag_count(&diag) > 0);
    bcfg_diag_free(&diag);
    puts("PASS test_error");
}

int main(void)
{
    test_parse();
    test_roundtrip();
    test_error();
    puts("All tests passed.");
    return 0;
}
