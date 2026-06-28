/*
 * test_basic.c -- basic parser correctness tests
 */

#include "../src/configbin.h"
#include "../src/frame.h"
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

/* Build a minimal valid doc: magic + version + 0 sections */
static void test_empty_doc(void)
{
    uint8_t buf[8];
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC);
    w16(&p, 1);   /* version */
    w16(&p, 0);   /* num_sections */

    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(buf, sizeof(buf), &doc);
    assert(rc == BCFG_OK);
    assert(doc != NULL);
    assert(doc->num_sections == 0);
    bcfg_free(doc);
    puts("PASS test_empty_doc");
}

/* Bad magic → BCFG_ERR_MAGIC */
static void test_bad_magic(void)
{
    uint8_t buf[8] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(buf, sizeof(buf), &doc);
    assert(rc == BCFG_ERR_MAGIC);
    assert(doc == NULL);
    puts("PASS test_bad_magic");
}

/* Truncated header → BCFG_ERR_TRUNCATED */
static void test_truncated_header(void)
{
    uint8_t buf[3] = {0x42, 0x43, 0x47};
    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(buf, sizeof(buf), &doc);
    assert(rc == BCFG_ERR_TRUNCATED);
    puts("PASS test_truncated_header");
}

/* One section, two entries: uint32 and string */
static void test_section_entries(void)
{
    uint8_t buf[256];
    uint8_t *p = buf;

    w32(&p, BCFG_MAGIC);
    w16(&p, 1);   /* version */
    w16(&p, 1);   /* 1 section */

    /* section: name="cfg" */
    w8(&p, 3);  wn(&p, "cfg", 3);
    w32(&p, 2); /* 2 entries */

    /* entry: key="port" type=UINT32 value=8080 */
    w8(&p, 4); wn(&p, "port", 4);
    w8(&p, BCFG_TYPE_UINT32);
    w32(&p, 8080);

    /* entry: key="host" type=STRING value="localhost" */
    w8(&p, 4); wn(&p, "host", 4);
    w8(&p, BCFG_TYPE_STRING);
    w16(&p, 9); wn(&p, "localhost", 9);

    size_t total = (size_t)(p - buf);
    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(buf, total, &doc);
    assert(rc == BCFG_OK);
    assert(doc->num_sections == 1);

    bcfg_section *s = bcfg_get_section(doc, "cfg");
    assert(s != NULL);

    bcfg_entry *ep = bcfg_get_entry(s, "port");
    assert(ep != NULL);
    assert(ep->value.type == BCFG_TYPE_UINT32);
    assert(ep->value.v.u32 == 8080);

    bcfg_entry *eh = bcfg_get_entry(s, "host");
    assert(eh != NULL);
    assert(eh->value.type == BCFG_TYPE_STRING);
    assert(strcmp(eh->value.v.str.data, "localhost") == 0);

    bcfg_free(doc);
    puts("PASS test_section_entries");
}

/* ARRAY of UINT32s */
static void test_array_uint32(void)
{
    uint8_t buf[256];
    uint8_t *p = buf;

    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 1);

    w8(&p, 3); wn(&p, "arr", 3);
    w32(&p, 1);

    w8(&p, 1); wn(&p, "v", 1);
    w8(&p, BCFG_TYPE_ARRAY);
    w8(&p, BCFG_TYPE_UINT32);  /* elem_type */
    w16(&p, 3);                 /* count=3 */
    w32(&p, 10); w32(&p, 20); w32(&p, 30);

    size_t total = (size_t)(p - buf);
    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(buf, total, &doc);
    assert(rc == BCFG_OK);

    bcfg_section *s = bcfg_get_section(doc, "arr");
    bcfg_entry   *e = bcfg_get_entry(s, "v");
    assert(e->value.type == BCFG_TYPE_ARRAY);
    assert(e->value.v.array.count == 3);

    uint32_t *vals = (uint32_t *)e->value.v.array.data;
    assert(vals[0] == 10);
    assert(vals[1] == 20);
    assert(vals[2] == 30);

    bcfg_free(doc);
    puts("PASS test_array_uint32");
}

/* SECREF resolution */
static void test_secref(void)
{
    uint8_t buf[256];
    uint8_t *p = buf;

    w32(&p, BCFG_MAGIC);
    w16(&p, 1);
    w16(&p, 2);  /* 2 sections */

    /* section 0: "main" with 1 entry pointing at section 1 */
    w8(&p, 4); wn(&p, "main", 4);
    w32(&p, 1);
    w8(&p, 4); wn(&p, "link", 4);
    w8(&p, BCFG_TYPE_SECREF);
    w16(&p, 1);  /* -> section index 1 */

    /* section 1: "sub" with no entries */
    w8(&p, 3); wn(&p, "sub", 3);
    w32(&p, 0);

    size_t total = (size_t)(p - buf);
    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(buf, total, &doc);
    assert(rc == BCFG_OK);

    bcfg_section *main_s = bcfg_get_section(doc, "main");
    assert(main_s != NULL);
    bcfg_entry *link_e = bcfg_get_entry(main_s, "link");
    assert(link_e != NULL);

    bcfg_section *sub = bcfg_resolve_secref(doc, &link_e->value);
    assert(sub != NULL);
    assert(memcmp(sub->name, "sub", 3) == 0);

    bcfg_free(doc);
    puts("PASS test_secref");
}

/* Frame layer: single frame containing a minimal doc */
static void test_frame_basic(void)
{
    uint8_t doc_buf[8];
    uint8_t *dp = doc_buf;
    w32(&dp, BCFG_MAGIC);
    w16(&dp, 1);
    w16(&dp, 0);
    uint32_t doc_len = (uint32_t)(dp - doc_buf);

    uint8_t frame_buf[256];
    uint8_t *fp = frame_buf;
    w16(&fp, FRAME_SYNC);
    w8(&fp, FRAME_FLAG_LAST);
    w8(&fp, 0);  /* pad_len=0 */
    w32(&fp, doc_len);
    wn(&fp, doc_buf, doc_len);

    size_t total = (size_t)(fp - frame_buf);
    int count = bcfg_stream_decode(frame_buf, total, NULL, NULL);
    assert(count == 1);
    puts("PASS test_frame_basic");
}

int main(void)
{
    test_empty_doc();
    test_bad_magic();
    test_truncated_header();
    test_section_entries();
    test_array_uint32();
    test_secref();
    test_frame_basic();
    puts("All tests passed.");
    return 0;
}
