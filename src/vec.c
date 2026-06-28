/*
 * vec.c -- generic growable arrays
 *
 * Implements two small dynamic-array helpers declared in vec.h.  bcfg_vec
 * stores fixed-size elements by value (memcpy in/out); bcfg_pvec is a thin
 * specialisation for void* pointer lists.  Capacity grows geometrically
 * (doubling) on demand.  Allocation failures leave the vector unmodified and
 * are reported as -1; every entry point tolerates a NULL vector pointer.
 * Depends only on libc.
 */

#include "vec.h"

#include <stdlib.h>
#include <string.h>

/* Smallest capacity allocated on the first growth.  Small enough not to waste
   memory on tiny vectors, large enough to avoid a flurry of early reallocs. */
#define BCFG_VEC_MIN_CAP 8

/*
 * Grow *cap_io (measured in elements of `elem` bytes) so that it can hold at
 * least `need` elements, reallocating *data_io as required.  Doubles capacity,
 * guarding against both element-count and byte-count overflow.  On success
 * returns 0 with *data_io / *cap_io updated; on overflow or allocation failure
 * returns -1 and leaves the caller's storage untouched.
 */
static int vec_grow_to(void **data_io, size_t *cap_io, size_t need, size_t elem)
{
    size_t cap = *cap_io;

    /* Already big enough: nothing to do. */
    if (need <= cap)
        return 0;

    size_t new_cap = cap ? cap : BCFG_VEC_MIN_CAP;
    while (new_cap < need) {
        /* Detect doubling overflow; fall back to the exact size needed. */
        if (new_cap > ((size_t)-1) / 2) {
            new_cap = need;
            break;
        }
        new_cap *= 2;
    }

    /* Guard the element-count -> byte-count multiplication against overflow. */
    if (elem != 0 && new_cap > ((size_t)-1) / elem)
        return -1;

    size_t bytes = new_cap * elem;
    void  *p = realloc(*data_io, bytes);
    if (!p && bytes != 0)
        return -1;

    *data_io = p;
    *cap_io  = new_cap;
    return 0;
}

/* ---- bcfg_vec: fixed-size elements held by value ---------------------- */

void bcfg_vec_init(bcfg_vec *v, size_t elem_size)
{
    if (!v) return;
    v->data = NULL;
    v->len  = 0;
    v->cap  = 0;
    v->elem = elem_size;
}

int bcfg_vec_reserve(bcfg_vec *v, size_t n_more)
{
    if (!v) return -1;

    /* Spare capacity already covers the request. */
    if (n_more <= v->cap - v->len)
        return 0;

    /* Guard against size_t overflow when computing the required total. */
    if (n_more > (size_t)-1 - v->len)
        return -1;

    size_t need = v->len + n_more;
    return vec_grow_to(&v->data, &v->cap, need, v->elem);
}

int bcfg_vec_push(bcfg_vec *v, const void *elem_ptr)
{
    if (!v || !elem_ptr) return -1;

    if (bcfg_vec_reserve(v, 1) != 0)
        return -1;

    /* data is non-NULL here: reserve(1) guarantees cap >= 1 with elem>0, and
       a zero-sized element makes the copy a no-op below. */
    memcpy((unsigned char *)v->data + v->len * v->elem, elem_ptr, v->elem);
    v->len++;
    return 0;
}

void *bcfg_vec_at(const bcfg_vec *v, size_t i)
{
    if (!v || i >= v->len)
        return NULL;
    return (unsigned char *)v->data + i * v->elem;
}

int bcfg_vec_pop(bcfg_vec *v, void *out)
{
    if (!v || v->len == 0)
        return -1;

    v->len--;
    if (out)
        memcpy(out, (unsigned char *)v->data + v->len * v->elem, v->elem);
    return 0;
}

void bcfg_vec_clear(bcfg_vec *v)
{
    if (!v) return;
    v->len = 0;   /* keep data/cap so the storage can be reused */
}

void bcfg_vec_free(bcfg_vec *v)
{
    if (!v) return;
    free(v->data);
    v->data = NULL;
    v->len  = 0;
    v->cap  = 0;
    /* elem is left intact so the vector may be reused after free without a
       fresh init; this mirrors bcfg_buf_free keeping the struct usable. */
}

/* ---- bcfg_pvec: array of void* pointers ------------------------------ */

void bcfg_pvec_init(bcfg_pvec *v)
{
    if (!v) return;
    v->data = NULL;
    v->len  = 0;
    v->cap  = 0;
}

int bcfg_pvec_reserve(bcfg_pvec *v, size_t n_more)
{
    if (!v) return -1;

    if (n_more <= v->cap - v->len)
        return 0;

    if (n_more > (size_t)-1 - v->len)
        return -1;

    size_t need = v->len + n_more;

    /* Reuse the shared grower; the (void**) storage is cast through void* so a
       single helper serves both vector flavours. */
    void *data = v->data;
    if (vec_grow_to(&data, &v->cap, need, sizeof(void *)) != 0)
        return -1;

    v->data = (void **)data;
    return 0;
}

int bcfg_pvec_push(bcfg_pvec *v, void *ptr)
{
    if (!v) return -1;

    if (bcfg_pvec_reserve(v, 1) != 0)
        return -1;

    v->data[v->len++] = ptr;
    return 0;
}

void *bcfg_pvec_at(const bcfg_pvec *v, size_t i)
{
    if (!v || i >= v->len)
        return NULL;
    return v->data[i];
}

void *bcfg_pvec_pop(bcfg_pvec *v)
{
    if (!v || v->len == 0)
        return NULL;

    v->len--;
    return v->data[v->len];
}

void bcfg_pvec_clear(bcfg_pvec *v)
{
    if (!v) return;
    v->len = 0;   /* keep data/cap; pointees are the caller's responsibility */
}

void bcfg_pvec_free(bcfg_pvec *v)
{
    if (!v) return;
    free(v->data);   /* does not free the pointees */
    v->data = NULL;
    v->len  = 0;
    v->cap  = 0;
}
