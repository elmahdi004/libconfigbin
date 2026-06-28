/*
 * merge.c -- deep clone and overlay merge of documents
 *
 * Two public operations:
 *   bcfg_doc_clone  -- a fully independent deep copy of a document.
 *   bcfg_merge      -- layer an overlay document on top of a base.
 *
 * Both yield a freshly heap-allocated document that the caller frees with
 * bcfg_free().  The cloned/merged structures own all of their strings, blob
 * payloads and array element blocks, so the originals can be freed
 * independently.  bcfg_value_hash() is used to skip redundant work when an
 * overlay value is byte-for-byte identical to the base value it replaces.
 */

#include "merge.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * Wire/element size in bytes for the fixed-size element types that may
 * appear inside an array payload.  Returns 0 for variable-width or unknown
 * types (arrays of those are never produced by the parser).  This mirrors
 * the private helper in value.c.
 */
static size_t elem_fixed_size(uint8_t t)
{
    switch (t) {
    case BCFG_TYPE_BOOL:      return 1;
    case BCFG_TYPE_UINT32:    return 4;
    case BCFG_TYPE_INT64:     return 8;
    case BCFG_TYPE_FLOAT64:   return 8;
    case BCFG_TYPE_TIMESTAMP: return 8;
    default:                  return 0;
    }
}

/*
 * Duplicate exactly n bytes from src into a fresh buffer.  n may be 0, in
 * which case a 1-byte allocation is returned so the result is always a
 * valid, free-able, non-NULL pointer.  Returns NULL only on allocation
 * failure.
 */
static void *dup_bytes(const void *src, size_t n)
{
    void *buf = malloc(n ? n : 1);
    if (!buf)
        return NULL;
    if (n && src)
        memcpy(buf, src, n);
    return buf;
}

/*
 * Deep-copy a single value into *dst.  Scalars are copied by assignment;
 * string/blob/array payloads get their own heap buffers.  On failure *dst
 * is left as a NULL value (safe to free) and BCFG_ERR_NOMEM is returned.
 */
static int clone_value(const bcfg_value *src, bcfg_value *dst)
{
    /* Start from a byte-for-byte copy so scalar members and the type tag
       come across for free; then re-own any heap payload. */
    *dst = *src;

    switch (src->type) {
    case BCFG_TYPE_STRING: {
        char *s = (char *)malloc((size_t)src->v.str.len + 1);
        if (!s) { dst->type = BCFG_TYPE_NULL; return BCFG_ERR_NOMEM; }
        if (src->v.str.data)
            memcpy(s, src->v.str.data, src->v.str.len);
        s[src->v.str.len] = '\0';
        dst->v.str.len  = src->v.str.len;
        dst->v.str.data = s;
        break;
    }

    case BCFG_TYPE_BLOB: {
        uint8_t *b = (uint8_t *)dup_bytes(src->v.blob.data, src->v.blob.len);
        if (!b) { dst->type = BCFG_TYPE_NULL; return BCFG_ERR_NOMEM; }
        dst->v.blob.len  = src->v.blob.len;
        dst->v.blob.data = b;
        break;
    }

    case BCFG_TYPE_ARRAY: {
        size_t esz   = elem_fixed_size(src->v.array.elem_type);
        size_t bytes = (size_t)src->v.array.count * esz;
        void  *data  = dup_bytes(src->v.array.data, bytes);
        if (!data) { dst->type = BCFG_TYPE_NULL; return BCFG_ERR_NOMEM; }
        dst->v.array.elem_type = src->v.array.elem_type;
        dst->v.array.count     = src->v.array.count;
        dst->v.array.data      = data;
        break;
    }

    default:
        /* NULL / UINT32 / INT64 / FLOAT64 / SECREF / BOOL / TIMESTAMP:
           the struct copy above already captured everything. */
        break;
    }

    return BCFG_OK;
}

/* Free any heap payload owned by a value (mirrors bcfg_free_value). */
static void free_value(bcfg_value *v)
{
    switch (v->type) {
    case BCFG_TYPE_STRING: free(v->v.str.data);   break;
    case BCFG_TYPE_BLOB:   free(v->v.blob.data);  break;
    case BCFG_TYPE_ARRAY:  free(v->v.array.data); break;
    default: break;
    }
}

/*
 * Deep-copy a single entry into *dst, allocating an independent key string
 * and value payload.  On failure nothing is leaked and BCFG_ERR_NOMEM is
 * returned with *dst left zeroed.
 */
static int clone_entry(const bcfg_entry *src, bcfg_entry *dst)
{
    memset(dst, 0, sizeof(*dst));

    char *key = (char *)malloc((size_t)src->key_len + 1);
    if (!key)
        return BCFG_ERR_NOMEM;
    if (src->key)
        memcpy(key, src->key, src->key_len);
    key[src->key_len] = '\0';

    int rc = clone_value(&src->value, &dst->value);
    if (rc != BCFG_OK) {
        free(key);
        return rc;
    }

    dst->key     = key;
    dst->key_len = src->key_len;
    return BCFG_OK;
}

/* Free an entry's owned memory (key + value payload). */
static void free_entry(bcfg_entry *e)
{
    free(e->key);
    free_value(&e->value);
}

/*
 * Allocate and deep-copy num_entries entries from src into a fresh array
 * stored at *out_entries.  num_entries may be 0 (yields NULL).  On failure
 * everything allocated so far is released and BCFG_ERR_NOMEM is returned.
 */
static int clone_entries(const bcfg_entry *src, uint32_t num_entries,
                         bcfg_entry **out_entries)
{
    *out_entries = NULL;
    if (num_entries == 0)
        return BCFG_OK;

    bcfg_entry *arr = (bcfg_entry *)calloc(num_entries, sizeof(bcfg_entry));
    if (!arr)
        return BCFG_ERR_NOMEM;

    for (uint32_t i = 0; i < num_entries; i++) {
        int rc = clone_entry(&src[i], &arr[i]);
        if (rc != BCFG_OK) {
            for (uint32_t j = 0; j < i; j++)
                free_entry(&arr[j]);
            free(arr);
            return rc;
        }
    }

    *out_entries = arr;
    return BCFG_OK;
}

/*
 * Deep-copy a section (name + all entries) into *dst.  On failure nothing is
 * leaked and *dst is left zeroed.
 */
static int clone_section(const bcfg_section *src, bcfg_section *dst)
{
    memset(dst, 0, sizeof(*dst));

    char *name = (char *)malloc((size_t)src->name_len + 1);
    if (!name)
        return BCFG_ERR_NOMEM;
    if (src->name)
        memcpy(name, src->name, src->name_len);
    name[src->name_len] = '\0';

    bcfg_entry *entries = NULL;
    int rc = clone_entries(src->entries, src->num_entries, &entries);
    if (rc != BCFG_OK) {
        free(name);
        return rc;
    }

    dst->name        = name;
    dst->name_len    = src->name_len;
    dst->entries     = entries;
    dst->num_entries = src->num_entries;
    return BCFG_OK;
}

/* Free a section's owned memory (name + entries array). */
static void free_section(bcfg_section *s)
{
    free(s->name);
    if (s->entries) {
        for (uint32_t j = 0; j < s->num_entries; j++)
            free_entry(&s->entries[j]);
        free(s->entries);
    }
}

/* ------------------------------------------------------------------ */
/* public: bcfg_doc_clone                                               */
/* ------------------------------------------------------------------ */

bcfg_doc *bcfg_doc_clone(const bcfg_doc *doc)
{
    if (!doc)
        return NULL;

    bcfg_doc *out = (bcfg_doc *)calloc(1, sizeof(bcfg_doc));
    if (!out)
        return NULL;

    if (doc->num_sections == 0)
        return out;   /* empty doc: NULL sections, count 0 */

    out->sections = (bcfg_section *)calloc(doc->num_sections,
                                           sizeof(bcfg_section));
    if (!out->sections) {
        free(out);
        return NULL;
    }

    for (uint16_t i = 0; i < doc->num_sections; i++) {
        if (clone_section(&doc->sections[i], &out->sections[i]) != BCFG_OK) {
            /* free the sections cloned so far, then the shell */
            out->num_sections = i;
            bcfg_free(out);
            return NULL;
        }
        out->num_sections++;
    }

    return out;
}

/* ------------------------------------------------------------------ */
/* public: bcfg_merge                                                   */
/* ------------------------------------------------------------------ */

/* Locate an entry by key within a section's entry array, or NULL. */
static bcfg_entry *find_entry(bcfg_section *sec, const char *key,
                              uint8_t key_len)
{
    for (uint32_t i = 0; i < sec->num_entries; i++) {
        bcfg_entry *e = &sec->entries[i];
        if (e->key_len == key_len &&
            memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

/* Locate a section by name within a doc's section array, or NULL. */
static bcfg_section *find_section(bcfg_doc *doc, const char *name,
                                  uint8_t name_len)
{
    for (uint16_t i = 0; i < doc->num_sections; i++) {
        bcfg_section *s = &doc->sections[i];
        if (s->name_len == name_len &&
            memcmp(s->name, name, name_len) == 0)
            return s;
    }
    return NULL;
}

/*
 * Replace the value of an existing destination entry with a deep copy of
 * the overlay entry's value.  Skipped entirely when the two values hash
 * equal (i.e. are byte-for-byte identical), which avoids a needless
 * allocate-and-free round trip.
 */
static int override_entry_value(bcfg_entry *dst, const bcfg_entry *ov)
{
    if (bcfg_value_hash(&dst->value) == bcfg_value_hash(&ov->value))
        return BCFG_OK;   /* identical payload -- keep what we have */

    bcfg_value tmp;
    int rc = clone_value(&ov->value, &tmp);
    if (rc != BCFG_OK)
        return rc;

    free_value(&dst->value);
    dst->value = tmp;
    return BCFG_OK;
}

/* Append a deep copy of overlay entry ov to the destination section. */
static int append_entry(bcfg_section *dst, const bcfg_entry *ov)
{
    size_t n = dst->num_entries;
    bcfg_entry *grown = (bcfg_entry *)realloc(dst->entries,
                                              (n + 1) * sizeof(bcfg_entry));
    if (!grown)
        return BCFG_ERR_NOMEM;
    dst->entries = grown;

    int rc = clone_entry(ov, &dst->entries[n]);
    if (rc != BCFG_OK)
        return rc;   /* slot n left untouched; count not bumped */

    dst->num_entries = (uint32_t)(n + 1);
    return BCFG_OK;
}

/* Merge every overlay entry of ov_sec into the destination section. */
static int merge_section_entries(bcfg_section *dst, const bcfg_section *ov_sec)
{
    for (uint32_t i = 0; i < ov_sec->num_entries; i++) {
        const bcfg_entry *ov = &ov_sec->entries[i];
        bcfg_entry *existing = find_entry(dst, ov->key, ov->key_len);
        int rc = existing ? override_entry_value(existing, ov)
                          : append_entry(dst, ov);
        if (rc != BCFG_OK)
            return rc;
    }
    return BCFG_OK;
}

/* Append a deep copy of an overlay-only section to the destination doc. */
static int append_section(bcfg_doc *dst, const bcfg_section *ov_sec)
{
    size_t n = dst->num_sections;
    bcfg_section *grown = (bcfg_section *)realloc(dst->sections,
                                                  (n + 1) * sizeof(bcfg_section));
    if (!grown)
        return BCFG_ERR_NOMEM;
    dst->sections = grown;

    int rc = clone_section(ov_sec, &dst->sections[n]);
    if (rc != BCFG_OK)
        return rc;   /* slot n untouched; count not bumped */

    dst->num_sections = (uint16_t)(n + 1);
    return BCFG_OK;
}

int bcfg_merge(const bcfg_doc *base, const bcfg_doc *overlay, bcfg_doc **out)
{
    if (!base || !overlay || !out)
        return BCFG_ERR_INVALID;

    /* Start the result as a full clone of the base. */
    bcfg_doc *result = bcfg_doc_clone(base);
    if (!result)
        return BCFG_ERR_NOMEM;

    /* Layer in every overlay section. */
    for (uint16_t i = 0; i < overlay->num_sections; i++) {
        const bcfg_section *ov_sec = &overlay->sections[i];
        bcfg_section *dst = find_section(result, ov_sec->name,
                                         ov_sec->name_len);
        int rc = dst ? merge_section_entries(dst, ov_sec)
                     : append_section(result, ov_sec);
        if (rc != BCFG_OK) {
            bcfg_free(result);
            return rc;
        }
    }

    *out = result;
    return BCFG_OK;
}
