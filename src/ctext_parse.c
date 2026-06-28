/*
 * ctext_parse.c -- recursive-descent parser for the ".bct" text frontend
 *
 * Drives the lexer in ctext_lex.c and assembles a bcfg_doc whose allocation
 * shape matches the binary parser's, so the result is freeable with
 * bcfg_free().  Sections and entries grow via realloc; scalar values are
 * decoded inline and arrays are packed into little-endian element blocks.
 * Any lexical or grammatical problem is reported through the diag (with line
 * and column) and unwinds cleanly, freeing the partial document.
 */

#include "ctext.h"
#include "ctext_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* internal value destructor, defined in value.c */
void bcfg_free_value(bcfg_value *v);

typedef struct {
    bcfg_lexer lx;
    bcfg_ctok  cur;        /* one-token lookahead                       */
    bcfg_diag *diag;       /* may be NULL                               */
    bcfg_doc  *doc;
    int        cur_section; /* index of the section entries attach to   */
} parser;

static size_t elem_size(uint8_t t)
{
    switch (t) {
    case BCFG_TYPE_BOOL:      return 1;
    case BCFG_TYPE_UINT32:    return 4;
    case BCFG_TYPE_INT64:     return 8;
    case BCFG_TYPE_FLOAT64:   return 8;
    case BCFG_TYPE_TIMESTAMP: return 8;
    default:                  return 0;
    }
}

static void perr(parser *p, int code, const char *msg)
{
    if (p->diag)
        bcfg_diag_add(p->diag, BCFG_SEV_ERROR, code, p->cur.line,
                      "line %zu col %zu: %s", p->cur.line, p->cur.col, msg);
}

/* discard the current token's owned payload and lex the next one */
static void advance(parser *p)
{
    if (p->cur.sval) { free(p->cur.sval); p->cur.sval = NULL; }
    bcfg_lex_next(&p->lx, &p->cur);
}

/* take ownership of the current STRING/BLOB buffer, detaching it from cur */
static char *take_payload(parser *p, size_t *len_out)
{
    char *s = p->cur.sval;
    *len_out = p->cur.slen;
    p->cur.sval = NULL;
    return s;
}

static int grow_sections(parser *p)
{
    bcfg_doc *d = p->doc;
    if (d->num_sections == 0xFFFF) return BCFG_ERR_RANGE;
    bcfg_section *ns = (bcfg_section *)realloc(
        d->sections, ((size_t)d->num_sections + 1) * sizeof(bcfg_section));
    if (!ns) return BCFG_ERR_NOMEM;
    d->sections = ns;
    memset(&d->sections[d->num_sections], 0, sizeof(bcfg_section));
    return BCFG_OK;
}

static int append_entry(parser *p, int sec_idx, const bcfg_entry *e)
{
    bcfg_section *s = &p->doc->sections[sec_idx];
    bcfg_entry *ne = (bcfg_entry *)realloc(
        s->entries, ((size_t)s->num_entries + 1) * sizeof(bcfg_entry));
    if (!ne) return BCFG_ERR_NOMEM;
    s->entries = ne;
    s->entries[s->num_entries] = *e;
    s->num_entries++;
    return BCFG_OK;
}

/* append one scalar's little-endian wire bytes for an array element */
static int put_elem(bcfg_buf *b, uint8_t type, const bcfg_ctok *t)
{
    switch (type) {
    case BCFG_TYPE_UINT32:    return bcfg_buf_put_u32(b, (uint32_t)t->ival);
    case BCFG_TYPE_INT64:     return bcfg_buf_put_u64(b, (uint64_t)t->ival);
    case BCFG_TYPE_TIMESTAMP: return bcfg_buf_put_u64(b, (uint64_t)t->ival);
    case BCFG_TYPE_BOOL:      return bcfg_buf_put_u8(b,
                                  (uint8_t)(t->kind == BCFG_TK_TRUE ? 1 : 0));
    case BCFG_TYPE_FLOAT64: {
        double d = (t->kind == BCFG_TK_FLOAT) ? t->fval : (double)t->ival;
        uint64_t bits;
        memcpy(&bits, &d, 8);
        return bcfg_buf_put_u64(b, bits);
    }
    default: return BCFG_ERR_INVALID;
    }
}

/* infer the array element type from the first element token */
static uint8_t infer_elem_type(const bcfg_ctok *t)
{
    switch (t->kind) {
    case BCFG_TK_INT:
        if (!t->is_negative && t->ival >= 0 && t->ival <= (int64_t)UINT32_MAX)
            return BCFG_TYPE_UINT32;
        return BCFG_TYPE_INT64;
    case BCFG_TK_FLOAT: return BCFG_TYPE_FLOAT64;
    case BCFG_TK_TRUE:
    case BCFG_TK_FALSE: return BCFG_TYPE_BOOL;
    case BCFG_TK_TS:    return BCFG_TYPE_TIMESTAMP;
    default:            return 0;
    }
}

static int elem_is_compatible(uint8_t type, const bcfg_ctok *t)
{
    switch (type) {
    case BCFG_TYPE_UINT32:
        return t->kind == BCFG_TK_INT && !t->is_negative &&
               t->ival >= 0 && t->ival <= (int64_t)UINT32_MAX;
    case BCFG_TYPE_INT64:     return t->kind == BCFG_TK_INT;
    case BCFG_TYPE_FLOAT64:   return t->kind == BCFG_TK_FLOAT ||
                                     t->kind == BCFG_TK_INT;
    case BCFG_TYPE_BOOL:      return t->kind == BCFG_TK_TRUE ||
                                     t->kind == BCFG_TK_FALSE;
    case BCFG_TYPE_TIMESTAMP: return t->kind == BCFG_TK_TS;
    default:                  return 0;
    }
}

static int parse_array(parser *p, bcfg_value *out)
{
    bcfg_buf  bytes;
    uint8_t   etype = BCFG_TYPE_UINT32;   /* default for an empty array */
    uint32_t  count = 0;
    int       first = 1;
    int       rc;

    bcfg_buf_init(&bytes);
    advance(p);                            /* consume '[' */

    for (;;) {
        while (p->cur.kind == BCFG_TK_NEWLINE)
            advance(p);

        if (p->cur.kind == BCFG_TK_RBRACKET) { advance(p); break; }

        if (first) {
            etype = infer_elem_type(&p->cur);
            if (etype == 0) {
                perr(p, BCFG_ERR_INVALID, "unsupported array element type");
                bcfg_buf_free(&bytes); return BCFG_ERR_INVALID;
            }
            first = 0;
        }
        if (!elem_is_compatible(etype, &p->cur)) {
            perr(p, BCFG_ERR_TYPE, "array element type mismatch");
            bcfg_buf_free(&bytes); return BCFG_ERR_TYPE;
        }
        if (put_elem(&bytes, etype, &p->cur) != BCFG_OK) {
            bcfg_buf_free(&bytes); return BCFG_ERR_NOMEM;
        }
        if (count == 0xFFFF) {
            perr(p, BCFG_ERR_RANGE, "array too long");
            bcfg_buf_free(&bytes); return BCFG_ERR_RANGE;
        }
        count++;
        advance(p);

        while (p->cur.kind == BCFG_TK_NEWLINE)
            advance(p);
        if (p->cur.kind == BCFG_TK_COMMA) { advance(p); continue; }
        if (p->cur.kind == BCFG_TK_RBRACKET) { advance(p); break; }

        perr(p, BCFG_ERR_INVALID, "expected ',' or ']' in array");
        bcfg_buf_free(&bytes); return BCFG_ERR_INVALID;
    }

    size_t span = (size_t)count * elem_size(etype);
    void  *data = malloc(span ? span : 1);
    if (!data) { bcfg_buf_free(&bytes); return BCFG_ERR_NOMEM; }
    if (span) memcpy(data, bytes.data, span);
    bcfg_buf_free(&bytes);

    out->type             = BCFG_TYPE_ARRAY;
    out->v.array.elem_type = etype;
    out->v.array.count    = (uint16_t)count;
    out->v.array.data     = data;
    rc = BCFG_OK;
    return rc;
}

static int parse_value(parser *p, bcfg_value *out)
{
    memset(out, 0, sizeof(*out));

    switch (p->cur.kind) {
    case BCFG_TK_INT:
        if (!p->cur.is_negative && p->cur.ival >= 0 &&
            p->cur.ival <= (int64_t)UINT32_MAX) {
            out->type  = BCFG_TYPE_UINT32;
            out->v.u32 = (uint32_t)p->cur.ival;
        } else {
            out->type  = BCFG_TYPE_INT64;
            out->v.i64 = p->cur.ival;
        }
        advance(p);
        return BCFG_OK;

    case BCFG_TK_FLOAT:
        out->type  = BCFG_TYPE_FLOAT64;
        out->v.f64 = p->cur.fval;
        advance(p);
        return BCFG_OK;

    case BCFG_TK_TRUE:
    case BCFG_TK_FALSE:
        out->type      = BCFG_TYPE_BOOL;
        out->v.boolean = (uint8_t)(p->cur.kind == BCFG_TK_TRUE ? 1 : 0);
        advance(p);
        return BCFG_OK;

    case BCFG_TK_NULLTOK:
        out->type = BCFG_TYPE_NULL;
        advance(p);
        return BCFG_OK;

    case BCFG_TK_TS:
        out->type = BCFG_TYPE_TIMESTAMP;
        out->v.ts = p->cur.ival;
        advance(p);
        return BCFG_OK;

    case BCFG_TK_STRING: {
        if (p->cur.slen > 0xFFFF) {
            perr(p, BCFG_ERR_RANGE, "string too long");
            return BCFG_ERR_RANGE;
        }
        size_t len; char *raw = take_payload(p, &len);
        char *s = (char *)malloc(len + 1);
        if (!s) { free(raw); return BCFG_ERR_NOMEM; }
        if (len) memcpy(s, raw, len);
        s[len] = '\0';
        free(raw);
        out->type        = BCFG_TYPE_STRING;
        out->v.str.len   = (uint16_t)len;
        out->v.str.data  = s;
        advance(p);
        return BCFG_OK;
    }

    case BCFG_TK_BLOB: {
        size_t len; char *raw = take_payload(p, &len);
        uint8_t *b = (uint8_t *)malloc(len ? len : 1);
        if (!b) { free(raw); return BCFG_ERR_NOMEM; }
        if (len) memcpy(b, raw, len);
        free(raw);
        out->type        = BCFG_TYPE_BLOB;
        out->v.blob.len  = (uint32_t)len;
        out->v.blob.data = b;
        advance(p);
        return BCFG_OK;
    }

    case BCFG_TK_AT:
        advance(p);
        if (p->cur.kind != BCFG_TK_INT || p->cur.is_negative ||
            p->cur.ival < 0 || p->cur.ival > 0xFFFF) {
            perr(p, BCFG_ERR_INVALID, "@ must be followed by a section index");
            return BCFG_ERR_INVALID;
        }
        out->type      = BCFG_TYPE_SECREF;
        out->v.secref  = (uint16_t)p->cur.ival;
        advance(p);
        return BCFG_OK;

    case BCFG_TK_LBRACKET:
        return parse_array(p, out);

    case BCFG_TK_ERROR:
        perr(p, BCFG_ERR_INVALID, p->cur.sval ? p->cur.sval : "lex error");
        return BCFG_ERR_INVALID;

    default:
        perr(p, BCFG_ERR_INVALID, "expected a value");
        return BCFG_ERR_INVALID;
    }
}

static int parse_section_header(parser *p)
{
    advance(p);                            /* consume '[' */
    if (p->cur.kind != BCFG_TK_IDENT) {
        perr(p, BCFG_ERR_INVALID, "expected section name");
        return BCFG_ERR_INVALID;
    }
    if (p->cur.lexeme_len < 1 || p->cur.lexeme_len > 255) {
        perr(p, BCFG_ERR_RANGE, "section name length out of range");
        return BCFG_ERR_RANGE;
    }
    uint8_t nlen = (uint8_t)p->cur.lexeme_len;
    char *name = (char *)malloc(nlen + 1);
    if (!name) return BCFG_ERR_NOMEM;
    memcpy(name, p->cur.lexeme, nlen);
    name[nlen] = '\0';

    advance(p);
    if (p->cur.kind != BCFG_TK_RBRACKET) {
        free(name);
        perr(p, BCFG_ERR_INVALID, "expected ']'");
        return BCFG_ERR_INVALID;
    }
    advance(p);

    int rc = grow_sections(p);
    if (rc != BCFG_OK) { free(name); return rc; }

    bcfg_section *s = &p->doc->sections[p->doc->num_sections];
    s->name        = name;
    s->name_len    = nlen;
    s->entries     = NULL;
    s->num_entries = 0;
    p->cur_section = p->doc->num_sections;
    p->doc->num_sections++;
    return BCFG_OK;
}

static int parse_entry(parser *p)
{
    if (p->cur_section < 0) {
        perr(p, BCFG_ERR_INVALID, "key before any [section]");
        return BCFG_ERR_INVALID;
    }
    if (p->cur.lexeme_len < 1 || p->cur.lexeme_len > 255) {
        perr(p, BCFG_ERR_RANGE, "key length out of range");
        return BCFG_ERR_RANGE;
    }
    uint8_t klen = (uint8_t)p->cur.lexeme_len;
    char *key = (char *)malloc(klen + 1);
    if (!key) return BCFG_ERR_NOMEM;
    memcpy(key, p->cur.lexeme, klen);
    key[klen] = '\0';

    advance(p);
    if (p->cur.kind != BCFG_TK_EQ) {
        free(key);
        perr(p, BCFG_ERR_INVALID, "expected '=' after key");
        return BCFG_ERR_INVALID;
    }
    advance(p);

    bcfg_entry e;
    e.key     = key;
    e.key_len = klen;
    int rc = parse_value(p, &e.value);
    if (rc != BCFG_OK) { free(key); return rc; }

    rc = append_entry(p, p->cur_section, &e);
    if (rc != BCFG_OK) { free(key); bcfg_free_value(&e.value); return rc; }
    return BCFG_OK;
}

int bcfg_ctext_parse(const char *text, size_t len, bcfg_doc **out,
                     bcfg_diag *diag)
{
    parser p;
    int    rc = BCFG_OK;

    if (!text || !out) return BCFG_ERR_INVALID;
    *out = NULL;

    p.doc = (bcfg_doc *)calloc(1, sizeof(bcfg_doc));
    if (!p.doc) return BCFG_ERR_NOMEM;
    p.diag        = diag;
    p.cur_section = -1;
    memset(&p.cur, 0, sizeof(p.cur));
    bcfg_lex_init(&p.lx, text, len);
    advance(&p);

    while (p.cur.kind != BCFG_TK_EOFTOK) {
        if (p.cur.kind == BCFG_TK_NEWLINE) { advance(&p); continue; }

        if (p.cur.kind == BCFG_TK_LBRACKET)
            rc = parse_section_header(&p);
        else if (p.cur.kind == BCFG_TK_IDENT)
            rc = parse_entry(&p);
        else if (p.cur.kind == BCFG_TK_ERROR) {
            perr(&p, BCFG_ERR_INVALID, p.cur.sval ? p.cur.sval : "lex error");
            rc = BCFG_ERR_INVALID;
        } else {
            perr(&p, BCFG_ERR_INVALID, "expected section header or key");
            rc = BCFG_ERR_INVALID;
        }

        if (rc != BCFG_OK) break;
    }

    if (p.cur.sval) { free(p.cur.sval); p.cur.sval = NULL; }

    if (rc != BCFG_OK) {
        bcfg_free(p.doc);
        return rc;
    }
    *out = p.doc;
    return BCFG_OK;
}
