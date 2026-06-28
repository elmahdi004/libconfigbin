/*
 * diag.c -- diagnostics and error-context tracking
 *
 * A bcfg_diag owns a growable array of bcfg_diag_item records.  Producers
 * (parsers, validators, tools) append items with bcfg_diag_add(); consumers
 * walk them with bcfg_diag_count()/bcfg_diag_at().  The collection starts
 * empty and allocates lazily on the first add.
 */

#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initial slot count for the backing array, and the growth factor.  Small
   enough to be cheap for the common "a handful of diagnostics" case. */
#define BCFG_DIAG_INITIAL_CAP 8

/* ------------------------------------------------------------------ */
/* lifecycle                                                            */
/* ------------------------------------------------------------------ */

void bcfg_diag_init(bcfg_diag *d)
{
    if (!d)
        return;
    d->items = NULL;
    d->count = 0;
    d->cap   = 0;
}

/*
 * Ensure room for at least one more item, growing the backing array if
 * needed.  Returns BCFG_OK on success or BCFG_ERR_NOMEM on failure; on
 * failure the existing array is left untouched.
 */
static int bcfg_diag_reserve_one(bcfg_diag *d)
{
    if (d->count < d->cap)
        return BCFG_OK;

    size_t new_cap = (d->cap == 0) ? BCFG_DIAG_INITIAL_CAP : d->cap * 2;

    /* Guard against multiplicative overflow when computing the byte size.
       In practice this never trips, but corrupt callers should not be able
       to wrap the allocation size. */
    if (new_cap < d->cap || new_cap > SIZE_MAX / sizeof(bcfg_diag_item))
        return BCFG_ERR_NOMEM;

    bcfg_diag_item *grown = realloc(d->items, new_cap * sizeof(bcfg_diag_item));
    if (!grown)
        return BCFG_ERR_NOMEM;

    d->items = grown;
    d->cap   = new_cap;
    return BCFG_OK;
}

int bcfg_diag_add(bcfg_diag *d, int severity, int code, size_t offset,
                  const char *fmt, ...)
{
    bcfg_diag_item *item;
    va_list ap;
    int rc;

    if (!d)
        return BCFG_ERR_INVALID;

    rc = bcfg_diag_reserve_one(d);
    if (rc != BCFG_OK)
        return rc;

    item = &d->items[d->count];

    /* Normalise the severity to a known level; anything out of range is
       treated as an error so unexpected callers fail loud, not silent. */
    if (severity != BCFG_SEV_INFO &&
        severity != BCFG_SEV_WARN &&
        severity != BCFG_SEV_ERROR)
        severity = BCFG_SEV_ERROR;

    item->code     = code;
    item->offset   = offset;
    item->severity = severity;

    /* Render the message.  vsnprintf always NUL-terminates when the buffer
       size is non-zero and truncates anything past BCFG_DIAG_MSGLEN-1. */
    if (fmt) {
        va_start(ap, fmt);
        int n = vsnprintf(item->message, BCFG_DIAG_MSGLEN, fmt, ap);
        va_end(ap);
        /* A negative return means an output/encoding error; fall back to an
           empty (but valid) message rather than leaving garbage behind. */
        if (n < 0)
            item->message[0] = '\0';
    } else {
        item->message[0] = '\0';
    }

    d->count++;
    return BCFG_OK;
}

/* ------------------------------------------------------------------ */
/* accessors                                                            */
/* ------------------------------------------------------------------ */

size_t bcfg_diag_count(const bcfg_diag *d)
{
    return d ? d->count : 0;
}

const bcfg_diag_item *bcfg_diag_at(const bcfg_diag *d, size_t i)
{
    if (!d || i >= d->count)
        return NULL;
    return &d->items[i];
}

void bcfg_diag_clear(bcfg_diag *d)
{
    if (!d)
        return;
    /* Keep the backing capacity so the collection can be refilled without
       re-allocating; just forget the contents. */
    d->count = 0;
}

void bcfg_diag_free(bcfg_diag *d)
{
    if (!d)
        return;
    free(d->items);
    d->items = NULL;
    d->count = 0;
    d->cap   = 0;
}

/* ------------------------------------------------------------------ */
/* error-code strings                                                   */
/* ------------------------------------------------------------------ */

/*
 * Map a BCFG_OK / BCFG_ERR_* status code to a static, human-readable
 * string.  Every code defined in configbin.h is covered; anything else
 * yields a generic fallback.  The returned pointer is to static storage
 * and must not be freed or modified.
 */
const char *bcfg_strerror(int code)
{
    switch (code) {
    case BCFG_OK:            return "success";
    case BCFG_ERR_MAGIC:     return "bad magic number";
    case BCFG_ERR_VERSION:   return "unsupported format version";
    case BCFG_ERR_TRUNCATED: return "truncated or incomplete data";
    case BCFG_ERR_NOMEM:     return "out of memory";
    case BCFG_ERR_INVALID:   return "invalid or malformed data";
    case BCFG_ERR_NOTFOUND:  return "section or entry not found";
    case BCFG_ERR_TYPE:      return "value type mismatch";
    case BCFG_ERR_RANGE:     return "value out of range";
    default:                 return "unknown error";
    }
}
