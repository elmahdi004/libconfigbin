/*
 * expr.c -- query engine over a parsed document
 *
 * A query is "<section>.<key>" with either side optionally the wildcard "*",
 * and the key optionally carrying an "[i]" array-element selector.  A bare
 * "key" (no dot) is treated as "*.key".  Evaluation walks the doc collecting
 * every (section, entry, array_index) triple that matches, growing a heap
 * array the caller frees with bcfg_expr_free().
 */

#include "expr.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *sec;        /* section pattern, or "*"            */
    size_t      sec_len;
    const char *key;        /* key pattern, or "*"                */
    size_t      key_len;
    int         has_index;  /* an [i] suffix was present          */
    long        index;      /* parsed index when has_index        */
} query_t;

typedef struct {
    bcfg_match *items;
    size_t      count;
    size_t      cap;
} match_vec;

static int mv_push(match_vec *mv, const bcfg_section *s,
                   const bcfg_entry *e, int idx)
{
    if (mv->count == mv->cap) {
        size_t ncap = mv->cap ? mv->cap * 2 : 8;
        bcfg_match *ni = (bcfg_match *)realloc(mv->items, ncap * sizeof(*ni));
        if (!ni) return BCFG_ERR_NOMEM;
        mv->items = ni;
        mv->cap   = ncap;
    }
    mv->items[mv->count].section     = s;
    mv->items[mv->count].entry       = e;
    mv->items[mv->count].array_index = idx;
    mv->count++;
    return BCFG_OK;
}

/* parse "<sec>.<key>[i]" into q; returns BCFG_OK or BCFG_ERR_INVALID */
static int parse_query(const char *query, query_t *q)
{
    size_t len = strlen(query);
    const char *dot;

    if (len == 0) return BCFG_ERR_INVALID;

    memset(q, 0, sizeof(*q));
    q->index = -1;

    dot = memchr(query, '.', len);
    if (dot) {
        q->sec     = query;
        q->sec_len = (size_t)(dot - query);
        q->key     = dot + 1;
        q->key_len = len - q->sec_len - 1;
    } else {
        q->sec     = "*";
        q->sec_len = 1;
        q->key     = query;
        q->key_len = len;
    }
    if (q->sec_len == 0 || q->key_len == 0)
        return BCFG_ERR_INVALID;

    /* optional trailing [digits] on the key */
    if (q->key_len >= 3 && q->key[q->key_len - 1] == ']') {
        const char *lb = memchr(q->key, '[', q->key_len);
        if (lb) {
            const char *d = lb + 1;
            long idx = 0;
            int  any = 0;
            while (d < q->key + q->key_len - 1) {
                if (*d < '0' || *d > '9') return BCFG_ERR_INVALID;
                idx = idx * 10 + (*d - '0');
                any = 1;
                d++;
            }
            if (!any) return BCFG_ERR_INVALID;
            q->has_index = 1;
            q->index     = idx;
            q->key_len   = (size_t)(lb - q->key);
            if (q->key_len == 0) return BCFG_ERR_INVALID;
        }
    }
    return BCFG_OK;
}

static int pat_match(const char *pat, size_t pat_len,
                     const char *name, size_t name_len)
{
    if (pat_len == 1 && pat[0] == '*')
        return 1;
    return pat_len == name_len && memcmp(pat, name, name_len) == 0;
}

int bcfg_expr_eval(const bcfg_doc *doc, const char *query,
                   bcfg_match **out, size_t *n_out)
{
    query_t   q;
    match_vec mv = {0};
    int       rc;

    if (!doc || !query || !out || !n_out)
        return BCFG_ERR_INVALID;

    rc = parse_query(query, &q);
    if (rc != BCFG_OK)
        return rc;

    for (uint16_t i = 0; i < doc->num_sections; i++) {
        const bcfg_section *s = &doc->sections[i];
        if (!pat_match(q.sec, q.sec_len, s->name, s->name_len))
            continue;

        for (uint32_t j = 0; j < s->num_entries; j++) {
            const bcfg_entry *e = &s->entries[j];
            if (!pat_match(q.key, q.key_len, e->key, e->key_len))
                continue;

            int idx = -1;
            if (q.has_index) {
                if (e->value.type != BCFG_TYPE_ARRAY ||
                    q.index < 0 ||
                    (uint32_t)q.index >= e->value.v.array.count)
                    continue;   /* index selects nothing here */
                idx = (int)q.index;
            }

            if (mv_push(&mv, s, e, idx) != BCFG_OK) {
                free(mv.items);
                return BCFG_ERR_NOMEM;
            }
        }
    }

    *out   = mv.items;   /* may be NULL when zero matches */
    *n_out = mv.count;
    return BCFG_OK;
}

void bcfg_expr_free(bcfg_match *matches)
{
    free(matches);
}
