/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_notify.h"

#include <onion/log.h>
#include <onion/notify.h>

#include <stdarg.h>

void bootstrap_notify(const char *text, ...) {
  va_list args;
  va_start(args, text);
  onion_notify_v(/*show_watermark=*/0, text, args);
  va_end(args);
}

void bootstrap_notify_starting(bool custom_icon_ready) {
  if (!custom_icon_ready) {
    LOG_WARN("Startup icon unavailable; using system notification icon");
    bootstrap_notify("notify.boot.starting");
    return;
  }
  onion_notify_rich("notify.brand", "notify.boot.starting",
                    ONION_NOTIFY_ICON_PATH, "download",
                    "588193128");
}
