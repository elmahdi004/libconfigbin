/*
 * configbin.h -- compact binary configuration format (v1)
 *
 * Wire format overview:
 *   [4B magic][2B version][2B num_sections]
 *   sections × num_sections:
 *     [1B name_len][name...][4B num_entries]
 *     entries × num_entries:
 *       [1B key_len][key...][1B type][payload...]
 *
 * Value payloads by type:
 *   NULL(0x00): nothing
 *   UINT32(0x01): 4 bytes LE
 *   INT64(0x02): 8 bytes LE
 *   FLOAT64(0x03): 8 bytes IEEE-754 LE
 *   STRING(0x04): [2B len][data]
 *   BLOB(0x05): [4B len][data]
 *   ARRAY(0x06): [1B elem_type][2B count][count × elem_size bytes]
 *   SECREF(0x07): [2B section_index]
 *   BOOL(0x08): [1B] 0 or 1
 *   TIMESTAMP(0x09): [8B] unix nanoseconds LE
 */

#ifndef CONFIGBIN_H
#define CONFIGBIN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BCFG_MAGIC    0x47464342U   /* 'BCFG' little-endian */
#define BCFG_VERSION  1

/* value type tags */
#define BCFG_TYPE_NULL      0x00
#define BCFG_TYPE_UINT32    0x01
#define BCFG_TYPE_INT64     0x02
#define BCFG_TYPE_FLOAT64   0x03
#define BCFG_TYPE_STRING    0x04
#define BCFG_TYPE_BLOB      0x05
#define BCFG_TYPE_ARRAY     0x06
#define BCFG_TYPE_SECREF    0x07
#define BCFG_TYPE_BOOL      0x08
#define BCFG_TYPE_TIMESTAMP 0x09

/* error codes */
#define BCFG_OK            0
#define BCFG_ERR_MAGIC    -1
#define BCFG_ERR_VERSION  -2
#define BCFG_ERR_TRUNCATED -3
#define BCFG_ERR_NOMEM    -4
#define BCFG_ERR_INVALID  -5
#define BCFG_ERR_NOTFOUND -6
#define BCFG_ERR_TYPE     -7
#define BCFG_ERR_RANGE    -8

typedef struct bcfg_value   bcfg_value;
typedef struct bcfg_entry   bcfg_entry;
typedef struct bcfg_section bcfg_section;
typedef struct bcfg_doc     bcfg_doc;

struct bcfg_value {
    uint8_t type;
    union {
        uint32_t  u32;
        int64_t   i64;
        double    f64;
        int64_t   ts;
        uint8_t   boolean;
        uint16_t  secref;
        struct { uint16_t len; char    *data; } str;
        struct { uint32_t len; uint8_t *data; } blob;
        struct {
            uint8_t  elem_type;
            uint16_t count;
            void    *data;
        } array;
    } v;
};

struct bcfg_entry {
    char       *key;
    uint8_t     key_len;
    bcfg_value  value;
};

struct bcfg_section {
    char        *name;
    uint8_t      name_len;
    bcfg_entry  *entries;
    uint32_t     num_entries;
};

struct bcfg_doc {
    bcfg_section *sections;
    uint16_t      num_sections;
};

/*
 * Parse in-memory buffer into a doc.
 * Returns BCFG_OK on success; negative error code otherwise.
 * On success *out_doc is heap-allocated; call bcfg_free() when done.
 */
int bcfg_parse(const uint8_t *data, size_t len, bcfg_doc **out_doc);

/* Look up a section by name (exact, case-sensitive). NULL if not found. */
bcfg_section *bcfg_get_section(const bcfg_doc *doc, const char *name);

/* Look up an entry by key within a section. NULL if not found. */
bcfg_entry   *bcfg_get_entry(const bcfg_section *sec, const char *key);

/* Resolve a BCFG_TYPE_SECREF value to the pointed-at section. */
bcfg_section *bcfg_resolve_secref(const bcfg_doc *doc, const bcfg_value *val);

/* Free a document and all associated memory. */
void bcfg_free(bcfg_doc *doc);

/* Utility: human-readable type name (static string, do not free). */
const char *bcfg_type_name(uint8_t type);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_H */
