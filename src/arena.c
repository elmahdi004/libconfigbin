/*
 * arena.c -- bump allocator with chained blocks
 *
 * Each arena owns a singly-linked list of blocks.  The head of the list is
 * the "current" block we bump-allocate from; older, full blocks trail behind
 * it.  Allocations are aligned to BCFG_ARENA_ALIGN and never freed
 * individually -- the whole arena is reset or destroyed at once.
 */

#include "arena.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Default block size used when the caller passes 0 to bcfg_arena_new(). */
#define BCFG_ARENA_DEFAULT_BLOCK 4096u

/* Round v up to the next multiple of BCFG_ARENA_ALIGN.
 * Returns 0 if the rounded value would overflow size_t. */
static size_t align_up(size_t v)
{
    size_t a = BCFG_ARENA_ALIGN;
    if (v > SIZE_MAX - (a - 1))
        return 0;               /* would overflow */
    return (v + (a - 1)) & ~(a - 1);
}

/*
 * Allocate a fresh block with at least `want` usable bytes and push it to the
 * front of the arena's list (it becomes the new current block).  Returns the
 * new block, or NULL on allocation failure.
 */
static bcfg_arena_block *push_block(bcfg_arena *a, size_t want)
{
    bcfg_arena_block *blk;
    size_t cap;

    /* A block must hold at least the default size, but grow to fit an
     * oversized request when needed. */
    cap = a->block_size;
    if (want > cap)
        cap = want;

    /* Guard the payload allocation against overflow.  cap is already a
     * sane size here, but stay defensive. */
    if (cap == 0)
        cap = 1;

    blk = (bcfg_arena_block *)malloc(sizeof(*blk));
    if (!blk)
        return NULL;

    blk->payload = (uint8_t *)malloc(cap);
    if (!blk->payload) {
        free(blk);
        return NULL;
    }

    blk->next = a->head;
    blk->cap  = cap;
    blk->used = 0;
    a->head   = blk;
    return blk;
}

bcfg_arena *bcfg_arena_new(size_t block_size)
{
    bcfg_arena *a;

    if (block_size == 0)
        block_size = BCFG_ARENA_DEFAULT_BLOCK;

    /* Make sure the default block size is itself aligned so the very first
     * allocation lands on an aligned boundary. */
    block_size = align_up(block_size);
    if (block_size == 0)
        return NULL;            /* overflow on rounding */

    a = (bcfg_arena *)malloc(sizeof(*a));
    if (!a)
        return NULL;

    a->head       = NULL;
    a->block_size = block_size;
    a->total_used = 0;

    /* Eagerly allocate the first block so reset() always has one to keep. */
    if (!push_block(a, block_size)) {
        free(a);
        return NULL;
    }

    return a;
}

void *bcfg_arena_alloc(bcfg_arena *a, size_t n)
{
    bcfg_arena_block *blk;
    size_t need;
    void *out;

    if (!a)
        return NULL;

    /* A zero-byte request still yields a unique, aligned, usable pointer. */
    if (n == 0)
        n = 1;

    need = align_up(n);
    if (need == 0)
        return NULL;            /* rounding overflowed */

    blk = a->head;

    /* Fast path: the current block has room.  cap >= used always holds, so
     * (cap - used) cannot underflow. */
    if (!blk || need > blk->cap - blk->used) {
        /* Current block can't fit the request; grab a new one.  Oversized
         * requests get a dedicated block sized exactly to fit. */
        blk = push_block(a, need);
        if (!blk)
            return NULL;
    }

    out = blk->payload + blk->used;
    blk->used     += need;
    a->total_used += need;
    return out;
}

void *bcfg_arena_calloc(bcfg_arena *a, size_t count, size_t size)
{
    size_t total;
    void *p;

    if (!a)
        return NULL;

    /* Detect multiplication overflow before asking for the bytes. */
    if (count != 0 && size > SIZE_MAX / count)
        return NULL;
    total = count * size;

    p = bcfg_arena_alloc(a, total);
    if (!p)
        return NULL;

    /* alloc rounds up; only the requested span is guaranteed to the caller,
     * so zero exactly that span (or one byte for the zero-size case). */
    memset(p, 0, total ? total : 1);
    return p;
}

char *bcfg_arena_strdup(bcfg_arena *a, const char *s)
{
    size_t len;
    char *copy;

    if (!a || !s)
        return NULL;

    len = strlen(s);
    if (len == SIZE_MAX)
        return NULL;            /* no room for the terminator */

    copy = (char *)bcfg_arena_alloc(a, len + 1);
    if (!copy)
        return NULL;

    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

void bcfg_arena_reset(bcfg_arena *a)
{
    bcfg_arena_block *first, *blk, *next;

    if (!a || !a->head)
        return;

    /* Keep the first (oldest) block, which sits at the tail of the list.
     * Walk from the head freeing everything until we reach it. */
    first = a->head;
    while (first->next)
        first = first->next;

    blk = a->head;
    while (blk != first) {
        next = blk->next;
        free(blk->payload);
        free(blk);
        blk = next;
    }

    /* Retain the surviving block as the sole, empty current block. */
    first->next   = NULL;
    first->used   = 0;
    a->head       = first;
    a->total_used = 0;
}

void bcfg_arena_free(bcfg_arena *a)
{
    bcfg_arena_block *blk, *next;

    if (!a)
        return;

    blk = a->head;
    while (blk) {
        next = blk->next;
        free(blk->payload);
        free(blk);
        blk = next;
    }

    free(a);
}

size_t bcfg_arena_bytes_used(const bcfg_arena *a)
{
    if (!a)
        return 0;
    return a->total_used;
}
