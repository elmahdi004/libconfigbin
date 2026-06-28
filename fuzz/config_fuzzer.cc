/*
 * config_fuzzer.cc -- fuzz bcfg_parse() directly
 *
 * Feed arbitrary bytes through the top-level parser, then exercise
 * the lookup and value-access APIs on whatever is successfully parsed.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>

extern "C" {
#include "../src/configbin.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bcfg_doc *doc = nullptr;
    int rc = bcfg_parse(data, size, &doc);
    if (rc != BCFG_OK || doc == nullptr)
        return 0;

    /* Walk every section and every entry to exercise value accessors */
    for (uint16_t si = 0; si < doc->num_sections; si++) {
        bcfg_section *sec = &doc->sections[si];

        /* also exercise the name-based lookup */
        bcfg_get_section(doc, sec->name);

        for (uint32_t ei = 0; ei < sec->num_entries; ei++) {
            bcfg_entry *ent = &sec->entries[ei];

            bcfg_get_entry(sec, ent->key);

            switch (ent->value.type) {
            case BCFG_TYPE_ARRAY: {
                /* touch every element to ensure the data pointer is valid */
                uint16_t count     = ent->value.v.array.count;
                uint8_t  elem_type = ent->value.v.array.elem_type;
                (void)count; (void)elem_type;
                /* deliberately dereference a few elements */
                if (count > 0) {
                    volatile uint8_t *ptr =
                        reinterpret_cast<volatile uint8_t *>(
                            ent->value.v.array.data);
                    (void)*ptr;
                }
                break;
            }
            case BCFG_TYPE_SECREF:
                bcfg_resolve_secref(doc, &ent->value);
                break;
            default:
                break;
            }
        }
    }

    bcfg_free(doc);
    return 0;
}
