/*
 * ctext_lex.c -- tokenizer for the ".bct" text frontend
 *
 * A hand-written scanner that turns source text into the token stream the
 * recursive-descent parser consumes.  Whitespace (other than newlines, which
 * are significant line terminators) and '#'/';' comments are skipped between
 * tokens.  STRING and BLOB tokens decode their contents into an owned sval
 * buffer the caller must free; numeric and keyword tokens carry their parsed
 * value in ival/fval.  The scanner never reads past the bound it was given.
 */

#include "ctext_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void bcfg_lex_init(bcfg_lexer *lx, const char *text, size_t len)
{
    lx->src  = text;
    lx->cur  = text;
    lx->end  = text + len;
    lx->line = 1;
    lx->col  = 1;
}

static int at_end(const bcfg_lexer *lx) { return lx->cur >= lx->end; }
static char peek(const bcfg_lexer *lx)  { return at_end(lx) ? '\0' : *lx->cur; }

static char advance(bcfg_lexer *lx)
{
    char c = *lx->cur++;
    if (c == '\n') { lx->line++; lx->col = 1; }
    else           { lx->col++; }
    return c;
}

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }
static int is_ident_char(int c)
{
    return isalnum(c) || c == '_' || c == '.' || c == '-';
}

/* fill a fresh token with position + defaults */
static void tok_begin(const bcfg_lexer *lx, bcfg_ctok *t, bcfg_tok_kind kind)
{
    memset(t, 0, sizeof(*t));
    t->kind   = kind;
    t->lexeme = lx->cur;
    t->line   = lx->line;
    t->col    = lx->col;
}

static void emit_error(const bcfg_lexer *lx, bcfg_ctok *t, const char *msg)
{
    size_t n = strlen(msg);
    tok_begin(lx, t, BCFG_TK_ERROR);
    t->sval = (char *)malloc(n + 1);
    if (t->sval) { memcpy(t->sval, msg, n + 1); t->slen = n; }
}

/* skip inter-token whitespace and comments (but never a newline) */
static void skip_gaps(bcfg_lexer *lx)
{
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lx);
        } else if (c == '#' || c == ';') {
            while (!at_end(lx) && peek(lx) != '\n')
                advance(lx);
        } else {
            break;
        }
    }
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* lex a "..." string with C-style escapes into an owned buffer */
static void lex_string(bcfg_lexer *lx, bcfg_ctok *t)
{
    tok_begin(lx, t, BCFG_TK_STRING);
    advance(lx);                       /* opening quote */

    size_t  cap = 16, len = 0;
    char   *buf = (char *)malloc(cap);
    if (!buf) { emit_error(lx, t, "out of memory"); return; }

    while (!at_end(lx) && peek(lx) != '"') {
        char c = advance(lx);
        char out;
        if (c == '\\') {
            if (at_end(lx)) { free(buf); emit_error(lx, t, "dangling escape"); return; }
            char e = advance(lx);
            switch (e) {
            case 'n':  out = '\n'; break;
            case 't':  out = '\t'; break;
            case 'r':  out = '\r'; break;
            case '0':  out = '\0'; break;
            case '"':  out = '"';  break;
            case '\\': out = '\\'; break;
            case 'x': {
                int hi = hexval(at_end(lx) ? -1 : peek(lx));
                if (hi < 0) { free(buf); emit_error(lx, t, "bad \\x escape"); return; }
                advance(lx);
                int lo = hexval(at_end(lx) ? -1 : peek(lx));
                if (lo < 0) { free(buf); emit_error(lx, t, "bad \\x escape"); return; }
                advance(lx);
                out = (char)((hi << 4) | lo);
                break;
            }
            default:
                free(buf); emit_error(lx, t, "unknown escape"); return;
            }
        } else {
            out = c;
        }
        if (len + 1 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); emit_error(lx, t, "out of memory"); return; }
            buf = nb;
        }
        buf[len++] = out;
    }

    if (at_end(lx)) { free(buf); emit_error(lx, t, "unterminated string"); return; }
    advance(lx);                       /* closing quote */

    t->sval = buf;
    t->slen = len;
    t->lexeme_len = (size_t)(lx->cur - t->lexeme);
}

/* lex b16"DEADBEEF" after the leading b16 ident has been consumed */
static void lex_blob(bcfg_lexer *lx, bcfg_ctok *t, const char *start,
                     size_t line, size_t col)
{
    memset(t, 0, sizeof(*t));
    t->kind = BCFG_TK_BLOB;
    t->lexeme = start;
    t->line = line;
    t->col  = col;
    advance(lx);                       /* opening quote */

    size_t   cap = 16, len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) { emit_error(lx, t, "out of memory"); return; }

    while (!at_end(lx) && peek(lx) != '"') {
        int hi = hexval(advance(lx));
        if (at_end(lx)) { free(buf); emit_error(lx, t, "odd hex digit count"); return; }
        int lo = hexval(advance(lx));
        if (hi < 0 || lo < 0) { free(buf); emit_error(lx, t, "bad hex digit"); return; }
        if (len + 1 > cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) { free(buf); emit_error(lx, t, "out of memory"); return; }
            buf = nb;
        }
        buf[len++] = (uint8_t)((hi << 4) | lo);
    }
    if (at_end(lx)) { free(buf); emit_error(lx, t, "unterminated blob"); return; }
    advance(lx);                       /* closing quote */

    t->sval = (char *)buf;
    t->slen = len;
    t->lexeme_len = (size_t)(lx->cur - start);
}

/* classify and lex an identifier-like run */
static void lex_ident(bcfg_lexer *lx, bcfg_ctok *t)
{
    const char *start = lx->cur;
    size_t      line  = lx->line, col = lx->col;

    while (!at_end(lx) && is_ident_char((unsigned char)peek(lx)))
        advance(lx);

    size_t n = (size_t)(lx->cur - start);

    /* b16"..." hex blob */
    if (n == 3 && memcmp(start, "b16", 3) == 0 && peek(lx) == '"') {
        lex_blob(lx, t, start, line, col);
        return;
    }

    memset(t, 0, sizeof(*t));
    t->lexeme = start;
    t->lexeme_len = n;
    t->line = line;
    t->col  = col;

    if (n == 4 && memcmp(start, "true", 4) == 0)  { t->kind = BCFG_TK_TRUE;    return; }
    if (n == 5 && memcmp(start, "false", 5) == 0) { t->kind = BCFG_TK_FALSE;   return; }
    if (n == 4 && memcmp(start, "null", 4) == 0)  { t->kind = BCFG_TK_NULLTOK; return; }

    /* t<digits> timestamp literal */
    if (n > 1 && start[0] == 't') {
        int all_digits = 1;
        for (size_t i = 1; i < n; i++)
            if (!isdigit((unsigned char)start[i])) { all_digits = 0; break; }
        if (all_digits) {
            char tmp[32];
            size_t cn = n - 1 < sizeof(tmp) - 1 ? n - 1 : sizeof(tmp) - 1;
            memcpy(tmp, start + 1, cn);
            tmp[cn] = '\0';
            t->kind = BCFG_TK_TS;
            t->ival = (int64_t)strtoll(tmp, NULL, 10);
            return;
        }
    }

    t->kind = BCFG_TK_IDENT;
}

/* lex a numeric literal (decimal, hex, or float) */
static void lex_number(bcfg_lexer *lx, bcfg_ctok *t)
{
    const char *start = lx->cur;
    size_t      line  = lx->line, col = lx->col;
    int         neg   = 0;

    if (peek(lx) == '-') { neg = 1; advance(lx); }

    int is_hex = 0;
    if (peek(lx) == '0' && lx->cur + 1 < lx->end &&
        (lx->cur[1] == 'x' || lx->cur[1] == 'X')) {
        is_hex = 1;
        advance(lx); advance(lx);
    }

    int has_dot = 0, has_exp = 0;
    while (!at_end(lx)) {
        char c = peek(lx);
        if (is_hex) {
            if (hexval((unsigned char)c) >= 0) { advance(lx); continue; }
            break;
        }
        if (isdigit((unsigned char)c)) { advance(lx); continue; }
        if (c == '.' && !has_dot && !has_exp) { has_dot = 1; advance(lx); continue; }
        if ((c == 'e' || c == 'E') && !has_exp) {
            has_exp = 1; advance(lx);
            if (peek(lx) == '+' || peek(lx) == '-') advance(lx);
            continue;
        }
        break;
    }

    size_t n = (size_t)(lx->cur - start);
    char   tmp[64];
    if (n >= sizeof(tmp)) { emit_error(lx, t, "number too long"); return; }
    memcpy(tmp, start, n);
    tmp[n] = '\0';

    memset(t, 0, sizeof(*t));
    t->lexeme = start;
    t->lexeme_len = n;
    t->line = line;
    t->col  = col;
    t->is_negative = neg;

    if (!is_hex && (has_dot || has_exp)) {
        t->kind = BCFG_TK_FLOAT;
        t->fval = strtod(tmp, NULL);
    } else if (is_hex) {
        unsigned long long u = strtoull(tmp + (neg ? 3 : 2), NULL, 16);
        t->kind = BCFG_TK_INT;
        t->ival = neg ? -(int64_t)u : (int64_t)u;
    } else {
        t->kind = BCFG_TK_INT;
        t->ival = (int64_t)strtoll(tmp, NULL, 10);
    }
}

int bcfg_lex_next(bcfg_lexer *lx, bcfg_ctok *tok)
{
    skip_gaps(lx);

    if (at_end(lx)) { tok_begin(lx, tok, BCFG_TK_EOFTOK); return BCFG_OK; }

    char c = peek(lx);

    switch (c) {
    case '\n': tok_begin(lx, tok, BCFG_TK_NEWLINE);  advance(lx); return BCFG_OK;
    case '[':  tok_begin(lx, tok, BCFG_TK_LBRACKET); advance(lx); return BCFG_OK;
    case ']':  tok_begin(lx, tok, BCFG_TK_RBRACKET); advance(lx); return BCFG_OK;
    case '=':  tok_begin(lx, tok, BCFG_TK_EQ);       advance(lx); return BCFG_OK;
    case ',':  tok_begin(lx, tok, BCFG_TK_COMMA);    advance(lx); return BCFG_OK;
    case '{':  tok_begin(lx, tok, BCFG_TK_LBRACE);   advance(lx); return BCFG_OK;
    case '}':  tok_begin(lx, tok, BCFG_TK_RBRACE);   advance(lx); return BCFG_OK;
    case '@':  tok_begin(lx, tok, BCFG_TK_AT);       advance(lx); return BCFG_OK;
    case '"':  lex_string(lx, tok); return BCFG_OK;
    default:   break;
    }

    if (is_ident_start((unsigned char)c)) { lex_ident(lx, tok);  return BCFG_OK; }
    if (isdigit((unsigned char)c) || c == '-') { lex_number(lx, tok); return BCFG_OK; }

    emit_error(lx, tok, "unexpected character");
    advance(lx);
    return BCFG_OK;
}
