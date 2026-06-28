/*
 * hash.h -- hashing utilities
 *
 * Byte-level hashes (FNV-1a, CRC-32) plus structural hashes for the parsed
 * value/entry/section types.  Structural hashes mix the type tag with the
 * scalar bytes, string/blob contents, or array payload so that two values
 * hash equal iff they are byte-for-byte equivalent.
 */

#ifndef CONFIGBIN_HASH_H
#define CONFIGBIN_HASH_H

#include "configbin.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 32-bit FNV-1a hash of n bytes at data. */
uint32_t bcfg_fnv1a(const void *data, size_t n);

/*
 * Standard CRC-32 (reflected, polynomial 0xEDB88320) of n bytes at data.
 * The lookup table is built lazily on first use.
 */
uint32_t bcfg_crc32(const void *data, size_t n);

/*
 * Structural hash of a value: the type tag mixed with the scalar bytes,
 * the string/blob contents, or the array element block as appropriate.
 */
uint64_t bcfg_value_hash(const bcfg_value *v);

/* Structural hash of an entry: its key bytes combined with its value hash. */
uint64_t bcfg_entry_hash(const bcfg_entry *e);

/* Structural hash of a section: its name combined with all entry hashes. */
uint64_t bcfg_section_hash(const bcfg_section *s);

#ifdef __cplusplus
}
#endif

#endif /* CONFIGBIN_HASH_H */
