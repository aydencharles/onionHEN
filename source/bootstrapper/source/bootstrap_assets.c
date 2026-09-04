/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/log.h>
#include <onion/notify.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern uint8_t sicon_start[];
extern const unsigned int sicon_size;

#define ONION_ICON(name)                                                       \
  extern uint8_t name##_start[];                                               \
  extern const unsigned int name##_size;
#include "icon_manifest.inc"
#undef ONION_ICON

typedef struct EmbeddedIcon {
  const char *name;
  const uint8_t *data;
  const unsigned int *size;
} EmbeddedIcon;

static const EmbeddedIcon kEmbeddedIcons[] = {
#define ONION_ICON(name) {#name, name##_start, &name##_size},
#include "icon_manifest.inc"
#undef ONION_ICON
};

static bool write_blob_file(const char *path, const void *data, size_t size) {
  char temp_path[1024];
  const int path_len =
      snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", path, getpid());
  if (path_len < 0 || (size_t)path_len >= sizeof(temp_path)) {
    LOG_DEBUG("bootstrap assets: path too long: %s", path);
    return false;
  }

  unlink(temp_path);
  const int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    LOG_ERROR("bootstrap assets: open failed: %s (%s)", path,
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
      LOG_ERROR("bootstrap assets: write failed: %s (%s)", path,
                written < 0 ? strerror(errno) : "zero-byte write");
      close(fd);
      unlink(temp_path);
      return false;
    }
    cursor += written;
    remaining -= (size_t)written;
  }

  if (close(fd) != 0 || rename(temp_path, path) != 0) {
    LOG_ERROR("bootstrap assets: commit failed: %s (%s)", path,
              strerror(errno));
    unlink(temp_path);
    return false;
  }
  return true;
}

bool bootstrap_assets_write(void) {
  mkdir("/data/OnionHEN", 0777);
  mkdir("/data/OnionHEN/assets", 0777);
  mkdir("/user/data", 0777);
  mkdir("/user/data/OnionHEN", 0777);

  const bool startup_icon_ready = write_blob_file(
      ONION_NOTIFY_ICON_PATH, sicon_start, sicon_size);

  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), "/data/OnionHEN/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_blob_file(path, kEmbeddedIcons[i].data,
                          *kEmbeddedIcons[i].size);
  }

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_blob_file("/system_ex/vsh_asset/onionhen.png", sicon_start,
                        sicon_size);
  return startup_icon_ready;
}
