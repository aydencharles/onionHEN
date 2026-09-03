/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_filesystem.h"

#include "bootstrap_notify.h"

#include <onion/log.h>

#include <stddef.h>
#include <stdio.h>
#include <sys/_iovec.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct BootstrapIovec {
  const void *iov_base;
  size_t iov_length;
} BootstrapIovec;

#define BUILD_IOVEC(value)                                                     \
  { .iov_base = (value), .iov_length = __builtin_strlen(value) + 1 }

static bool remount(const char *device, const char *path) {
  BootstrapIovec iov[] = {
      BUILD_IOVEC("fstype"),    BUILD_IOVEC("exfatfs"),
      BUILD_IOVEC("fspath"),    BUILD_IOVEC(path),
      BUILD_IOVEC("from"),      BUILD_IOVEC(device),
      BUILD_IOVEC("large"),     BUILD_IOVEC("yes"),
      BUILD_IOVEC("timezone"),  BUILD_IOVEC("static"),
      BUILD_IOVEC("async"),     {NULL, 0},
      BUILD_IOVEC("ignoreacl"), {NULL, 0},
  };
  return nmount((struct iovec *)iov, sizeof(iov) / sizeof(iov[0]),
                MNT_UPDATE) == 0;
}

void bootstrap_filesystem_create_directories(void) {
  mkdir("/data/OnionHEN", 0777);
  mkdir("/data/OnionHEN/payloads", 0777);
  mkdir("/data/OnionHEN/plugins", 0777);
  mkdir("/data/OnionHEN/assets", 0777);
  mkdir("/data/OnionHEN/games", 0777);
}

bool bootstrap_filesystem_mount_system(void) {
  LOG_DEBUG("Remounting system partitions ...");
  if (!remount("/dev/ssd0.system_ex", "/system_ex")) {
    perror("failed to mount /system_ex\nif you see this reboot");
    bootstrap_notify("notify.mount.system_ex");
    return false;
  }
  if (!remount("/dev/ssd0.system", "/system")) {
    perror("failed to mount /system_\nif you see this reboot");
    bootstrap_notify("notify.mount.system");
    return false;
  }
  LOG_DEBUG("   Success!");
  return true;
}

void bootstrap_filesystem_disable_updates(void) {
  LOG_DEBUG("Unmounting /update forcefully ...");
  unlink("/update/PS5UPDATE.PUP");
  unlink("/update/PS5UPDATE.PUP.net.temp");
  if (unmount("/update", 0x80000LL) < 0)
    (void)unmount("/update", 0);
  LOG_DEBUG("   Success!");
}
