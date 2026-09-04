/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_cache.h"

#include "sha1.h"

#include <onion/fs.h>
#include <onion/log.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool bootstrap_cache_prepare_dir(void) {
  if (mkdir_tree(ONIONHEN_USER_DATA_DIR))
    return true;
  LOG_WARN("bootstrap cache: cannot create %s (%s)", ONIONHEN_USER_DATA_DIR,
           strerror(errno));
  return false;
}

bool bootstrap_cache_matches(const char *path, size_t expected_size,
                             const uint8_t *expected_digest, size_t digest_size) {
  struct stat st;
  uint8_t digest[SHA1_DIGEST_SIZE];

  if (!path || path[0] != '/' || expected_size == 0 || !expected_digest ||
      digest_size != SHA1_DIGEST_SIZE)
    return false;

  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;

  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      (size_t)st.st_size != expected_size) {
    LOG_DEBUG("bootstrap cache: size mismatch or not a regular file");
    close(fd);
    return false;
  }

  if (!sha1_hash_fd(fd, digest)) {
    LOG_DEBUG("bootstrap cache: sha1 read failed (%s)", strerror(errno));
    close(fd);
    return false;
  }
  close(fd);

  if (memcmp(digest, expected_digest, SHA1_DIGEST_SIZE) != 0) {
    LOG_DEBUG("bootstrap cache: sha1 mismatch");
    return false;
  }
  return true;
}

bool bootstrap_cache_commit(const char *path, const uint8_t *elf, size_t size) {
  if (!path || path[0] != '/' || !elf || size == 0)
    return false;
  if (!write_file_atomic(path, elf, size))
    return false;
  LOG_DEBUG("bootstrap cache: wrote %zu bytes to %s", size, path);
  return true;
}
