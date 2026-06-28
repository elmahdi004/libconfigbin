/*
 * hmap.h -- open-addressing string -> int32 hash map
 *
 * Linear-probing hash table keyed by arbitrary byte strings (not necessarily
 * NUL-terminated) mapping to int32_t values.  The table size is always a power
 * of two; deletions leave tombstones; the table grows (rehashing live entries)
 * when the live+tombstone load factor reaches 0.7.  Keys are copied into the
 * map and owned by it.  Hashing uses FNV-1a from hash.h.
 */

#ifndef CONFIGBIN_HMAP_H
#define CONFIGBIN_HMAP_H

#include "configbin.h"
#include "hash.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* one slot in the probe table; state distinguishes empty/tombstone/live */
typedef struct {
    char    *key;     /* owned copy of the key bytes, or NULL when empty */
    size_t   key_len; /* key length in bytes                             */
    uint32_t hash;    /* cached FNV-1a of the key                        */
    int32_t  val;     /* stored value                                    */
    uint8_t  state;   /* 0 empty, 1 live, 2 tombstone                    */
} bcfg_hmap_slot;

/* open-addressing hash map; treat fields as opaque */
typedef struct {
    bcfg_hmap_slot *slots;   /* probe table, cap entries (power of two) */
    size_t          cap;     /* slot count, always a power of two       */
    size_t          live;    /* number of live entries                  */
    size_t          used;    /* live + tombstones (for load factor)     */
} bcfg_hmap;

/* Initialise an empty map (no allocation until the first put). */
void bcfg_hmap_init(bcfg_hmap *m);

/*
 * Insert or update key (key_len bytes) -> val.  The key is copied.
 * Grows and rehashes at load factor 0.7.  Returns BCFG_OK, or
 * BCFG_ERR_NOMEM if the table cannot grow.
 */
int bcfg_hmap_put(bcfg_hmap *m, const char *key, size_t key_len, int32_t val);

/*
 * Look up key (key_len bytes).  On a hit, store the value in *out (if non-NULL)
 * and return BCFG_OK; on a miss return BCFG_ERR_NOTFOUND.
 */
int bcfg_hmap_get(const bcfg_hmap *m, const char *key, size_t key_len,
                  int32_t *out);

/*
 * Delete key (key_len bytes), leaving a tombstone.
 * Returns BCFG_OK, or BCFG_ERR_NOTFOUND if the key was absent.
 */
int bcfg_hmap_del(bcfg_hmap *m, const char *key, size_t key_len);

/* Number of live entries currently held. */
size_t bcfg_hmap_count(const bcfg_hmap *m);

/* Release the table and all copied keys, resetting to the empty state. */
void bcfg_hmap_free(bcfg_hmap *m);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_HMAP_H */
