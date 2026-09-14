/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/fs.h>
#include <onion/log.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool if_exists(const char *path) {
  struct stat buffer;
  if (!path) {
    return false;
  }
  return stat(path, &buffer) == 0;
}

bool touch_file(const char *path) {
  if (!path) {
    return false;
  }
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd > 0) {
    close(fd);
    return true;
  }
  return false;
}

bool write_file_atomic(const char *path, const void *data, size_t size) {
  char temp_path[1024];

  if (!path || path[0] == '\0' || (size > 0 && !data))
    return false;

  const int n =
      snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", path, (int)getpid());
  if (n < 0 || (size_t)n >= sizeof(temp_path))
    return false;

  unlink(temp_path);
  const int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd < 0) {
    LOG_ERROR("write_file_atomic: open %s failed (%s)", temp_path,
              strerror(errno));
    return false;
  }

  const uint8_t *cursor = (const uint8_t *)data;
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t written = write(fd, cursor, remaining);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0) {
      LOG_ERROR("write_file_atomic: write %s failed (%s)", path,
                written < 0 ? strerror(errno) : "zero-byte write");
      close(fd);
      unlink(temp_path);
      return false;
    }
    cursor += (size_t)written;
    remaining -= (size_t)written;
  }

  if (fsync(fd) != 0) {
    LOG_ERROR("write_file_atomic: fsync %s failed (%s)", path, strerror(errno));
    close(fd);
    unlink(temp_path);
    return false;
  }
  if (close(fd) != 0) {
    LOG_ERROR("write_file_atomic: close %s failed (%s)", path, strerror(errno));
    unlink(temp_path);
    return false;
  }
  if (rename(temp_path, path) != 0) {
    LOG_ERROR("write_file_atomic: rename %s failed (%s)", path, strerror(errno));
    unlink(temp_path);
    return false;
  }
  return true;
}

/* Read exactly @size bytes into @buf, retrying short reads and EINTR. */
static bool read_exact(int fd, uint8_t *buf, size_t size) {
  size_t done = 0;
  while (done < size) {
    const ssize_t n = read(fd, buf + done, size - done);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return false;
    done += (size_t)n;
  }
  return true;
}

bool read_file_alloc(const char *path, size_t max_size, uint8_t **out,
                     size_t *out_size) {
  struct stat st;
  uint8_t *buf;
  size_t size;

  if (!path || path[0] == '\0' || !out || !out_size)
    return false;

  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_DEBUG("read_file_alloc: open %s failed (%s)", path, strerror(errno));
    return false;
  }

  if (fstat(fd, &st) != 0) {
    LOG_DEBUG("read_file_alloc: stat %s failed (%s)", path, strerror(errno));
    close(fd);
    return false;
  }
  if (!S_ISREG(st.st_mode) || st.st_size <= 0) {
    LOG_DEBUG("read_file_alloc: %s is not a non-empty regular file", path);
    close(fd);
    return false;
  }
  size = (size_t)st.st_size;
  if (max_size > 0 && size > max_size) {
    LOG_DEBUG("read_file_alloc: %s is %zu bytes, over the %zu byte cap", path,
              size, max_size);
    close(fd);
    return false;
  }

  /* One spare byte so text callers get a C string for free; *out_size stays
   * the file length. */
  if (!(buf = malloc(size + 1))) {
    LOG_DEBUG("read_file_alloc: cannot allocate %zu bytes for %s", size, path);
    close(fd);
    return false;
  }

  if (!read_exact(fd, buf, size)) {
    LOG_DEBUG("read_file_alloc: read %s failed (%s)", path, strerror(errno));
    free(buf);
    close(fd);
    return false;
  }
  close(fd);
  buf[size] = '\0';

  *out = buf;
  *out_size = size;
  return true;
}

bool read_file_alloc_str(const char *path, size_t max_size, char **out,
                         size_t *out_size) {
  uint8_t *bytes = NULL;
  size_t size = 0;

  if (!out)
    return false;

  if (!read_file_alloc(path, max_size, &bytes, &size))
    return false;

  *out = (char *)bytes;
  *out_size = size;
  return true;
}
