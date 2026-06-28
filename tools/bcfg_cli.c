/*
 * bcfg_cli.c -- command-line front-end for libconfigbin
 *
 * Usage:
 *   bcfg_cli <file> [--dump | --hex | --validate | --query <path>]
 *
 * Reads the whole file into memory, parses it with bcfg_parse, and then:
 *   --dump            render an INI/TOML-ish text view (bcfg_dump_text)
 *   --hex             classic offset/hex/ascii hexdump of the raw bytes
 *   --validate        report that the document parsed (a structural summary)
 *   --query <path>    resolve "section.key" and print the scalar value
 *   (no mode)         print a one-line summary: section and entry counts
 *
 * On any parse error the matching bcfg_strerror() message is written to
 * stderr and the process exits non-zero.  All heap allocations are freed.
 */

#include "../src/configbin.h"
#include "../src/buffer.h"
#include "../src/dump.h"
#include "../src/path.h"
#include "../src/iter.h"
#include "../src/diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Slurp an entire file into a freshly malloc'd buffer.  On success *out_data
   owns the bytes (free it) and *out_len holds the length; returns 0.  On any
   I/O or allocation failure prints to stderr and returns -1. */
static int read_file(const char *path, uint8_t **out_data, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "bcfg_cli: cannot open '%s'\n", path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "bcfg_cli: cannot seek '%s'\n", path);
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "bcfg_cli: cannot size '%s'\n", path);
        fclose(f);
        return -1;
    }
    rewind(f);

    size_t len = (size_t)sz;
    uint8_t *data = (uint8_t *)malloc(len ? len : 1);
    if (!data) {
        fprintf(stderr, "bcfg_cli: out of memory\n");
        fclose(f);
        return -1;
    }

    size_t got = len ? fread(data, 1, len, f) : 0;
    fclose(f);
    if (got != len) {
        fprintf(stderr, "bcfg_cli: short read on '%s'\n", path);
        free(data);
        return -1;
    }

    *out_data = data;
    *out_len  = len;
    return 0;
}

/* Write a bcfg_buf's contents verbatim to stdout. */
static void emit_buf(const bcfg_buf *b)
{
    if (b->data && b->len)
        fwrite(b->data, 1, b->len, stdout);
}

/* --dump : render the parsed document as text. */
static int do_dump(const bcfg_doc *doc)
{
    bcfg_buf out;
    bcfg_buf_init(&out);
    int rc = bcfg_dump_text(doc, &out);
    if (rc != BCFG_OK) {
        fprintf(stderr, "bcfg_cli: dump failed: %s\n", bcfg_strerror(rc));
        bcfg_buf_free(&out);
        return 1;
    }
    emit_buf(&out);
    bcfg_buf_free(&out);
    return 0;
}

/* --hex : hexdump the raw input bytes. */
static int do_hex(const uint8_t *data, size_t len)
{
    bcfg_buf out;
    bcfg_buf_init(&out);
    int rc = bcfg_dump_hex(data, len, &out);
    if (rc != BCFG_OK) {
        fprintf(stderr, "bcfg_cli: hexdump failed: %s\n", bcfg_strerror(rc));
        bcfg_buf_free(&out);
        return 1;
    }
    emit_buf(&out);
    bcfg_buf_free(&out);
    return 0;
}

/* --validate : the document parsed; report a structural summary. */
static int do_validate(const bcfg_doc *doc)
{
    size_t entries = bcfg_count_entries(doc);
    printf("valid: %u section(s), %zu entr%s\n",
           (unsigned)doc->num_sections, entries,
           entries == 1 ? "y" : "ies");
    return 0;
}

/* --query <path> : resolve a scalar value and print it. */
static int do_query(const bcfg_doc *doc, const char *path)
{
    const bcfg_value *v = bcfg_query(doc, path);
    if (!v) {
        fprintf(stderr, "bcfg_cli: path not found: %s\n", path);
        return 1;
    }
    char buf[256];
    bcfg_value_to_string(v, buf, sizeof buf);
    printf("%s\n", buf);
    return 0;
}

/* Default mode : one-line summary of section/entry counts. */
static int do_summary(const bcfg_doc *doc)
{
    size_t entries = bcfg_count_entries(doc);
    printf("%u section(s), %zu entr%s\n",
           (unsigned)doc->num_sections, entries,
           entries == 1 ? "y" : "ies");
    return 0;
}

static void usage(FILE *out, const char *argv0)
{
    fprintf(out,
        "usage: %s <file> [--dump | --hex | --validate | --query <path>]\n",
        argv0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr, argv[0]);
        return 2;
    }

    /* The first non-option argument is the file path. */
    const char *file = argv[1];
    if (strcmp(file, "--help") == 0 || strcmp(file, "-h") == 0) {
        usage(stdout, argv[0]);
        return 0;
    }

    /* Parse the optional mode flag (defaults to a summary). */
    enum { M_SUMMARY, M_DUMP, M_HEX, M_VALIDATE, M_QUERY } mode = M_SUMMARY;
    const char *query_path = NULL;

    if (argc >= 3) {
        const char *flag = argv[2];
        if (strcmp(flag, "--dump") == 0) {
            mode = M_DUMP;
        } else if (strcmp(flag, "--hex") == 0) {
            mode = M_HEX;
        } else if (strcmp(flag, "--validate") == 0) {
            mode = M_VALIDATE;
        } else if (strcmp(flag, "--query") == 0) {
            if (argc < 4) {
                fprintf(stderr, "bcfg_cli: --query requires a <path>\n");
                usage(stderr, argv[0]);
                return 2;
            }
            mode = M_QUERY;
            query_path = argv[3];
        } else {
            fprintf(stderr, "bcfg_cli: unknown option '%s'\n", flag);
            usage(stderr, argv[0]);
            return 2;
        }
    }

    /* Read the file. */
    uint8_t *data = NULL;
    size_t   len  = 0;
    if (read_file(file, &data, &len) != 0)
        return 1;

    /* Parse it. */
    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(data, len, &doc);
    if (rc != BCFG_OK) {
        fprintf(stderr, "bcfg_cli: parse error: %s\n", bcfg_strerror(rc));
        free(data);
        bcfg_free(doc);   /* NULL-safe; doc stays NULL on failure */
        return 1;
    }

    /* Dispatch. */
    int status;
    switch (mode) {
    case M_DUMP:     status = do_dump(doc);              break;
    case M_HEX:      status = do_hex(data, len);         break;
    case M_VALIDATE: status = do_validate(doc);          break;
    case M_QUERY:    status = do_query(doc, query_path); break;
    case M_SUMMARY:  default: status = do_summary(doc);  break;
    }

    bcfg_free(doc);
    free(data);
    return status;
}
