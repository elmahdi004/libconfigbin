/*
 * writer.h -- encoder: serialize a document back to the v1 wire format
 *
 * The inverse of the parser in configbin.c / value.c.  bcfg_write_doc emits
 *   [4B magic][2B version][2B num_sections]
 * followed by each section
 *   [1B name_len][name][4B num_entries]
 * and each entry
 *   [1B key_len][key][1B type][payload]
 * with array payloads written as [1B elem_type][2B count][count*elem_size].
 * All multi-byte fields are little-endian; output is appended to a bcfg_buf.
 */

#ifndef CONFIGBIN_WRITER_H
#define CONFIGBIN_WRITER_H

#include "configbin.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Encode a single value's type tag and payload into out.
 * Returns BCFG_OK, BCFG_ERR_NOMEM, or BCFG_ERR_INVALID (e.g. an array whose
 * element type has no fixed wire size).
 */
int bcfg_write_value(const bcfg_value *v, bcfg_buf *out);

/*
 * Encode a whole document (header, sections, entries) into out.
 * Returns BCFG_OK, BCFG_ERR_NOMEM, or BCFG_ERR_INVALID.  On error the
 * contents written to out so far are unspecified.
 */
int bcfg_write_doc(const bcfg_doc *doc, bcfg_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_WRITER_H */
