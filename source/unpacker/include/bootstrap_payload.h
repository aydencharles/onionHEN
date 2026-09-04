/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Embedded LZMA bootstrapper and the build-time metadata needed to
 * validate a disk cache of the decompressed ELF.
 */

#pragma once

#include "sha1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BOOTSTRAP_PAYLOAD_OK = 0,
  BOOTSTRAP_PAYLOAD_INVALID,
  BOOTSTRAP_PAYLOAD_NOMEM,
  BOOTSTRAP_PAYLOAD_LZMA,
} bootstrap_payload_status;

/** Uncompressed ELF size recorded at pack time. 0 if the size file is junk. */
size_t bootstrap_payload_expected_size(void);

/** SHA-1 of the uncompressed ELF recorded at pack time. */
bool bootstrap_payload_expected_sha1(uint8_t digest[SHA1_DIGEST_SIZE]);

/**
 * LZMA-unpack the embedded bootstrapper. On success the caller owns *@out
 * (malloc) and *@out_size is the actual decompressed length.
 */
bootstrap_payload_status bootstrap_payload_decompress(uint8_t **out,
                                                      size_t *out_size);

#ifdef __cplusplus
}
#endif
