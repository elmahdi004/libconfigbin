/*
 * overlay.c -- streaming overlay accumulator
 *
 * Each applied document is parsed, its populated sections' names are copied
 * into a shared name pool, and a lightweight record per section is kept so the
 * merged listing can be produced on demand.  Pooling the names keeps the
 * per-section records small and lets long streams accumulate without a copy
 * of every name living in its own allocation.
 */

#include "overlay.h"
#include <stdlib.h>
#include <string.h>

#define OVERLAY_POOL_INIT 64

/* shared backing store for accumulated section names */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} name_pool;

/* one accumulated section: name lives in the pool, plus a little metadata */
typedef struct {
    const char *name;
    size_t      name_len;
    uint32_t    entry_count;
} ov_rec;

struct bcfg_overlay {
    ov_rec   *secs;
    size_t    n;
    size_t    cap;
    name_pool pool;
};

bcfg_overlay *bcfg_overlay_new(void)
{
    return (bcfg_overlay *)calloc(1, sizeof(bcfg_overlay));
}

/* ensure the pool can take `need` more bytes, growing geometrically */
static int pool_reserve(name_pool *p, size_t need)
{
    if (p->cap - p->len >= need)
        return BCFG_OK;

    size_t ncap = p->cap ? p->cap : OVERLAY_POOL_INIT;
    while (ncap - p->len < need)
        ncap *= 2;

    char *nb = (char *)malloc(ncap);
    if (!nb)
        return BCFG_ERR_NOMEM;
    if (p->buf) {
        memcpy(nb, p->buf, p->len);
        free(p->buf);
    }
    p->buf = nb;
    p->cap = ncap;
    return BCFG_OK;
}

/* copy n bytes (plus a terminator) into the pool, returning their location */
static char *pool_intern(name_pool *p, const char *s, size_t n)
{
    if (pool_reserve(p, n + 1) != BCFG_OK)
        return NULL;
    char *at = p->buf + p->len;
    memcpy(at, s, n);
    at[n] = '\0';
    p->len += n + 1;
    return at;
}

static int secs_reserve(bcfg_overlay *ov)
{
    if (ov->n < ov->cap)
        return BCFG_OK;
    size_t  ncap = ov->cap ? ov->cap * 2 : 8;
    ov_rec *nr = (ov_rec *)realloc(ov->secs, ncap * sizeof(*nr));
    if (!nr)
        return BCFG_ERR_NOMEM;
    ov->secs = nr;
    ov->cap  = ncap;
    return BCFG_OK;
}

int bcfg_overlay_apply(bcfg_overlay *ov, const uint8_t *doc, size_t len)
{
    if (!ov)
        return BCFG_ERR_INVALID;

    bcfg_doc *d = NULL;
    int rc = bcfg_parse(doc, len, &d);
    if (rc != BCFG_OK || !d)
        return BCFG_OK;   /* unparseable frame: skip, keep streaming */

    for (uint16_t i = 0; i < d->num_sections; i++) {
        bcfg_section *s = &d->sections[i];

        /* only sections that actually carry settings are worth recording */
        if (s->num_entries == 0)
            continue;

        char *pooled = pool_intern(&ov->pool, s->name, s->name_len);
        if (!pooled) {
            bcfg_free(d);
            return BCFG_ERR_NOMEM;
        }
        if (secs_reserve(ov) != BCFG_OK) {
            bcfg_free(d);
            return BCFG_ERR_NOMEM;
        }
        ov->secs[ov->n].name        = pooled;
        ov->secs[ov->n].name_len    = s->name_len;
        ov->secs[ov->n].entry_count = s->num_entries;
        ov->n++;
    }

    bcfg_free(d);
    return BCFG_OK;
}

int bcfg_overlay_render(bcfg_overlay *ov, bcfg_buf *out)
{
    if (!ov || !out)
        return BCFG_ERR_INVALID;

    for (size_t i = 0; i < ov->n; i++) {
        int rc = bcfg_buf_put_bytes(out, ov->secs[i].name, ov->secs[i].name_len);
        if (rc != BCFG_OK)
            return rc;
        if ((rc = bcfg_buf_put_u8(out, '\n')) != BCFG_OK)
            return rc;
    }
    return BCFG_OK;
}

size_t bcfg_overlay_count(const bcfg_overlay *ov)
{
    return ov ? ov->n : 0;
}

void bcfg_overlay_free(bcfg_overlay *ov)
{
    if (!ov)
        return;
    free(ov->pool.buf);
    free(ov->secs);
    free(ov);
}
