/*
 * bcfg_convert -- convert a configuration document between representations
 *
 *   bcfg_convert <in> <out> --from <bin|text|json> --to <bin|text|json>
 *
 * Reads <in>, parses it with the requested frontend (binary bcfg_parse, text
 * bcfg_ctext_parse, or JSON bcfg_json_parse), then emits it through the
 * requested backend (bcfg_write_doc / bcfg_ctext_write / bcfg_json_write) to
 * <out>.  Diagnostics from the text/JSON readers are printed to stderr.
 */

#include "../src/configbin.h"
#include "../src/writer.h"
#include "../src/ctext.h"
#include "../src/cjson.h"
#include "../src/buffer.h"
#include "../src/diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum fmt { FMT_BIN, FMT_TEXT, FMT_JSON };

static int parse_fmt(const char *s, enum fmt *out)
{
    if (!strcmp(s, "bin"))  { *out = FMT_BIN;  return 0; }
    if (!strcmp(s, "text")) { *out = FMT_TEXT; return 0; }
    if (!strcmp(s, "json")) { *out = FMT_JSON; return 0; }
    return -1;
}

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

static void dump_diag(const bcfg_diag *d)
{
    for (size_t i = 0; i < bcfg_diag_count(d); i++) {
        const bcfg_diag_item *it = bcfg_diag_at(d, i);
        fprintf(stderr, "  [%d] %s\n", it->code, it->message);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s <in> <out> --from <bin|text|json> --to <bin|text|json>\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL, *from_s = NULL, *to_s = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--from") && i + 1 < argc) from_s = argv[++i];
        else if (!strcmp(argv[i], "--to") && i + 1 < argc) to_s = argv[++i];
        else if (!in)  in = argv[i];
        else if (!out) out = argv[i];
        else { usage(argv[0]); return 2; }
    }
    enum fmt from, to;
    if (!in || !out || !from_s || !to_s ||
        parse_fmt(from_s, &from) || parse_fmt(to_s, &to)) {
        usage(argv[0]);
        return 2;
    }

    size_t n = 0;
    uint8_t *data = slurp(in, &n);
    if (!data) return 1;

    bcfg_diag diag;
    bcfg_diag_init(&diag);
    bcfg_doc *doc = NULL;
    int rc;

    switch (from) {
    case FMT_BIN:  rc = bcfg_parse(data, n, &doc); break;
    case FMT_TEXT: rc = bcfg_ctext_parse((const char *)data, n, &doc, &diag); break;
    case FMT_JSON: rc = bcfg_json_parse((const char *)data, n, &doc, &diag); break;
    default:       rc = BCFG_ERR_INVALID; break;
    }
    free(data);

    if (rc != BCFG_OK || !doc) {
        fprintf(stderr, "%s: parse failed: %s\n", in, bcfg_strerror(rc));
        dump_diag(&diag);
        bcfg_diag_free(&diag);
        return 1;
    }
    bcfg_diag_free(&diag);

    bcfg_buf buf;
    bcfg_buf_init(&buf);
    switch (to) {
    case FMT_BIN:  rc = bcfg_write_doc(doc, &buf); break;
    case FMT_TEXT: rc = bcfg_ctext_write(doc, &buf); break;
    case FMT_JSON: rc = bcfg_json_write(doc, &buf, 1); break;
    default:       rc = BCFG_ERR_INVALID; break;
    }
    bcfg_free(doc);

    if (rc != BCFG_OK) {
        fprintf(stderr, "%s: encode failed: %s\n", out, bcfg_strerror(rc));
        bcfg_buf_free(&buf);
        return 1;
    }

    FILE *of = fopen(out, "wb");
    if (!of) { perror(out); bcfg_buf_free(&buf); return 1; }
    if (buf.len && fwrite(buf.data, 1, buf.len, of) != buf.len) {
        perror(out); fclose(of); bcfg_buf_free(&buf); return 1;
    }
    fclose(of);
    bcfg_buf_free(&buf);

    fprintf(stderr, "wrote %s (%s -> %s)\n", out, from_s, to_s);
    return 0;
}
