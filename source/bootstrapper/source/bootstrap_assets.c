/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/fs.h>
#include <onion/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

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

bool bootstrap_assets_write(void) {
  mkdir("/data/OnionHEN", 0777);
  mkdir("/data/OnionHEN/assets", 0777);
  mkdir("/user/data", 0777);
  mkdir("/user/data/OnionHEN", 0777);

  const bool startup_icon_ready = write_file_atomic(
      ONION_NOTIFY_ICON_PATH, sicon_start, sicon_size);

  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), "/data/OnionHEN/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_file_atomic(path, kEmbeddedIcons[i].data,
                            *kEmbeddedIcons[i].size);
  }

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic("/system_ex/vsh_asset/onionhen.png", sicon_start,
                          sicon_size);
  return startup_icon_ready;
}
