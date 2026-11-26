/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Persistent decompressed bootstrapper cache under /user/data/OnionHEN.
 *
 * The cache is an optimization only: callers must still be able to boot
 * from the embedded LZMA blob when the directory is missing, the file is
 * stale, or the write fails.
 *
 * A cache hit returns the verified ELF bytes so the caller can hand them to
 * the external elfldr as a plain ELF payload. The bootstrapper is never
 * launched through a file: URI: that protocol only exists in elfldr >= v0.22,
 * and older loaders reject it without any error the caller could observe.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONIONHEN_USER_DATA_DIR "/user/data/OnionHEN"
#define ONIONHEN_CACHED_ELF "/user/data/OnionHEN/onionhen.elf"

/** Create /user/data/OnionHEN (and parents). False if it cannot exist. */
bool bootstrap_cache_prepare_dir(void);

/**
 * Load @path into a freshly malloc'd buffer and validate it in one pass.
 *
 * True only when @path is a regular file of exactly @expected_size bytes whose
 * SHA-1 matches @expected_sha1 (SHA1_DIGEST_SIZE bytes). On failure nothing is
 * allocated and *@out is left untouched, so the caller can fall back to the
 * embedded LZMA blob. The caller owns *@out on success (free()).
 *
 * The digest is fixed at SHA-1 on purpose: this cache is only ever validated
 * against the digest recorded at pack time, so a digest-algorithm parameter
 * would have exactly one legal value.
 */
bool bootstrap_cache_load(const char *path, size_t expected_size,
                          const uint8_t *expected_sha1, uint8_t **out);

/**
 * Boot-path convenience wrapper over bootstrap_cache_load(): the cached ELF
 * bytes (caller free()), or NULL when there is no usable cache. A miss is
 * normal and only logged at DEBUG, so callers can fall back silently.
 */
uint8_t *bootstrap_cache_try_load(const char *path, size_t expected_size,
                                  const uint8_t *expected_sha1);

/**
 * Best-effort cache refresh after a decompression. Skips the write when
 * @elf_size does not match @expected_size and logs, but never fails: a failed
 * write only means the next boot decompresses again.
 */
void bootstrap_cache_store_if_valid(const char *path, const uint8_t *elf,
                                    size_t elf_size, size_t expected_size);

/**
 * Atomically replace @path with @elf via a temp file + rename(). False
 * leaves any previous @path untouched.
 */
bool bootstrap_cache_commit(const char *path, const uint8_t *elf, size_t size);

#ifdef __cplusplus
}
#endif
