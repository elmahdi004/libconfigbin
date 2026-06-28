/*
 * ctext_internal.h -- PRIVATE lexer<->parser interface for the text frontend
 *
 * Not part of the public API.  Defines the token kinds, the token record
 * (bcfg_ctok) the lexer hands the parser, and the lexer state (bcfg_lexer)
 * with its scan-one-token entry points.  Shared only between ctext_lex.c and
 * ctext_parse.c.
 */

#ifndef CONFIGBIN_CTEXT_INTERNAL_H
#define CONFIGBIN_CTEXT_INTERNAL_H

#include "configbin.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* token kinds produced by the lexer */
typedef enum {
    BCFG_TK_LBRACKET,   /* '[' opening a [section] header                 */
    BCFG_TK_RBRACKET,   /* ']' closing a [section] header                 */
    BCFG_TK_IDENT,      /* key or section name [A-Za-z0-9_.-]+            */
    BCFG_TK_EQ,         /* '='                                            */
    BCFG_TK_INT,        /* integer literal (see ival/is_negative)         */
    BCFG_TK_FLOAT,      /* floating literal (see fval)                    */
    BCFG_TK_STRING,     /* "..." with escapes decoded into sval           */
    BCFG_TK_TRUE,       /* true                                           */
    BCFG_TK_FALSE,      /* false                                          */
    BCFG_TK_NULLTOK,    /* null                                           */
    BCFG_TK_LBRACE,     /* '{' -- reserved; arrays open with '['          */
    BCFG_TK_COMMA,      /* ',' array element separator                    */
    BCFG_TK_RBRACE,     /* '}' -- reserved array/group close              */
    BCFG_TK_AT,         /* '@' introducing a SECREF (@<index>)            */
    BCFG_TK_BLOB,       /* b16"..." hex blob; bytes decoded into sval     */
    BCFG_TK_TS,         /* t<digits> timestamp; nanoseconds in ival       */
    BCFG_TK_NEWLINE,    /* end of a logical line                          */
    BCFG_TK_EOFTOK,     /* end of input                                   */
    BCFG_TK_ERROR       /* lexical error; message in sval                 */
} bcfg_tok_kind;

/* one lexed token; lexeme points into the source text and is not owned */
typedef struct {
    bcfg_tok_kind kind;        /* token classification                       */
    const char   *lexeme;      /* start of the raw token in the source       */
    size_t        lexeme_len;  /* raw token length in bytes                  */
    /* parsed scratch, valid per-kind: */
    int64_t       ival;        /* INT / TS value                             */
    double        fval;        /* FLOAT value                                */
    char         *sval;        /* decoded STRING/BLOB bytes (owned), or NULL */
    size_t        slen;        /* length of sval in bytes                    */
    int           is_negative; /* INT: literal carried a leading '-'         */
    size_t        line;        /* 1-based source line                        */
    size_t        col;         /* 1-based source column                      */
} bcfg_ctok;

/* lexer state over a fixed source span */
typedef struct {
    const char *src;   /* start of source text                        */
    const char *cur;   /* next byte to scan                           */
    const char *end;   /* one past the last source byte               */
    size_t      line;  /* current 1-based line                        */
    size_t      col;   /* current 1-based column                      */
} bcfg_lexer;

/* Bind a lexer to text[0, len). */
void bcfg_lex_init(bcfg_lexer *lx, const char *text, size_t len);

/*
 * Scan the next token into *tok.  Comments ('#' and ';' to end of line) are
 * skipped.  STRING/BLOB tokens allocate tok->sval, which the caller frees.
 * Returns BCFG_OK; an ERROR/EOFTOK kind is delivered via *tok, not the return.
 */
int bcfg_lex_next(bcfg_lexer *lx, bcfg_ctok *tok);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_CTEXT_INTERNAL_H */
