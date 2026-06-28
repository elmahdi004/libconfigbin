/*
 * configbin.c -- top-level parser: header, sections, entries
 */

#include "configbin.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* forward declarations from value.c */
int  bcfg_parse_value(const uint8_t **pp, const uint8_t *end, bcfg_value *out);
void bcfg_free_value(bcfg_value *v);

/* ------------------------------------------------------------------ */
/* internal helpers                                                     */
/* ------------------------------------------------------------------ */

static int read_u8(const uint8_t **pp, const uint8_t *end, uint8_t *out)
{
    if (*pp >= end) return BCFG_ERR_TRUNCATED;
    *out = **pp;
    (*pp)++;
    return BCFG_OK;
}

static int read_u16(const uint8_t **pp, const uint8_t *end, uint16_t *out)
{
    if ((size_t)(end - *pp) < 2) return BCFG_ERR_TRUNCATED;
    memcpy(out, *pp, 2);
    *pp += 2;
    return BCFG_OK;
}

static int read_u32(const uint8_t **pp, const uint8_t *end, uint32_t *out)
{
    if ((size_t)(end - *pp) < 4) return BCFG_ERR_TRUNCATED;
    memcpy(out, *pp, 4);
    *pp += 4;
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* entry parsing                                                        */
/* ------------------------------------------------------------------ */

static int parse_entry(const uint8_t **pp, const uint8_t *end,
                       bcfg_entry *out)
{
    const uint8_t *p = *pp;
    int rc;
    uint8_t key_len;

    rc = read_u8(&p, end, &key_len);
    if (rc) return rc;
    if (key_len == 0) return BCFG_ERR_INVALID;
    if ((size_t)(end - p) < key_len) return BCFG_ERR_TRUNCATED;

    char *key = (char *)malloc(key_len + 1);
    if (!key) return BCFG_ERR_NOMEM;
    memcpy(key, p, key_len);
    key[key_len] = '\0';
    p += key_len;

    out->key     = key;
    out->key_len = key_len;

    rc = bcfg_parse_value(&p, end, &out->value);
    if (rc) {
        free(key);
        return rc;
    }

    *pp = p;
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* section parsing                                                      */
/* ------------------------------------------------------------------ */

static int parse_section(const uint8_t **pp, const uint8_t *end,
                         bcfg_section *out)
{
    const uint8_t *p = *pp;
    int rc;

    uint8_t name_len;
    rc = read_u8(&p, end, &name_len);
    if (rc) return rc;
    if (name_len == 0) return BCFG_ERR_INVALID;
    if ((size_t)(end - p) < name_len) return BCFG_ERR_TRUNCATED;

    char *name = (char *)malloc(name_len + 1);
    if (!name) return BCFG_ERR_NOMEM;
    memcpy(name, p, name_len);
    name[name_len] = '\0';
    p += name_len;

    out->name     = name;
    out->name_len = name_len;

    uint32_t num_entries;
    rc = read_u32(&p, end, &num_entries);
    if (rc) { free(name); return rc; }

    /* cap to something sane to avoid huge allocs on corrupt data */
    if (num_entries > 65535) { free(name); return BCFG_ERR_INVALID; }

    out->entries     = NULL;
    out->num_entries = 0;

    if (num_entries == 0) {
        *pp = p;
        return BCFG_OK;
    }

    out->entries = (bcfg_entry *)calloc(num_entries, sizeof(bcfg_entry));
    if (!out->entries) { free(name); return BCFG_ERR_NOMEM; }

    for (uint32_t i = 0; i < num_entries; i++) {
        rc = parse_entry(&p, end, &out->entries[i]);
        if (rc != BCFG_OK) {
            /* free already-parsed entries */
            for (uint32_t j = 0; j < i; j++) {
                free(out->entries[j].key);
                bcfg_free_value(&out->entries[j].value);
            }
            free(out->entries);
            free(name);
            return rc;
        }
        out->num_entries++;
    }

    *pp = p;
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* public: bcfg_parse                                                   */
/* ------------------------------------------------------------------ */

int bcfg_parse(const uint8_t *data, size_t len, bcfg_doc **out_doc)
{
    if (!data || !out_doc) return BCFG_ERR_INVALID;

    const uint8_t *p   = data;
    const uint8_t *end = data + len;
    int rc;

    /* magic */
    if (len < 4) return BCFG_ERR_TRUNCATED;
    uint32_t magic;
    memcpy(&magic, p, 4); p += 4;
    if (magic != BCFG_MAGIC) return BCFG_ERR_MAGIC;

    /* version */
    uint16_t version;
    rc = read_u16(&p, end, &version);
    if (rc) return rc;
    if (version != BCFG_VERSION) return BCFG_ERR_VERSION;

    /* num_sections */
    uint16_t num_sections;
    rc = read_u16(&p, end, &num_sections);
    if (rc) return rc;

    bcfg_doc *doc = (bcfg_doc *)calloc(1, sizeof(bcfg_doc));
    if (!doc) return BCFG_ERR_NOMEM;

    if (num_sections == 0) {
        doc->sections     = NULL;
        doc->num_sections = 0;
        *out_doc = doc;
        return BCFG_OK;
    }

    doc->sections = (bcfg_section *)calloc(num_sections, sizeof(bcfg_section));
    if (!doc->sections) { free(doc); return BCFG_ERR_NOMEM; }

    for (uint16_t i = 0; i < num_sections; i++) {
        rc = parse_section(&p, end, &doc->sections[i]);
        if (rc != BCFG_OK) {
            doc->num_sections = i; /* free only parsed ones */
            bcfg_free(doc);
            return rc;
        }
        doc->num_sections++;
    }

    *out_doc = doc;
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* public: lookups                                                      */
/* ------------------------------------------------------------------ */

bcfg_section *bcfg_get_section(const bcfg_doc *doc, const char *name)
{
    if (!doc || !name) return NULL;
    size_t nlen = strlen(name);
    for (uint16_t i = 0; i < doc->num_sections; i++) {
        bcfg_section *s = &doc->sections[i];
        if (s->name_len == (uint8_t)nlen &&
            memcmp(s->name, name, nlen) == 0)
            return s;
    }
    return NULL;
}

bcfg_entry *bcfg_get_entry(const bcfg_section *sec, const char *key)
{
    if (!sec || !key) return NULL;
    size_t klen = strlen(key);
    for (uint32_t i = 0; i < sec->num_entries; i++) {
        bcfg_entry *e = &sec->entries[i];
        if (e->key_len == (uint8_t)klen &&
            memcmp(e->key, key, klen) == 0)
            return e;
    }
    return NULL;
}

bcfg_section *bcfg_resolve_secref(const bcfg_doc *doc, const bcfg_value *val)
{
    if (!doc || !val) return NULL;
    if (val->type != BCFG_TYPE_SECREF) return NULL;
    uint16_t idx = val->v.secref;
    if (idx >= doc->num_sections) return NULL;
    return &doc->sections[idx];
}

/* ------------------------------------------------------------------ */
/* public: bcfg_free                                                    */
/* ------------------------------------------------------------------ */

void bcfg_free(bcfg_doc *doc)
{
    if (!doc) return;
    for (uint16_t i = 0; i < doc->num_sections; i++) {
        bcfg_section *s = &doc->sections[i];
        free(s->name);
        if (s->entries) {
            for (uint32_t j = 0; j < s->num_entries; j++) {
                free(s->entries[j].key);
                bcfg_free_value(&s->entries[j].value);
            }
            free(s->entries);
        }
    }
    free(doc->sections);
    free(doc);
}
