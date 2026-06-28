/*
 * test_czip.c -- compression round-trip tests
 *
 * Both codecs (RLE and LZSS) must reproduce their input exactly across a
 * compress/decompress cycle, over repetitive, mixed, and empty data.  Also
 * checks that a corrupt LZSS header is rejected rather than mis-decoded.
 */

#include "../src/czip.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static void roundtrip(const uint8_t *src, size_t n)
{
    bcfg_buf c, d;

    /* RLE */
    bcfg_buf_init(&c);
    bcfg_buf_init(&d);
    assert(bcfg_rle_compress(src, n, &c) == BCFG_OK);
    assert(bcfg_rle_decompress(c.data, c.len, &d) == BCFG_OK);
    assert(d.len == n);
    if (n) assert(memcmp(d.data, src, n) == 0);
    bcfg_buf_free(&c);
    bcfg_buf_free(&d);

    /* LZSS */
    bcfg_buf_init(&c);
    bcfg_buf_init(&d);
    assert(bcfg_lzss_compress(src, n, &c) == BCFG_OK);
    assert(bcfg_lzss_decompress(c.data, c.len, &d) == BCFG_OK);
    assert(d.len == n);
    if (n) assert(memcmp(d.data, src, n) == 0);
    bcfg_buf_free(&c);
    bcfg_buf_free(&d);
}

static void test_roundtrips(void)
{
    uint8_t rep[512];
    memset(rep, 'A', sizeof(rep));
    roundtrip(rep, sizeof(rep));

    const char *txt =
        "the quick brown fox jumps over the lazy dog; "
        "the quick brown fox jumps over the lazy dog again.";
    roundtrip((const uint8_t *)txt, strlen(txt));

    uint8_t mixed[300];
    for (size_t i = 0; i < sizeof(mixed); i++)
        mixed[i] = (uint8_t)((i * 7 + (i / 5)) & 0xff);
    roundtrip(mixed, sizeof(mixed));

    roundtrip((const uint8_t *)"", 0);

    puts("PASS test_roundtrips");
}

static void test_corrupt_rejected(void)
{
    const char *txt = "aaaaaaaaaabbbbbbbbbbccccccccccdddddddddd";
    bcfg_buf c, d;
    bcfg_buf_init(&c);
    assert(bcfg_lzss_compress((const uint8_t *)txt, strlen(txt), &c) == BCFG_OK);

    /* clobber the magic in the 16-byte header */
    c.data[0] ^= 0xff;
    bcfg_buf_init(&d);
    assert(bcfg_lzss_decompress(c.data, c.len, &d) == BCFG_ERR_INVALID);

    /* a stream shorter than the header is truncated */
    bcfg_buf e;
    bcfg_buf_init(&e);
    assert(bcfg_lzss_decompress(c.data, 4, &e) == BCFG_ERR_TRUNCATED);

    bcfg_buf_free(&c);
    bcfg_buf_free(&d);
    bcfg_buf_free(&e);
    puts("PASS test_corrupt_rejected");
}

int main(void)
{
    test_roundtrips();
    test_corrupt_rejected();
    puts("All tests passed.");
    return 0;
}
