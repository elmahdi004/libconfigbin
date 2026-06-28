/*
 * ctext.h -- public text frontend (".bct")
 *
 * A human-editable textual surface that maps 1:1 onto the binary bcfg_doc.
 * bcfg_ctext_parse() builds a heap doc (free with bcfg_free); bcfg_ctext_write()
 * renders a doc back to canonical text.
 *
 * Format (".bct"):
 *   # line comment            also ';' comments
 *   [section_name]            -> a section
 *   key = 123                 -> UINT32 if it fits in u32, else INT64
 *   key = -5                  -> INT64
 *   key = 3.14                -> FLOAT64
 *   key = "hello\n"           -> STRING (C escapes: \n \t \" \\ \xHH)
 *   key = true | false        -> BOOL
 *   key = null                -> NULL
 *   key = [1, 2, 3]           -> ARRAY, elem type inferred from the first
 *                                element (fixed-size elems only:
 *                                uint32/int64/float64/bool/timestamp)
 *   key = @3                  -> SECREF to section index 3
 *   key = b16"DEADBEEF"       -> BLOB from hex
 *   key = t1700000000000000000 -> TIMESTAMP (nanoseconds, 't' prefix)
 *
 * Whitespace around '=' is optional; keys and section names match
 * [A-Za-z0-9_.-]+ and are at most 255 bytes long.
 */

#ifndef CONFIGBIN_CTEXT_H
#define CONFIGBIN_CTEXT_H

#include "configbin.h"
#include "buffer.h"
#include "diag.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse len bytes of ".bct" text into a new document.
 * On success *out is heap-allocated (free with bcfg_free) and BCFG_OK is
 * returned.  On failure a negative BCFG_ERR_* code is returned and, if diag
 * is non-NULL, one or more diagnostics describing the problem are appended.
 */
int bcfg_ctext_parse(const char *text, size_t len, bcfg_doc **out,
                     bcfg_diag *diag);

/*
 * Render doc to canonical ".bct" text, appending it to out.
 * Returns BCFG_OK or a negative BCFG_ERR_* code (e.g. BCFG_ERR_NOMEM).
 */
int bcfg_ctext_write(const bcfg_doc *doc, bcfg_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_CTEXT_H */
