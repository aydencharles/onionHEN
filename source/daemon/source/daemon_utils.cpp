/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Shared daemon helpers (file/net/app query) — extracted from commands.cpp.
 */

#include "daemon_ops.hpp"
#include "launcher.hpp"

#include <onion/platform.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

extern "C" {
  int sceSystemServiceGetAppIdOfRunningBigApp();
  int sceSystemServiceGetAppTitleId(int app_id, char *title_id);
}

bool Get_Running_App_TID(std::string &title_id, int &BigAppid) {
  char tid[255];
  BigAppid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (BigAppid < 0)
    return false;

  (void)memset(tid, 0, sizeof tid);
  if (sceSystemServiceGetAppTitleId(BigAppid, &tid[0]) != 0)
    return false;

  title_id = std::string(tid);
  return true;
}

bool Open_Utility_Elf(const char *path, uint8_t **buffer) {
  if (!path || !buffer) {
    LOG_ERROR("Invalid arguments: path or buffer is null.");
    return false;
  }

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open file: %s (error: %s)", path, strerror(errno));
    return false;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    LOG_ERROR("Failed to get file stats for %s (error: %s)", path, strerror(errno));
    close(fd);
    return false;
  }

  if (st.st_size == 0) {
    LOG_ERROR("File %s is empty.", path);
    close(fd);
    return false;
  }

  uint8_t *buf = (uint8_t *)malloc((size_t)st.st_size);
  if (!buf) {
    LOG_ERROR("Failed to allocate memory for file %s (size: %ld bytes).", path,
                 st.st_size);
    close(fd);
    return false;
  }

  ssize_t bytes_read = read(fd, buf, (size_t)st.st_size);
  if (bytes_read != st.st_size) {
    LOG_ERROR("Failed to read the entire file %s (read: %ld bytes, expected: %ld bytes).",
                 path, bytes_read, st.st_size);
    free(buf);
    close(fd);
    return false;
  }

  close(fd);
  *buffer = buf;
  return true;
}
