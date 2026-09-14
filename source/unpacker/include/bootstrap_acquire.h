/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * The unpacker's "give me the bootstrapper ELF" use case.
 *
 * Prefers the verified on-disk cache, falls back to the embedded LZMA blob,
 * and refreshes the cache whenever it had to decompress. Every failure either
 * falls back or notifies the user, so callers only decide whether to continue.
 *
 * Kept out of the composition root so main.c stays wiring only.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the bootstrapper ELF to hand to the external elfldr (caller free()),
 * or NULL when neither the cache nor the embedded blob is usable. *@out_size
 * receives the length on success and is left untouched on failure.
 *
 * A cache hit only replaces the LZMA decompression; both paths yield the same
 * raw ELF. User-visible failures are notified here.
 */
uint8_t *bootstrap_acquire(size_t *out_size);

#ifdef __cplusplus
}
#endif
