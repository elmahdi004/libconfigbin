/*
 * test_vec.c -- growable vector tests
 *
 * Exercises the by-value bcfg_vec and the pointer bcfg_pvec: append ordering,
 * geometric growth across a reserve, indexed access bounds, and LIFO pop.
 */

#include "../src/vec.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

static void test_byte_vec(void)
{
    bcfg_vec v;
    bcfg_vec_init(&v, sizeof(int));

    for (int i = 0; i < 100; i++)
        assert(bcfg_vec_push(&v, &i) == 0);

    assert(v.len == 100);
    for (int i = 0; i < 100; i++)
        assert(*(int *)bcfg_vec_at(&v, (size_t)i) == i);

    assert(bcfg_vec_at(&v, 100) == NULL);   /* out of range */

    int popped = -1;
    for (int i = 99; i >= 0; i--) {
        assert(bcfg_vec_pop(&v, &popped) == 0);
        assert(popped == i);
    }
    assert(v.len == 0);
    assert(bcfg_vec_pop(&v, &popped) == -1); /* empty */

    bcfg_vec_free(&v);
    puts("PASS test_byte_vec");
}

static void test_reserve_and_clear(void)
{
    bcfg_vec v;
    bcfg_vec_init(&v, sizeof(double));
    assert(bcfg_vec_reserve(&v, 64) == 0);
    assert(v.cap >= 64);

    double d = 2.5;
    assert(bcfg_vec_push(&v, &d) == 0);
    assert(v.len == 1);

    bcfg_vec_clear(&v);
    assert(v.len == 0);
    assert(v.cap >= 64);   /* capacity retained */

    bcfg_vec_free(&v);
    puts("PASS test_reserve_and_clear");
}

static void test_pvec(void)
{
    bcfg_pvec pv;
    bcfg_pvec_init(&pv);

    int a = 1, b = 2, c = 3;
    assert(bcfg_pvec_push(&pv, &a) == 0);
    assert(bcfg_pvec_push(&pv, &b) == 0);
    assert(bcfg_pvec_push(&pv, &c) == 0);
    assert(pv.len == 3);

    assert(bcfg_pvec_at(&pv, 0) == &a);
    assert(bcfg_pvec_at(&pv, 2) == &c);
    assert(bcfg_pvec_at(&pv, 3) == NULL);

    assert(bcfg_pvec_pop(&pv) == &c);
    assert(bcfg_pvec_pop(&pv) == &b);
    assert(pv.len == 1);

    bcfg_pvec_clear(&pv);
    assert(pv.len == 0);
    assert(bcfg_pvec_pop(&pv) == NULL);

    bcfg_pvec_free(&pv);
    puts("PASS test_pvec");
}

int main(void)
{
    test_byte_vec();
    test_reserve_and_clear();
    test_pvec();
    puts("All tests passed.");
    return 0;
}
