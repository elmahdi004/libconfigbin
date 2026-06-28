/*
 * test_validate.c -- constraint validation tests
 *
 * Build a small document, then check it against rule sets that should pass
 * cleanly and ones that should flag a missing required key, a type mismatch,
 * an out-of-range integer, and an over-length string, asserting the violation
 * counts and that each violation is recorded in the diag.
 */

#include "../src/configbin.h"
#include "../src/validate.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

static void w8(uint8_t **p, uint8_t v)   { **p = v; (*p)++; }
static void w16(uint8_t **p, uint16_t v) { memcpy(*p, &v, 2); *p += 2; }
static void w32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }
static void wn(uint8_t **p, const void *s, size_t n) { memcpy(*p, s, n); *p += n; }

static size_t build(uint8_t *buf)
{
    uint8_t *p = buf;
    w32(&p, BCFG_MAGIC); w16(&p, 1); w16(&p, 1);

    w8(&p, 3); wn(&p, "srv", 3); w32(&p, 2);
    w8(&p, 4); wn(&p, "port", 4); w8(&p, BCFG_TYPE_UINT32); w32(&p, 8080);
    w8(&p, 4); wn(&p, "name", 4); w8(&p, BCFG_TYPE_STRING); w16(&p, 2); wn(&p, "ok", 2);

    return (size_t)(p - buf);
}

static int check(bcfg_ruleset *rs, const bcfg_doc *d, size_t *diag_items)
{
    bcfg_diag diag;
    bcfg_diag_init(&diag);
    int v = bcfg_rules_check(rs, d, &diag);
    if (diag_items) *diag_items = bcfg_diag_count(&diag);
    bcfg_diag_free(&diag);
    return v;
}

int main(void)
{
    uint8_t buf[128];
    size_t len = build(buf);
    bcfg_doc *d = NULL;
    assert(bcfg_parse(buf, len, &d) == BCFG_OK);

    /* a fully satisfied rule set */
    {
        bcfg_ruleset *rs = bcfg_rules_new();
        assert(rs);
        bcfg_rule_spec a = {0};
        a.flags = BCFG_RULE_REQUIRED | BCFG_RULE_TYPE | BCFG_RULE_INT_MAX;
        a.want_type = BCFG_TYPE_UINT32; a.imax = 65535;
        assert(bcfg_rules_add(rs, "srv", "port", &a) == BCFG_OK);

        bcfg_rule_spec b = {0};
        b.flags = BCFG_RULE_REQUIRED | BCFG_RULE_TYPE | BCFG_RULE_STR_MAXLEN;
        b.want_type = BCFG_TYPE_STRING; b.str_maxlen = 10;
        assert(bcfg_rules_add(rs, "srv", "name", &b) == BCFG_OK);

        size_t items = 0;
        assert(check(rs, d, &items) == 0);
        assert(items == 0);
        bcfg_rules_free(rs);
    }

    /* a missing required key */
    {
        bcfg_ruleset *rs = bcfg_rules_new();
        bcfg_rule_spec a = {0};
        a.flags = BCFG_RULE_REQUIRED;
        assert(bcfg_rules_add(rs, "srv", "absent", &a) == BCFG_OK);
        size_t items = 0;
        assert(check(rs, d, &items) == 1);
        assert(items == 1);
        bcfg_rules_free(rs);
    }

    /* type mismatch + integer out of range + string too long */
    {
        bcfg_ruleset *rs = bcfg_rules_new();
        bcfg_rule_spec a = {0};
        a.flags = BCFG_RULE_TYPE | BCFG_RULE_INT_MAX;
        a.want_type = BCFG_TYPE_STRING;   /* port is uint32 -> mismatch */
        a.imax = 100;                     /* 8080 > 100 -> range        */
        assert(bcfg_rules_add(rs, "srv", "port", &a) == BCFG_OK);

        bcfg_rule_spec b = {0};
        b.flags = BCFG_RULE_STR_MAXLEN;
        b.str_maxlen = 1;                 /* "ok" len 2 > 1 -> too long */
        assert(bcfg_rules_add(rs, "srv", "name", &b) == BCFG_OK);

        size_t items = 0;
        int v = check(rs, d, &items);
        assert(v == 3);
        assert(items == 3);
        bcfg_rules_free(rs);
    }

    /* enum constraint */
    {
        bcfg_ruleset *rs = bcfg_rules_new();
        const char *allowed[] = {"prod", "dev"};
        bcfg_rule_spec a = {0};
        a.flags = BCFG_RULE_ENUM;
        a.enum_vals = allowed; a.n_enum_vals = 2;
        assert(bcfg_rules_add(rs, "srv", "name", &a) == BCFG_OK);  /* "ok" not allowed */
        size_t items = 0;
        assert(check(rs, d, &items) == 1);
        bcfg_rules_free(rs);
    }

    bcfg_free(d);
    puts("PASS test_validate");
    puts("All tests passed.");
    return 0;
}
