/*
 * cjson_parse.c -- JSON -> bcfg_doc bridge (subset)
 *
 * A self-contained recursive-descent JSON reader specialised to the document
 * mapping documented in cjson.h: the top-level object's members are sections,
 * a section object's members are entries, and entry values are scalars or
 * homogeneous numeric arrays.  Constructs outside the subset (a nested object
 * as an entry value, an array of non-numbers) are reported through the diag.
 * The document is built with the same allocation shape the binary parser uses,
 * so the result frees cleanly with bcfg_free().
 */

#include "cjson.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

/* internal value destructor, defined in value.c */
void bcfg_free_value(bcfg_value *v);

typedef struct {
    const char *start;
    const char *p;
    const char *end;
    bcfg_diag  *diag;
} jctx;

typedef struct {
    int     is_real;
    int64_t i;
    double  d;
} numtok;

static void jerr(jctx *j, int code, const char *msg)
{
    if (j->diag)
        bcfg_diag_add(j->diag, BCFG_SEV_ERROR, code,
                      (size_t)(j->p - j->start), "%s", msg);
}

static void skip_ws(jctx *j)
{
    while (j->p < j->end) {
        char c = *j->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') j->p++;
        else break;
    }
}

static int peek(jctx *j) { return (j->p < j->end) ? (unsigned char)*j->p : -1; }

/* ---- doc growth helpers (mirror the binary allocation shape) ---- */

static int grow_sections(bcfg_doc *d)
{
    if (d->num_sections == 0xFFFF) return BCFG_ERR_RANGE;
    bcfg_section *ns = (bcfg_section *)realloc(
        d->sections, ((size_t)d->num_sections + 1) * sizeof(bcfg_section));
    if (!ns) return BCFG_ERR_NOMEM;
    d->sections = ns;
    memset(&d->sections[d->num_sections], 0, sizeof(bcfg_section));
    return BCFG_OK;
}

static int append_entry(bcfg_section *s, const bcfg_entry *e)
{
    bcfg_entry *ne = (bcfg_entry *)realloc(
        s->entries, ((size_t)s->num_entries + 1) * sizeof(bcfg_entry));
    if (!ne) return BCFG_ERR_NOMEM;
    s->entries = ne;
    s->entries[s->num_entries] = *e;
    s->num_entries++;
    return BCFG_OK;
}

/* ---- scalar scanners ---- */

static int hex4(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        int c = p[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = (v << 4) | d;
    }
    return v;
}

/* parse a JSON string at j->p (which must be on '"'); returns owned buffer */
static int parse_string(jctx *j, char **out, size_t *out_len)
{
    if (peek(j) != '"') { jerr(j, BCFG_ERR_INVALID, "expected string"); return BCFG_ERR_INVALID; }
    j->p++;

    size_t cap = 16, len = 0;
    char  *buf = (char *)malloc(cap);
    if (!buf) return BCFG_ERR_NOMEM;

    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        char enc[4];
        size_t enc_n = 0;
        if (c == '\\') {
            if (j->p >= j->end) { free(buf); jerr(j, BCFG_ERR_TRUNCATED, "dangling escape"); return BCFG_ERR_INVALID; }
            char e = *j->p++;
            switch (e) {
            case '"':  enc[0] = '"';  enc_n = 1; break;
            case '\\': enc[0] = '\\'; enc_n = 1; break;
            case '/':  enc[0] = '/';  enc_n = 1; break;
            case 'b':  enc[0] = '\b'; enc_n = 1; break;
            case 'f':  enc[0] = '\f'; enc_n = 1; break;
            case 'n':  enc[0] = '\n'; enc_n = 1; break;
            case 'r':  enc[0] = '\r'; enc_n = 1; break;
            case 't':  enc[0] = '\t'; enc_n = 1; break;
            case 'u': {
                if (j->end - j->p < 4) { free(buf); jerr(j, BCFG_ERR_TRUNCATED, "short \\u"); return BCFG_ERR_INVALID; }
                int cp = hex4(j->p);
                if (cp < 0) { free(buf); jerr(j, BCFG_ERR_INVALID, "bad \\u"); return BCFG_ERR_INVALID; }
                j->p += 4;
                /* encode the BMP code point as UTF-8 */
                if (cp < 0x80) { enc[0] = (char)cp; enc_n = 1; }
                else if (cp < 0x800) {
                    enc[0] = (char)(0xC0 | (cp >> 6));
                    enc[1] = (char)(0x80 | (cp & 0x3F));
                    enc_n = 2;
                } else {
                    enc[0] = (char)(0xE0 | (cp >> 12));
                    enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    enc[2] = (char)(0x80 | (cp & 0x3F));
                    enc_n = 3;
                }
                break;
            }
            default: free(buf); jerr(j, BCFG_ERR_INVALID, "bad escape"); return BCFG_ERR_INVALID;
            }
        } else {
            enc[0] = c; enc_n = 1;
        }
        if (len + enc_n > cap) {
            while (len + enc_n > cap) cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return BCFG_ERR_NOMEM; }
            buf = nb;
        }
        memcpy(buf + len, enc, enc_n);
        len += enc_n;
    }
    if (j->p >= j->end) { free(buf); jerr(j, BCFG_ERR_TRUNCATED, "unterminated string"); return BCFG_ERR_INVALID; }
    j->p++;                            /* closing quote */

    *out = buf;
    *out_len = len;
    return BCFG_OK;
}

static int parse_number(jctx *j, numtok *nt)
{
    const char *start = j->p;
    int is_real = 0;

    if (peek(j) == '-') j->p++;
    while (j->p < j->end) {
        char c = *j->p;
        if (c >= '0' && c <= '9') { j->p++; }
        else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            is_real = (c == '.' || c == 'e' || c == 'E') ? 1 : is_real;
            j->p++;
        } else break;
    }
    size_t n = (size_t)(j->p - start);
    if (n == 0) { jerr(j, BCFG_ERR_INVALID, "expected number"); return BCFG_ERR_INVALID; }

    char tmp[64];
    if (n >= sizeof(tmp)) { jerr(j, BCFG_ERR_RANGE, "number too long"); return BCFG_ERR_RANGE; }
    memcpy(tmp, start, n);
    tmp[n] = '\0';

    nt->is_real = is_real;
    if (is_real) {
        nt->d = strtod(tmp, NULL);
        nt->i = 0;
    } else {
        errno = 0;
        long long v = strtoll(tmp, NULL, 10);
        if (errno == ERANGE) { nt->is_real = 1; nt->d = strtod(tmp, NULL); }
        else nt->i = (int64_t)v;
    }
    return BCFG_OK;
}

/* ---- array of numbers ---- */

static int parse_num_array(jctx *j, bcfg_value *out)
{
    numtok  *nums = NULL;
    size_t   count = 0, cap = 0;
    int      rc = BCFG_OK;

    j->p++;                            /* consume '[' */
    skip_ws(j);

    if (peek(j) == ']') {
        j->p++;
    } else {
        for (;;) {
            skip_ws(j);
            int c = peek(j);
            if (c != '-' && !(c >= '0' && c <= '9')) {
                jerr(j, BCFG_ERR_TYPE, "array elements must be numbers");
                free(nums); return BCFG_ERR_TYPE;
            }
            if (count == cap) {
                size_t ncap = cap ? cap * 2 : 8;
                numtok *nn = (numtok *)realloc(nums, ncap * sizeof(*nn));
                if (!nn) { free(nums); return BCFG_ERR_NOMEM; }
                nums = nn; cap = ncap;
            }
            rc = parse_number(j, &nums[count]);
            if (rc != BCFG_OK) { free(nums); return rc; }
            count++;
            if (count > 0xFFFF) { jerr(j, BCFG_ERR_RANGE, "array too long"); free(nums); return BCFG_ERR_RANGE; }
            skip_ws(j);
            if (peek(j) == ',') { j->p++; continue; }
            if (peek(j) == ']') { j->p++; break; }
            jerr(j, BCFG_ERR_INVALID, "expected ',' or ']'"); free(nums); return BCFG_ERR_INVALID;
        }
    }

    /* decide a homogeneous element type */
    uint8_t et = BCFG_TYPE_UINT32;
    int any_real = 0, need_i64 = 0;
    for (size_t i = 0; i < count; i++) {
        if (nums[i].is_real) any_real = 1;
        else if (nums[i].i < 0 || nums[i].i > (int64_t)UINT32_MAX) need_i64 = 1;
    }
    if (any_real)      et = BCFG_TYPE_FLOAT64;
    else if (need_i64) et = BCFG_TYPE_INT64;

    bcfg_buf bytes;
    bcfg_buf_init(&bytes);
    for (size_t i = 0; i < count; i++) {
        int e;
        if (et == BCFG_TYPE_UINT32)      e = bcfg_buf_put_u32(&bytes, (uint32_t)nums[i].i);
        else if (et == BCFG_TYPE_INT64)  e = bcfg_buf_put_u64(&bytes, (uint64_t)nums[i].i);
        else {
            double d = nums[i].is_real ? nums[i].d : (double)nums[i].i;
            uint64_t bits; memcpy(&bits, &d, 8);
            e = bcfg_buf_put_u64(&bytes, bits);
        }
        if (e != BCFG_OK) { bcfg_buf_free(&bytes); free(nums); return BCFG_ERR_NOMEM; }
    }
    free(nums);

    size_t span = bytes.len;
    void  *data = malloc(span ? span : 1);
    if (!data) { bcfg_buf_free(&bytes); return BCFG_ERR_NOMEM; }
    if (span) memcpy(data, bytes.data, span);
    bcfg_buf_free(&bytes);

    out->type              = BCFG_TYPE_ARRAY;
    out->v.array.elem_type = et;
    out->v.array.count     = (uint16_t)count;
    out->v.array.data      = data;
    return BCFG_OK;
}

static int parse_entry_value(jctx *j, bcfg_value *out)
{
    memset(out, 0, sizeof(*out));
    skip_ws(j);
    int c = peek(j);

    if (c == '{') { jerr(j, BCFG_ERR_INVALID, "nested object not allowed as a value"); return BCFG_ERR_INVALID; }
    if (c == '[') return parse_num_array(j, out);

    if (c == '"') {
        char *s; size_t len;
        int rc = parse_string(j, &s, &len);
        if (rc != BCFG_OK) return rc;
        if (len > 0xFFFF) { free(s); jerr(j, BCFG_ERR_RANGE, "string too long"); return BCFG_ERR_RANGE; }
        char *z = (char *)malloc(len + 1);
        if (!z) { free(s); return BCFG_ERR_NOMEM; }
        if (len) memcpy(z, s, len);
        z[len] = '\0';
        free(s);
        out->type       = BCFG_TYPE_STRING;
        out->v.str.len  = (uint16_t)len;
        out->v.str.data = z;
        return BCFG_OK;
    }

    if (c == 't' || c == 'f') {
        if ((size_t)(j->end - j->p) >= 4 && memcmp(j->p, "true", 4) == 0) {
            j->p += 4; out->type = BCFG_TYPE_BOOL; out->v.boolean = 1; return BCFG_OK;
        }
        if ((size_t)(j->end - j->p) >= 5 && memcmp(j->p, "false", 5) == 0) {
            j->p += 5; out->type = BCFG_TYPE_BOOL; out->v.boolean = 0; return BCFG_OK;
        }
        jerr(j, BCFG_ERR_INVALID, "bad literal"); return BCFG_ERR_INVALID;
    }
    if (c == 'n') {
        if ((size_t)(j->end - j->p) >= 4 && memcmp(j->p, "null", 4) == 0) {
            j->p += 4; out->type = BCFG_TYPE_NULL; return BCFG_OK;
        }
        jerr(j, BCFG_ERR_INVALID, "bad literal"); return BCFG_ERR_INVALID;
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
        numtok nt;
        int rc = parse_number(j, &nt);
        if (rc != BCFG_OK) return rc;
        if (nt.is_real) { out->type = BCFG_TYPE_FLOAT64; out->v.f64 = nt.d; }
        else if (nt.i >= 0 && nt.i <= (int64_t)UINT32_MAX) {
            out->type = BCFG_TYPE_UINT32; out->v.u32 = (uint32_t)nt.i;
        } else { out->type = BCFG_TYPE_INT64; out->v.i64 = nt.i; }
        return BCFG_OK;
    }

    jerr(j, BCFG_ERR_INVALID, "unexpected token");
    return BCFG_ERR_INVALID;
}

static int parse_section_obj(jctx *j, bcfg_section *s)
{
    skip_ws(j);
    if (peek(j) != '{') { jerr(j, BCFG_ERR_INVALID, "section must be an object"); return BCFG_ERR_INVALID; }
    j->p++;
    skip_ws(j);
    if (peek(j) == '}') { j->p++; return BCFG_OK; }

    for (;;) {
        skip_ws(j);
        char  *key; size_t klen;
        int rc = parse_string(j, &key, &klen);
        if (rc != BCFG_OK) return rc;
        if (klen < 1 || klen > 255) { free(key); jerr(j, BCFG_ERR_RANGE, "key length out of range"); return BCFG_ERR_RANGE; }

        skip_ws(j);
        if (peek(j) != ':') { free(key); jerr(j, BCFG_ERR_INVALID, "expected ':'"); return BCFG_ERR_INVALID; }
        j->p++;

        bcfg_entry e;
        e.key = (char *)malloc(klen + 1);
        if (!e.key) { free(key); return BCFG_ERR_NOMEM; }
        memcpy(e.key, key, klen); e.key[klen] = '\0';
        e.key_len = (uint8_t)klen;
        free(key);

        rc = parse_entry_value(j, &e.value);
        if (rc != BCFG_OK) { free(e.key); return rc; }

        rc = append_entry(s, &e);
        if (rc != BCFG_OK) { free(e.key); bcfg_free_value(&e.value); return rc; }

        skip_ws(j);
        if (peek(j) == ',') { j->p++; continue; }
        if (peek(j) == '}') { j->p++; break; }
        jerr(j, BCFG_ERR_INVALID, "expected ',' or '}'"); return BCFG_ERR_INVALID;
    }
    return BCFG_OK;
}

int bcfg_json_parse(const char *json, size_t len, bcfg_doc **out,
                    bcfg_diag *diag)
{
    jctx j;
    int  rc;

    if (!json || !out) return BCFG_ERR_INVALID;
    *out = NULL;

    j.start = json; j.p = json; j.end = json + len; j.diag = diag;

    bcfg_doc *doc = (bcfg_doc *)calloc(1, sizeof(bcfg_doc));
    if (!doc) return BCFG_ERR_NOMEM;

    skip_ws(&j);
    if (peek(&j) != '{') { jerr(&j, BCFG_ERR_INVALID, "document must be an object"); bcfg_free(doc); return BCFG_ERR_INVALID; }
    j.p++;
    skip_ws(&j);

    if (peek(&j) == '}') { j.p++; *out = doc; return BCFG_OK; }

    for (;;) {
        skip_ws(&j);
        char  *name; size_t nlen;
        rc = parse_string(&j, &name, &nlen);
        if (rc != BCFG_OK) { bcfg_free(doc); return rc; }
        if (nlen < 1 || nlen > 255) { free(name); jerr(&j, BCFG_ERR_RANGE, "section name length out of range"); bcfg_free(doc); return BCFG_ERR_RANGE; }

        skip_ws(&j);
        if (peek(&j) != ':') { free(name); jerr(&j, BCFG_ERR_INVALID, "expected ':'"); bcfg_free(doc); return BCFG_ERR_INVALID; }
        j.p++;

        rc = grow_sections(doc);
        if (rc != BCFG_OK) { free(name); bcfg_free(doc); return rc; }
        bcfg_section *s = &doc->sections[doc->num_sections];
        s->name = (char *)malloc(nlen + 1);
        if (!s->name) { free(name); bcfg_free(doc); return BCFG_ERR_NOMEM; }
        memcpy(s->name, name, nlen); s->name[nlen] = '\0';
        s->name_len = (uint8_t)nlen;
        free(name);
        doc->num_sections++;           /* section now safe to free */

        rc = parse_section_obj(&j, s);
        if (rc != BCFG_OK) { bcfg_free(doc); return rc; }

        skip_ws(&j);
        if (peek(&j) == ',') { j.p++; continue; }
        if (peek(&j) == '}') { j.p++; break; }
        jerr(&j, BCFG_ERR_INVALID, "expected ',' or '}'"); bcfg_free(doc); return BCFG_ERR_INVALID;
    }

    *out = doc;
    return BCFG_OK;
}
