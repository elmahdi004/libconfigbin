/*
 * test_writer.c -- encoder round-trip tests
 *
 * Hand-build wire bytes, bcfg_parse them, re-encode with bcfg_write_doc into a
 * bcfg_buf, parse the re-encoded bytes, and assert the two documents are
 * structurally equal: section/entry counts plus a uint32, a string, and an
 * array (count + elements) survive the round trip intact.
 */

#include "../src/configbin.h"
#include "../src/writer.h"
#include "../src/buffer.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

/* helper: write little-endian values (same pattern as test_basic.c) */
static void w8(uint8_t **p, uint8_t v)  { **p = v; (*p)++; }
static void w16(uint8_t **p, uint16_t v){ memcpy(*p, &v, 2); *p+=2; }
static void w32(uint8_t **p, uint32_t v){ memcpy(*p, &v, 4); *p+=4; }
static void wn(uint8_t **p, const void *s, size_t n) { memcpy(*p,s,n); *p+=n; }

/* Build a doc with one section "cfg" holding a uint32, a string, and a
   uint32 array, returning the byte length written into buf. */
static size_t build_doc(uint8_t *buf)
{
    uint8_t *p = buf;

    w32(&p, BCFG_MAGIC);
    w16(&p, 1);   /* version */
    w16(&p, 1);   /* 1 section */

    w8(&p, 3); wn(&p, "cfg", 3);
    w32(&p, 3); /* 3 entries */

    /* port = uint32 8080 */
    w8(&p, 4); wn(&p, "port", 4);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 8080);

    /* host = string "localhost" */
    w8(&p, 4); wn(&p, "host", 4);
    w8(&p, BCFG_TYPE_STRING);
    w16(&p, 9); wn(&p, "localhost", 9);

    /* ids = array<uint32>[3] {11, 22, 33} */
    w8(&p, 3); wn(&p, "ids", 3);
    w8(&p, BCFG_TYPE_ARRAY);
    w8(&p, BCFG_TYPE_UINT32); /* elem_type */
    w16(&p, 3);               /* count */
    w32(&p, 11); w32(&p, 22); w32(&p, 33);

    return (size_t)(p - buf);
}

/* Assert the structural invariants we care about hold for `doc`. */
static void check_doc(const bcfg_doc *doc)
{
    assert(doc != NULL);
    assert(doc->num_sections == 1);

    bcfg_section *s = bcfg_get_section(doc, "cfg");
    assert(s != NULL);
    assert(s->num_entries == 3);

    bcfg_entry *port = bcfg_get_entry(s, "port");
    assert(port != NULL);
    assert(port->value.type == BCFG_TYPE_UINT32);
    assert(port->value.v.u32 == 8080);

    bcfg_entry *host = bcfg_get_entry(s, "host");
    assert(host != NULL);
    assert(host->value.type == BCFG_TYPE_STRING);
    assert(host->value.v.str.len == 9);
    assert(strcmp(host->value.v.str.data, "localhost") == 0);

    bcfg_entry *ids = bcfg_get_entry(s, "ids");
    assert(ids != NULL);
    assert(ids->value.type == BCFG_TYPE_ARRAY);
    assert(ids->value.v.array.elem_type == BCFG_TYPE_UINT32);
    assert(ids->value.v.array.count == 3);
    uint32_t *av = (uint32_t *)ids->value.v.array.data;
    assert(av[0] == 11);
    assert(av[1] == 22);
    assert(av[2] == 33);
}

static void test_roundtrip(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);

    /* parse the original bytes */
    bcfg_doc *orig = NULL;
    int rc = bcfg_parse(buf, total, &orig);
    assert(rc == BCFG_OK);
    check_doc(orig);

    /* re-encode the parsed doc */
    bcfg_buf out;
    bcfg_buf_init(&out);
    rc = bcfg_write_doc(orig, &out);
    assert(rc == BCFG_OK);
    assert(out.data != NULL);
    assert(out.len > 0);

    /* parse the re-encoded bytes */
    bcfg_doc *again = NULL;
    rc = bcfg_parse(out.data, out.len, &again);
    assert(rc == BCFG_OK);
    check_doc(again);

    /* structural equality across the round trip */
    assert(orig->num_sections == again->num_sections);
    bcfg_section *so = bcfg_get_section(orig,  "cfg");
    bcfg_section *sa = bcfg_get_section(again, "cfg");
    assert(so->num_entries == sa->num_entries);

    bcfg_buf_free(&out);
    bcfg_free(again);
    bcfg_free(orig);
    puts("PASS test_roundtrip");
}

/* Re-encoding the freshly re-encoded bytes must be byte-identical: encoding
   is deterministic and stable. */
static void test_reencode_stable(void)
{
    uint8_t buf[256];
    size_t total = build_doc(buf);

    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(buf, total, &doc);
    assert(rc == BCFG_OK);

    bcfg_buf a, b;
    bcfg_buf_init(&a);
    bcfg_buf_init(&b);

    rc = bcfg_write_doc(doc, &a);
    assert(rc == BCFG_OK);

    /* parse a's bytes and write again into b */
    bcfg_doc *doc2 = NULL;
    rc = bcfg_parse(a.data, a.len, &doc2);
    assert(rc == BCFG_OK);
    rc = bcfg_write_doc(doc2, &b);
    assert(rc == BCFG_OK);

    assert(a.len == b.len);
    assert(memcmp(a.data, b.data, a.len) == 0);

    bcfg_buf_free(&a);
    bcfg_buf_free(&b);
    bcfg_free(doc2);
    bcfg_free(doc);
    puts("PASS test_reencode_stable");
}

int main(void)
{
    test_roundtrip();
    test_reencode_stable();
    puts("All tests passed.");
    return 0;
}
