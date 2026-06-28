/*
 * hmap.c -- open-addressing string -> int32 hash map
 *
 * Linear-probing hash table keyed by arbitrary byte strings (not necessarily
 * NUL-terminated) mapping to int32_t values.  The table size is always a power
 * of two; deletions leave tombstones; the table grows (rehashing live entries)
 * when the live+tombstone load factor reaches 0.7.  Keys are copied into the
 * map and owned by it.  Hashing uses FNV-1a from hash.h.
 */

#include "hmap.h"

#include "configbin.h"
#include "hash.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* slot states (must match the values documented in hmap.h) */
#define SLOT_EMPTY     0
#define SLOT_LIVE      1
#define SLOT_TOMBSTONE 2

/* initial table size on first insertion; must be a power of two */
#define HMAP_INITIAL_CAP 8

/*
 * Grow when used (live + tombstones) reaches 0.7 of capacity.  Comparison is
 * done with integer arithmetic to avoid floating point: used * 10 >= cap * 7.
 */
#define HMAP_LOAD_NUM 7
#define HMAP_LOAD_DEN 10

/* ------------------------------------------------------------------ */
/* internal helpers                                                     */
/* ------------------------------------------------------------------ */

/* True if n is a power of two (and non-zero). */
static int is_pow2(size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

/*
 * Compare two keys for byte-for-byte equality.  Lengths are checked first so
 * the memcmp only runs on equal-length keys; zero-length keys compare equal.
 */
static int key_eq(const bcfg_hmap_slot *slot, uint32_t hash,
                  const char *key, size_t key_len)
{
    if (slot->hash != hash)
        return 0;
    if (slot->key_len != key_len)
        return 0;
    if (key_len == 0)
        return 1;
    return memcmp(slot->key, key, key_len) == 0;
}

/*
 * Allocate and copy a key.  A trailing NUL is appended for safety/debuggability
 * but is not considered part of the stored bytes.  Returns NULL on allocation
 * failure or when key is NULL with a non-zero length.
 */
static char *key_dup(const char *key, size_t key_len)
{
    char *copy = (char *)malloc(key_len + 1);
    if (!copy)
        return NULL;
    if (key_len != 0)
        memcpy(copy, key, key_len);
    copy[key_len] = '\0';
    return copy;
}

/*
 * Locate the slot for key within an already-allocated table.  On success
 * *out_index receives the slot index and the return value is 1 if the key is
 * present (live) or 0 if absent.  When absent, *out_index is the first slot
 * suitable for insertion (the earliest tombstone seen, else the empty slot
 * that terminates the probe).  Requires cap > 0 and cap a power of two.
 */
static int find_slot(const bcfg_hmap *m, uint32_t hash,
                     const char *key, size_t key_len, size_t *out_index)
{
    size_t mask = m->cap - 1;
    size_t idx = (size_t)hash & mask;
    size_t first_tomb = m->cap; /* sentinel: no tombstone seen yet */
    size_t i;

    for (i = 0; i < m->cap; i++) {
        const bcfg_hmap_slot *slot = &m->slots[idx];

        if (slot->state == SLOT_EMPTY) {
            /* miss: prefer an earlier tombstone for reuse if we found one */
            *out_index = (first_tomb != m->cap) ? first_tomb : idx;
            return 0;
        }
        if (slot->state == SLOT_TOMBSTONE) {
            if (first_tomb == m->cap)
                first_tomb = idx;
        } else if (key_eq(slot, hash, key, key_len)) {
            *out_index = idx;
            return 1;
        }

        idx = (idx + 1) & mask;
    }

    /*
     * Table fully scanned without an empty slot.  This should not happen while
     * the load factor is enforced below 1.0, but degrade gracefully: report a
     * miss into a tombstone if one exists.
     */
    *out_index = (first_tomb != m->cap) ? first_tomb : m->cap;
    return 0;
}

/*
 * Insert a key/value/hash triple into a table that is known to have room and
 * not to already contain the key.  Used during rehashing where keys are moved
 * (ownership transferred) rather than copied.  Returns the slot index.
 */
static void insert_known_absent(bcfg_hmap *m, char *key, size_t key_len,
                                uint32_t hash, int32_t val)
{
    size_t mask = m->cap - 1;
    size_t idx = (size_t)hash & mask;
    bcfg_hmap_slot *slot;

    while ((slot = &m->slots[idx])->state == SLOT_LIVE)
        idx = (idx + 1) & mask;

    /* slot is empty or a tombstone; either is reusable */
    slot->key = key;
    slot->key_len = key_len;
    slot->hash = hash;
    slot->val = val;
    slot->state = SLOT_LIVE;
}

/*
 * Grow (or initially allocate) the table to new_cap slots and rehash all live
 * entries, dropping tombstones.  new_cap must be a power of two and strictly
 * greater than the current live count.  Returns BCFG_OK or BCFG_ERR_NOMEM;
 * on failure the existing table is left untouched.
 */
static int rehash(bcfg_hmap *m, size_t new_cap)
{
    bcfg_hmap_slot *new_slots;
    bcfg_hmap_slot *old_slots = m->slots;
    size_t old_cap = m->cap;
    size_t i;

    if (!is_pow2(new_cap))
        return BCFG_ERR_INVALID;

    new_slots = (bcfg_hmap_slot *)calloc(new_cap, sizeof(*new_slots));
    if (!new_slots)
        return BCFG_ERR_NOMEM;

    /* point the map at the fresh, empty table before re-inserting */
    m->slots = new_slots;
    m->cap = new_cap;
    m->live = 0;
    m->used = 0;

    for (i = 0; i < old_cap; i++) {
        bcfg_hmap_slot *slot = &old_slots[i];
        if (slot->state != SLOT_LIVE)
            continue;
        insert_known_absent(m, slot->key, slot->key_len, slot->hash,
                            slot->val);
        m->live++;
        m->used++;
    }

    free(old_slots);
    return BCFG_OK;
}

/*
 * Ensure the table has spare capacity for one more insertion, growing if the
 * load factor would be reached.  Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
static int ensure_capacity(bcfg_hmap *m)
{
    size_t projected;

    if (m->cap == 0)
        return rehash(m, HMAP_INITIAL_CAP);

    /* would inserting one more reach the 0.7 load factor? */
    projected = m->used + 1;
    if (projected * HMAP_LOAD_DEN >= m->cap * HMAP_LOAD_NUM) {
        size_t new_cap = m->cap * 2;
        /* guard against size_t overflow on absurdly large tables */
        if (new_cap < m->cap)
            return BCFG_ERR_NOMEM;
        return rehash(m, new_cap);
    }
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* public API                                                           */
/* ------------------------------------------------------------------ */

void bcfg_hmap_init(bcfg_hmap *m)
{
    if (!m)
        return;
    m->slots = NULL;
    m->cap = 0;
    m->live = 0;
    m->used = 0;
}

int bcfg_hmap_put(bcfg_hmap *m, const char *key, size_t key_len, int32_t val)
{
    uint32_t hash;
    size_t idx;
    int found;
    int rc;

    if (!m || (key_len != 0 && key == NULL))
        return BCFG_ERR_INVALID;

    rc = ensure_capacity(m);
    if (rc != BCFG_OK)
        return rc;

    hash = bcfg_fnv1a(key, key_len);

    found = find_slot(m, hash, key, key_len, &idx);
    if (found) {
        /* update in place; key already owned by the map */
        m->slots[idx].val = val;
        return BCFG_OK;
    }

    if (idx >= m->cap)
        return BCFG_ERR_NOMEM; /* no usable slot (should not occur) */

    {
        bcfg_hmap_slot *slot = &m->slots[idx];
        char *copy = key_dup(key, key_len);
        if (!copy)
            return BCFG_ERR_NOMEM;

        /* reusing a tombstone does not increase the used count */
        if (slot->state == SLOT_EMPTY)
            m->used++;

        slot->key = copy;
        slot->key_len = key_len;
        slot->hash = hash;
        slot->val = val;
        slot->state = SLOT_LIVE;
        m->live++;
    }

    return BCFG_OK;
}

int bcfg_hmap_get(const bcfg_hmap *m, const char *key, size_t key_len,
                  int32_t *out)
{
    uint32_t hash;
    size_t idx;

    if (!m || (key_len != 0 && key == NULL))
        return BCFG_ERR_INVALID;
    if (m->cap == 0 || m->live == 0)
        return BCFG_ERR_NOTFOUND;

    hash = bcfg_fnv1a(key, key_len);

    if (find_slot(m, hash, key, key_len, &idx)) {
        if (out)
            *out = m->slots[idx].val;
        return BCFG_OK;
    }
    return BCFG_ERR_NOTFOUND;
}

int bcfg_hmap_del(bcfg_hmap *m, const char *key, size_t key_len)
{
    uint32_t hash;
    size_t idx;

    if (!m || (key_len != 0 && key == NULL))
        return BCFG_ERR_INVALID;
    if (m->cap == 0 || m->live == 0)
        return BCFG_ERR_NOTFOUND;

    hash = bcfg_fnv1a(key, key_len);

    if (!find_slot(m, hash, key, key_len, &idx))
        return BCFG_ERR_NOTFOUND;

    {
        bcfg_hmap_slot *slot = &m->slots[idx];
        free(slot->key);
        slot->key = NULL;
        slot->key_len = 0;
        slot->hash = 0;
        slot->val = 0;
        slot->state = SLOT_TOMBSTONE; /* keep `used` count: tombstone remains */
        m->live--;
    }

    return BCFG_OK;
}

size_t bcfg_hmap_count(const bcfg_hmap *m)
{
    return m ? m->live : 0;
}

void bcfg_hmap_free(bcfg_hmap *m)
{
    size_t i;

    if (!m)
        return;

    if (m->slots) {
        for (i = 0; i < m->cap; i++) {
            if (m->slots[i].state == SLOT_LIVE)
                free(m->slots[i].key);
        }
        free(m->slots);
    }

    m->slots = NULL;
    m->cap = 0;
    m->live = 0;
    m->used = 0;
}
