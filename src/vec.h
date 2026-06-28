/*
 * vec.h -- generic growable arrays
 *
 * Two small dynamic-array helpers used by the text/JSON frontends and the
 * query engine.  bcfg_vec stores elements of a fixed size by value (memcpy
 * in/out); bcfg_pvec is a thin specialisation for void* pointer lists.
 * Capacity grows geometrically (doubling).  Depends only on libc.
 */

#ifndef CONFIGBIN_VEC_H
#define CONFIGBIN_VEC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* growable array of fixed-size elements held by value */
typedef struct {
    void   *data;   /* backing storage, or NULL when empty        */
    size_t  len;    /* number of elements stored                  */
    size_t  cap;    /* capacity in elements                       */
    size_t  elem;   /* size in bytes of one element               */
} bcfg_vec;

/* Initialise an empty vector whose elements are elem_size bytes each. */
void bcfg_vec_init(bcfg_vec *v, size_t elem_size);

/*
 * Ensure space for at least n_more additional elements without reallocating.
 * Doubles capacity as required.  Returns 0 on success, -1 on allocation error.
 */
int bcfg_vec_reserve(bcfg_vec *v, size_t n_more);

/*
 * Append one element by copying elem bytes from elem_ptr onto the tail.
 * Returns 0 on success, -1 on allocation error.
 */
int bcfg_vec_push(bcfg_vec *v, const void *elem_ptr);

/* Borrowed pointer to element i (element-sized), or NULL if out of range. */
void *bcfg_vec_at(const bcfg_vec *v, size_t i);

/*
 * Remove the last element.  If out is non-NULL, copy it there first.
 * Returns 0 on success, -1 if the vector is empty.
 */
int bcfg_vec_pop(bcfg_vec *v, void *out);

/* Drop all elements but keep the allocated capacity for reuse. */
void bcfg_vec_clear(bcfg_vec *v);

/* Release the backing storage and reset to the empty state. */
void bcfg_vec_free(bcfg_vec *v);

/* growable array of void* pointers (ownership of pointees is the caller's) */
typedef struct {
    void  **data;   /* backing storage, or NULL when empty        */
    size_t  len;    /* number of pointers stored                  */
    size_t  cap;    /* capacity in pointers                       */
} bcfg_pvec;

/* Initialise an empty pointer vector. */
void bcfg_pvec_init(bcfg_pvec *v);

/* Ensure space for n_more more pointers.  Returns 0 or -1 on error. */
int bcfg_pvec_reserve(bcfg_pvec *v, size_t n_more);

/* Append a pointer.  Returns 0 on success, -1 on allocation error. */
int bcfg_pvec_push(bcfg_pvec *v, void *ptr);

/* Pointer stored at index i, or NULL if out of range. */
void *bcfg_pvec_at(const bcfg_pvec *v, size_t i);

/* Remove and return the last pointer, or NULL if empty. */
void *bcfg_pvec_pop(bcfg_pvec *v);

/* Drop all pointers but keep capacity (does not free pointees). */
void bcfg_pvec_clear(bcfg_pvec *v);

/* Release the backing storage (does not free pointees). */
void bcfg_pvec_free(bcfg_pvec *v);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_VEC_H */
