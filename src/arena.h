/*
 * arena.h -- bump allocator with chained blocks
 *
 * An arena hands out small allocations from large pre-acquired blocks and
 * frees them all at once.  Blocks are chained in a singly-linked list; when
 * the current block cannot satisfy a request a fresh block is allocated.
 * Requests larger than the configured block size get a dedicated block sized
 * exactly to fit.  Individual allocations are never freed on their own.
 */

#ifndef CONFIGBIN_ARENA_H
#define CONFIGBIN_ARENA_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* alignment guarantee for every allocation handed out */
#define BCFG_ARENA_ALIGN 8

/* one chained block of arena storage */
typedef struct bcfg_arena_block {
    struct bcfg_arena_block *next;
    size_t                   cap;     /* usable bytes in payload          */
    size_t                   used;    /* bytes already handed out         */
    uint8_t                 *payload; /* cap bytes of backing storage     */
} bcfg_arena_block;

/* arena handle; treat the fields as opaque */
typedef struct bcfg_arena {
    bcfg_arena_block *head;        /* current block (front of the list)    */
    size_t            block_size;  /* default size for new blocks          */
    size_t            total_used;  /* running sum of bytes handed out      */
} bcfg_arena;

/*
 * Create a new arena whose default block holds at least block_size bytes.
 * A block_size of 0 selects an implementation-defined default.
 * Returns NULL on allocation failure.
 */
bcfg_arena *bcfg_arena_new(size_t block_size);

/*
 * Allocate n bytes, aligned to BCFG_ARENA_ALIGN.  Carves from the current
 * block when it fits, otherwise allocates a new block (oversized requests
 * get their own exactly-sized block).  Returns NULL on allocation failure.
 */
void *bcfg_arena_alloc(bcfg_arena *a, size_t n);

/* Like bcfg_arena_alloc but the returned memory is zero-filled. */
void *bcfg_arena_calloc(bcfg_arena *a, size_t count, size_t size);

/* Copy a NUL-terminated string into the arena; returns NULL on failure. */
char *bcfg_arena_strdup(bcfg_arena *a, const char *s);

/*
 * Reset the arena for reuse: keep the first block (marking it empty) and
 * free every chained block after it.  Outstanding pointers are invalidated.
 */
void bcfg_arena_reset(bcfg_arena *a);

/* Free every block and the arena handle itself. */
void bcfg_arena_free(bcfg_arena *a);

/* Total bytes handed out since creation or the last reset. */
size_t bcfg_arena_bytes_used(const bcfg_arena *a);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_ARENA_H */
