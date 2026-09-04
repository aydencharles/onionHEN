/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Streaming SHA-1. Used by the unpacker cache to hash on-disk ELFs in
 * chunks so a cache hit never materializes the whole file in RAM.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA1_DIGEST_SIZE 20
#define SHA1_HEX_SIZE 40

/** Parse 40 hex chars (optional surrounding whitespace). */
bool sha1_parse_hex(const char *hex, uint8_t digest[SHA1_DIGEST_SIZE]);

/** Hash a contiguous buffer. */
void sha1_hash(const uint8_t *data, size_t len, uint8_t digest[SHA1_DIGEST_SIZE]);

/** Hash the whole file behind @fd from offset 0, reading in blocks. */
bool sha1_hash_fd(int fd, uint8_t digest[SHA1_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif
