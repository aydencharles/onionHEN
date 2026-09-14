/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_cache.h"

#include "sha1.h"

#include <onion/fs.h>
#include <onion/log.h>
#include <onion/tree.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool bootstrap_cache_prepare_dir(void) {
  if (mkdir_tree(ONIONHEN_USER_DATA_DIR))
    return true;
  LOG_WARN("bootstrap cache: cannot create %s (%s)", ONIONHEN_USER_DATA_DIR,
           strerror(errno));
  return false;
}

bool bootstrap_cache_load(const char *path, size_t expected_size,
                          const uint8_t *expected_sha1, uint8_t **out) {
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint8_t *buf = NULL;
  size_t size = 0;

  if (!path || path[0] != '/' || expected_size == 0 || !expected_sha1 || !out)
    return false;

  /* expected_size doubles as the read cap so a stale or unrelated file is
   * rejected before it is pulled into RAM. */
  if (!read_file_alloc(path, expected_size, &buf, &size))
    return false;

  if (size != expected_size) {
    LOG_DEBUG("bootstrap cache: size mismatch (%zu != %zu)", size,
              expected_size);
    free(buf);
    return false;
  }

  sha1_hash(buf, expected_size, digest);
  if (memcmp(digest, expected_sha1, SHA1_DIGEST_SIZE) != 0) {
    LOG_DEBUG("bootstrap cache: sha1 mismatch");
    free(buf);
    return false;
  }

  LOG_DEBUG("bootstrap cache: loaded %zu bytes from %s", expected_size, path);
  *out = buf;
  return true;
}

uint8_t *bootstrap_cache_try_load(const char *path, size_t expected_size,
                                  const uint8_t *expected_sha1) {
  uint8_t *elf = NULL;

  if (!bootstrap_cache_load(path, expected_size, expected_sha1, &elf))
    return NULL;

  LOG_DEBUG("bootstrap cache hit (%zu bytes), skipping LZMA", expected_size);
  return elf;
}

void bootstrap_cache_store_if_valid(const char *path, const uint8_t *elf,
                                    size_t elf_size, size_t expected_size) {
  if (elf_size != expected_size) {
    LOG_WARN("decompressed size %zu != %zu; not caching", elf_size,
             expected_size);
    return;
  }
  if (!bootstrap_cache_commit(path, elf, elf_size))
    LOG_WARN("bootstrap cache write failed; next boot will decompress");
}

bool bootstrap_cache_commit(const char *path, const uint8_t *elf, size_t size) {
  if (!path || path[0] != '/' || !elf || size == 0)
    return false;
  if (!write_file_atomic(path, elf, size))
    return false;
  LOG_DEBUG("bootstrap cache: wrote %zu bytes to %s", size, path);
  return true;
}
