/*
 * test_overlay.c -- streaming overlay accumulator tests
 *
 * Apply a couple of small documents and confirm the rendered listing contains
 * each populated section's name and that empty sections are skipped.  The
 * inputs are kept small so they exercise the ordinary path.
 */

#include "../src/overlay.h"
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

/* one section `name` with `nentries` trivial null-valued entries */
static size_t build_doc(uint8_t *buf, const char *name, uint32_t nentries)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 1);
    uint8_t nl = (uint8_t)strlen(name);
    w8(&p, nl); wn(&p, name, nl);
    w32(&p, nentries);
    for (uint32_t i = 0; i < nentries; i++) {
        w8(&p, 1); wn(&p, "k", 1);          /* key "k" */
        w8(&p, BCFG_TYPE_NULL);             /* null value, no payload */
    }
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

static void test_basic_render(void)
{
    bcfg_overlay *ov = bcfg_overlay_new();
    assert(ov);

    uint8_t buf[128];
    size_t n;

    n = build_doc(buf, "alpha", 1);
    assert(bcfg_overlay_apply(ov, buf, n) == BCFG_OK);
    n = build_doc(buf, "beta", 2);
    assert(bcfg_overlay_apply(ov, buf, n) == BCFG_OK);

    assert(bcfg_overlay_count(ov) == 2);

    bcfg_buf out;
    bcfg_buf_init(&out);
    assert(bcfg_overlay_render(ov, &out) == BCFG_OK);
    assert(contains(&out, "alpha"));
    assert(contains(&out, "beta"));

    bcfg_buf_free(&out);
    bcfg_overlay_free(ov);
    puts("PASS test_basic_render");
}

static void test_empty_section_skipped(void)
{
    bcfg_overlay *ov = bcfg_overlay_new();
    assert(ov);

    uint8_t buf[128];
    size_t n = build_doc(buf, "empty", 0);   /* no entries -> not recorded */
    assert(bcfg_overlay_apply(ov, buf, n) == BCFG_OK);
    assert(bcfg_overlay_count(ov) == 0);

    bcfg_overlay_free(ov);
    puts("PASS test_empty_section_skipped");
}

int main(void)
{
    test_basic_render();
    test_empty_section_skipped();
    puts("All tests passed.");
    return 0;
}
