/*
 * bcfg_query -- run a query expression against a binary configuration file
 *
 *   bcfg_query <file> <expr>
 *
 * Parses <file> as a binary document and evaluates <expr> (see expr.h) against
 * it, printing one "section.key = value" line per match.  Exits non-zero when
 * the file cannot be read/parsed or when the query matches nothing.
 */

#include "../src/configbin.h"
#include "../src/expr.h"
#include "../src/dump.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n ? (size_t)n : 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len_out = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <file> <expr>\n", argv[0]);
        return 2;
    }

    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) return 1;

    bcfg_doc *doc = NULL;
    int rc = bcfg_parse(data, n, &doc);
    free(data);
    if (rc != BCFG_OK || !doc) {
        fprintf(stderr, "%s: parse failed (%d)\n", argv[1], rc);
        return 1;
    }

    bcfg_match *m = NULL;
    size_t      count = 0;
    rc = bcfg_expr_eval(doc, argv[2], &m, &count);
    if (rc != BCFG_OK) {
        fprintf(stderr, "bad query '%s' (%d)\n", argv[2], rc);
        bcfg_free(doc);
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        char vbuf[128];
        bcfg_value_to_string(&m[i].entry->value, vbuf, sizeof(vbuf));
        if (m[i].array_index >= 0)
            printf("%s.%s[%d] = %s\n", m[i].section->name, m[i].entry->key,
                   m[i].array_index, vbuf);
        else
            printf("%s.%s = %s\n", m[i].section->name, m[i].entry->key, vbuf);
    }

    bcfg_expr_free(m);
    bcfg_free(doc);

    if (count == 0) {
        fprintf(stderr, "no matches for '%s'\n", argv[2]);
        return 1;
    }
    return 0;
}
