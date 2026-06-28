/*
 * diag.h -- diagnostics and error-context tracking
 *
 * A bcfg_diag is a growable array of diagnostic items, each carrying a
 * severity, an error code (a BCFG_ERR_* value or 0), the wire offset the
 * problem was observed at, and a short formatted message.  Parsers,
 * validators and tools append items as they go; callers iterate them
 * afterwards to report what happened and where.
 */

#ifndef CONFIGBIN_DIAG_H
#define CONFIGBIN_DIAG_H

#include "configbin.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* severity levels */
#define BCFG_SEV_INFO   0
#define BCFG_SEV_WARN   1
#define BCFG_SEV_ERROR  2

/* maximum stored message length, including the NUL terminator */
#define BCFG_DIAG_MSGLEN 160

/* a single diagnostic record */
typedef struct {
    int    code;                       /* BCFG_ERR_* code, or 0 if none   */
    size_t offset;                     /* wire offset the issue relates to */
    int    severity;                   /* BCFG_SEV_*                       */
    char   message[BCFG_DIAG_MSGLEN];  /* NUL-terminated, possibly empty   */
} bcfg_diag_item;

/* growable diagnostic collection; treat fields as opaque */
typedef struct {
    bcfg_diag_item *items;
    size_t          count;
    size_t          cap;
} bcfg_diag;

/* Initialise an empty collection (no allocation until the first add). */
void bcfg_diag_init(bcfg_diag *d);

/*
 * Append one diagnostic.  The message is rendered with printf-style fmt
 * and varargs via vsnprintf and truncated to BCFG_DIAG_MSGLEN-1 chars.
 * Returns BCFG_OK, or BCFG_ERR_NOMEM if the backing array cannot grow.
 */
int bcfg_diag_add(bcfg_diag *d, int severity, int code, size_t offset,
                  const char *fmt, ...);

/* Number of diagnostics currently held. */
size_t bcfg_diag_count(const bcfg_diag *d);

/* Borrowed pointer to item i, or NULL if i is out of range. */
const bcfg_diag_item *bcfg_diag_at(const bcfg_diag *d, size_t i);

/* Drop all items but keep the backing capacity for reuse. */
void bcfg_diag_clear(bcfg_diag *d);

/* Release the backing array and reset to the empty state. */
void bcfg_diag_free(bcfg_diag *d);

/*
 * Map a BCFG_* status code to a static, human-readable string.
 * Every BCFG_OK / BCFG_ERR_* in configbin.h is covered; unknown codes
 * return a generic "unknown error".  The returned string is static.
 */
const char *bcfg_strerror(int code);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_DIAG_H */
