/*
 * path.c -- dotted-path query over a parsed document
 *
 * A path addresses a value as "section.key", with an optional trailing
 * "[index]" (e.g. "data.ids[2]").  We split on the first '.': everything
 * before it is the section name, everything after is the entry key.  A
 * trailing "[n]" on the key is parsed and, when present, range-checked
 * against an ARRAY value -- but the resolved value handed back is always
 * the entry's own value, never an individual element.
 */

#include "path.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*
 * Result of splitting a path into its parts.  `section` and `key` point
 * into the caller-supplied scratch buffer (a private copy of the path),
 * not into the original string.  `has_index` records whether a trailing
 * "[n]" was seen, and `index` carries its parsed value.
 */
typedef struct {
    const char *section;
    const char *key;
    int         has_index;
    uint32_t    index;
} bcfg_path_parts;

/*
 * Parse a decimal index out of "[<digits>]" found at the tail of `key`.
 *
 * `bracket` points at the '[' inside the writable scratch buffer; on
 * success the '[' is overwritten with '\0' so the key is truncated to the
 * portion before the bracket.  Returns 0 on success, -1 if the bracket
 * expression is malformed (no digits, overflow, junk, or missing ']').
 */
static int parse_index(char *bracket, uint32_t *out_index)
{
    const char *p = bracket + 1;          /* first char after '[' */
    uint64_t    acc = 0;
    int         digits = 0;

    /* must have at least one decimal digit */
    while (*p >= '0' && *p <= '9') {
        acc = acc * 10u + (uint64_t)(*p - '0');
        if (acc > UINT32_MAX)             /* guard against overflow */
            return -1;
        p++;
        digits++;
    }
    if (digits == 0)
        return -1;                        /* "[]" or "[x]" -> malformed */

    /* the very next char must be the closing ']' and then end-of-string */
    if (p[0] != ']' || p[1] != '\0')
        return -1;

    *bracket   = '\0';                    /* trim "[...]" off the key */
    *out_index = (uint32_t)acc;
    return 0;
}

/*
 * Split `path` into section / key (and an optional [index]) using `scratch`
 * as writable storage.  `scratch_len` must be at least strlen(path)+1.
 *
 * Returns 0 on success with `parts` populated; -1 if the path is malformed
 * (NULL, empty, no '.', empty section, empty key, or a bad bracket).
 */
static int split_path(const char *path, char *scratch, size_t scratch_len,
                      bcfg_path_parts *parts)
{
    if (!path)
        return -1;

    size_t len = strlen(path);
    if (len == 0 || len + 1 > scratch_len)
        return -1;

    memcpy(scratch, path, len + 1);

    /* the section/key boundary is the FIRST '.' -- keys themselves never
       contain a dot in this format, so this is unambiguous. */
    char *dot = strchr(scratch, '.');
    if (!dot || dot == scratch)           /* no '.' or empty section */
        return -1;

    *dot = '\0';                          /* terminate the section name */
    char *key = dot + 1;
    if (*key == '\0')                     /* empty key after the dot */
        return -1;

    parts->section   = scratch;
    parts->key       = key;
    parts->has_index = 0;
    parts->index     = 0;

    /* an index, if any, must be a trailing "[...]" on the key */
    char *bracket = strchr(key, '[');
    if (bracket) {
        if (bracket == key)               /* "[..]" with no key name */
            return -1;
        if (parse_index(bracket, &parts->index) != 0)
            return -1;
        parts->has_index = 1;             /* key was truncated in-place */
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* public API                                                           */
/* ------------------------------------------------------------------ */

const bcfg_section *bcfg_query_section(const bcfg_doc *doc, const char *name)
{
    if (!doc || !name)
        return NULL;
    /* bcfg_get_section already does an exact, case-sensitive match */
    return bcfg_get_section(doc, name);
}

/*
 * Resolve the entry referenced by a path and return a borrowed pointer to
 * its value.  The optional [index] is validated against ARRAY bounds but
 * does not change which value is returned.
 *
 * `parts` must already be populated by split_path().  Returns NULL on any
 * failure (missing section/key, out-of-range index, indexed non-array).
 */
static const bcfg_value *resolve(const bcfg_doc *doc,
                                 const bcfg_path_parts *parts)
{
    const bcfg_section *sec = bcfg_get_section(doc, parts->section);
    if (!sec)
        return NULL;

    const bcfg_entry *entry = bcfg_get_entry(sec, parts->key);
    if (!entry)
        return NULL;

    const bcfg_value *val = &entry->value;

    if (parts->has_index) {
        /* an index only makes sense against an array, and must be in range */
        if (val->type != BCFG_TYPE_ARRAY)
            return NULL;
        if (parts->index >= val->v.array.count)
            return NULL;
    }

    return val;
}

const bcfg_value *bcfg_query(const bcfg_doc *doc, const char *path)
{
    if (!doc || !path)
        return NULL;

    /* private writable copy so split_path can carve it up in place */
    size_t len = strlen(path);
    char  *scratch = (char *)malloc(len + 1);
    if (!scratch)
        return NULL;

    bcfg_path_parts parts;
    const bcfg_value *result = NULL;

    if (split_path(path, scratch, len + 1, &parts) == 0)
        result = resolve(doc, &parts);

    free(scratch);
    return result;
}

int bcfg_query_uint32(const bcfg_doc *doc, const char *path, uint32_t *out)
{
    if (!doc || !path || !out)
        return BCFG_ERR_INVALID;

    const bcfg_value *val = bcfg_query(doc, path);
    if (!val)
        return BCFG_ERR_NOTFOUND;
    if (val->type != BCFG_TYPE_UINT32)
        return BCFG_ERR_TYPE;

    *out = val->v.u32;
    return BCFG_OK;
}

int bcfg_query_string(const bcfg_doc *doc, const char *path,
                      const char **out, uint16_t *out_len)
{
    if (!doc || !path || !out || !out_len)
        return BCFG_ERR_INVALID;

    const bcfg_value *val = bcfg_query(doc, path);
    if (!val)
        return BCFG_ERR_NOTFOUND;
    if (val->type != BCFG_TYPE_STRING)
        return BCFG_ERR_TYPE;

    /* borrowed pointer -- owned by the doc, valid until bcfg_free() */
    *out     = val->v.str.data;
    *out_len = val->v.str.len;
    return BCFG_OK;
}
