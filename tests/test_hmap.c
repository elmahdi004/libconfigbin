/*
 * test_hmap.c -- open-addressing hash map tests
 *
 * Inserts enough keys to force several grows, verifies lookups and updates,
 * then deletes a subset (leaving tombstones) and confirms the live count,
 * surviving keys, and that deleted keys miss and can be reinserted.
 */

#include "../src/hmap.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static void key_for(int i, char *out)
{
    sprintf(out, "key_%d", i);
}

static void test_put_get(void)
{
    bcfg_hmap m;
    bcfg_hmap_init(&m);

    char k[32];
    for (int i = 0; i < 500; i++) {
        key_for(i, k);
        assert(bcfg_hmap_put(&m, k, strlen(k), i) == BCFG_OK);
    }
    assert(bcfg_hmap_count(&m) == 500);

    for (int i = 0; i < 500; i++) {
        key_for(i, k);
        int32_t v = -1;
        assert(bcfg_hmap_get(&m, k, strlen(k), &v) == BCFG_OK);
        assert(v == i);
    }

    /* update an existing key in place (no count change) */
    key_for(42, k);
    assert(bcfg_hmap_put(&m, k, strlen(k), 4242) == BCFG_OK);
    assert(bcfg_hmap_count(&m) == 500);
    int32_t v = 0;
    assert(bcfg_hmap_get(&m, k, strlen(k), &v) == BCFG_OK);
    assert(v == 4242);

    /* a missing key */
    assert(bcfg_hmap_get(&m, "nope", 4, NULL) == BCFG_ERR_NOTFOUND);

    bcfg_hmap_free(&m);
    puts("PASS test_put_get");
}

static void test_delete_tombstones(void)
{
    bcfg_hmap m;
    bcfg_hmap_init(&m);

    char k[32];
    for (int i = 0; i < 200; i++) {
        key_for(i, k);
        assert(bcfg_hmap_put(&m, k, strlen(k), i * 2) == BCFG_OK);
    }

    /* delete the even-indexed keys */
    for (int i = 0; i < 200; i += 2) {
        key_for(i, k);
        assert(bcfg_hmap_del(&m, k, strlen(k)) == BCFG_OK);
    }
    assert(bcfg_hmap_count(&m) == 100);

    for (int i = 0; i < 200; i++) {
        key_for(i, k);
        int32_t v = -1;
        int rc = bcfg_hmap_get(&m, k, strlen(k), &v);
        if (i % 2 == 0) assert(rc == BCFG_ERR_NOTFOUND);
        else { assert(rc == BCFG_OK); assert(v == i * 2); }
    }

    /* deleting an absent key fails; reinserting over a tombstone works */
    key_for(0, k);
    assert(bcfg_hmap_del(&m, k, strlen(k)) == BCFG_ERR_NOTFOUND);
    assert(bcfg_hmap_put(&m, k, strlen(k), 7) == BCFG_OK);
    assert(bcfg_hmap_count(&m) == 101);

    bcfg_hmap_free(&m);
    puts("PASS test_delete_tombstones");
}

int main(void)
{
    test_put_get();
    test_delete_tombstones();
    puts("All tests passed.");
    return 0;
}
