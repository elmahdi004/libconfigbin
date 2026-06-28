/*
 * dump.h -- human-readable rendering
 *
 * Pretty-printers for inspection and debugging.  bcfg_dump_text renders an
 * INI/TOML-ish view of a parsed document; bcfg_dump_hex produces a classic
 * offset/hex/ascii hexdump of arbitrary bytes; bcfg_value_to_string formats
 * a single scalar value into a caller-supplied buffer.  Text output is
 * appended to a bcfg_buf.
 */

#ifndef CONFIGBIN_DUMP_H
#define CONFIGBIN_DUMP_H

#include "configbin.h"
#include "buffer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Render doc as indented text: a "[section]" header per section followed by
 * "key = value" lines.  Arrays print as "[a, b, c]", strings are quoted, and
 * blobs render as hex truncated to a fixed length cap.  Output is appended
 * to out.  Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_dump_text(const bcfg_doc *doc, bcfg_buf *out);

/*
 * Append a classic hexdump of n bytes at data to out: an offset column, the
 * hex bytes (grouped 16 per line), and an ASCII gutter.
 * Returns BCFG_OK or BCFG_ERR_NOMEM.
 */
int bcfg_dump_hex(const uint8_t *data, size_t n, bcfg_buf *out);

/*
 * Format a single scalar value into buf (always NUL-terminated when buflen
 * is non-zero).  Returns the number of characters that the full rendering
 * would occupy, excluding the NUL (snprintf-style); output is truncated if
 * it does not fit.
 */
size_t bcfg_value_to_string(const bcfg_value *v, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_DUMP_H */
