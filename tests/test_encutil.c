/*
 * test_encutil.c -- encoding helper tests
 *
 * base64 and hex round-trips over assorted byte strings (including empty and
 * full-range binary), plus unsigned/zigzag varint round-trips across boundary
 * values, and rejection of malformed hex.
 */

#include "../src/encutil.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static void rt_base64(const uint8_t *src, size_t n)
{
    bcfg_buf enc, dec;
    bcfg_buf_init(&enc);
    bcfg_buf_init(&dec);

    int e = bcfg_base64_encode(src, n, &enc);
    assert(e >= 0);
    assert((size_t)e == bcfg_base64_enc_len(n));

    int d = bcfg_base64_decode((const char *)enc.data, enc.len, &dec);
    assert(d >= 0);
    assert((size_t)d == n);
    assert(dec.len == n);
    if (n) assert(memcmp(dec.data, src, n) == 0);

    bcfg_buf_free(&enc);
    bcfg_buf_free(&dec);
}

static void test_base64(void)
{
    rt_base64((const uint8_t *)"", 0);
    rt_base64((const uint8_t *)"f", 1);
    rt_base64((const uint8_t *)"fo", 2);
    rt_base64((const uint8_t *)"foo", 3);
    rt_base64((const uint8_t *)"hello, world", 12);

    uint8_t bin[256];
    for (int i = 0; i < 256; i++) bin[i] = (uint8_t)i;
    rt_base64(bin, sizeof(bin));

    puts("PASS test_base64");
}

static void test_hex(void)
{
    const uint8_t in[] = {0x00, 0x0f, 0xa5, 0xff, 0x10};
    char enc[16];
    int n = bcfg_hex_encode(in, sizeof(in), enc, sizeof(enc));
    assert(n == (int)(sizeof(in) * 2));
    assert(memcmp(enc, "000FA5FF10", 10) == 0);

    uint8_t back[8];
    int m = bcfg_hex_decode(enc, (size_t)n, back, sizeof(back));
    assert(m == (int)sizeof(in));
    assert(memcmp(back, in, sizeof(in)) == 0);

    /* lower-case accepted on decode */
    uint8_t b2[2];
    assert(bcfg_hex_decode("abcd", 4, b2, sizeof(b2)) == 2);
    assert(b2[0] == 0xab && b2[1] == 0xcd);

    /* malformed input rejected */
    assert(bcfg_hex_decode("abc", 3, back, sizeof(back)) == BCFG_ERR_INVALID);
    assert(bcfg_hex_decode("xy", 2, back, sizeof(back)) == BCFG_ERR_INVALID);

    puts("PASS test_hex");
}

static void rt_uvarint(uint64_t v)
{
    uint8_t buf[10];
    int n = bcfg_uvarint_encode(v, buf, sizeof(buf));
    assert(n > 0);
    uint64_t out = 0;
    int m = bcfg_uvarint_decode(buf, (size_t)n, &out);
    assert(m == n);
    assert(out == v);
}

static void rt_svarint(int64_t v)
{
    uint8_t buf[10];
    int n = bcfg_svarint_encode(v, buf, sizeof(buf));
    assert(n > 0);
    int64_t out = 0;
    int m = bcfg_svarint_decode(buf, (size_t)n, &out);
    assert(m == n);
    assert(out == v);
}

static void test_varints(void)
{
    uint64_t uvals[] = {0, 1, 127, 128, 300, 16384, 0xFFFFFFFFull, ~0ull};
    for (size_t i = 0; i < sizeof(uvals) / sizeof(uvals[0]); i++)
        rt_uvarint(uvals[i]);

    int64_t svals[] = {0, -1, 1, -64, 63, -100000, 100000,
                       (int64_t)0x7fffffffffffffffLL,
                       (int64_t)0x8000000000000000LL};
    for (size_t i = 0; i < sizeof(svals) / sizeof(svals[0]); i++)
        rt_svarint(svals[i]);

    /* truncated unsigned varint is detected */
    uint8_t trunc[] = {0x80, 0x80};
    uint64_t out;
    assert(bcfg_uvarint_decode(trunc, sizeof(trunc), &out) == BCFG_ERR_TRUNCATED);

    puts("PASS test_varints");
}

int main(void)
{
    test_base64();
    test_hex();
    test_varints();
    puts("All tests passed.");
    return 0;
}
