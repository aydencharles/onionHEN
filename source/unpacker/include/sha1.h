/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * SHA-1. Used by the unpacker to validate the on-disk bootstrapper cache
 * against the digest recorded at pack time.
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

#ifdef __cplusplus
}
#endif
