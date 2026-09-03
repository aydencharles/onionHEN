/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Shared /system_tmp namespace for OnionHEN runtime state.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#if defined(ONION_HOST_TEST)
#define ONION_SYSTEM_TMP_ROOT "/tmp/onionhen"
#else
#define ONION_SYSTEM_TMP_ROOT "/system_tmp/onionhen"
#endif

#define ONION_SYSTEM_TMP_IPC_ROOT ONION_SYSTEM_TMP_ROOT "/ipc"
#define ONION_SYSTEM_TMP_READY_ROOT ONION_SYSTEM_TMP_ROOT "/ready"
#define ONION_SYSTEM_TMP_PID_ROOT ONION_SYSTEM_TMP_ROOT "/pid"

#define ONION_SYSTEM_TMP_CRIT_SOCKET ONION_SYSTEM_TMP_IPC_ROOT "/crit_service"
#define ONION_SYSTEM_TMP_UTIL_SOCKET ONION_SYSTEM_TMP_IPC_ROOT "/util_service"
#define ONION_SYSTEM_TMP_PLUGIN_SOCKET ONION_SYSTEM_TMP_IPC_ROOT "/plugin_service"
#define ONION_SYSTEM_TMP_SHELLUI_PLUGIN_SOCKET                                  \
  ONION_SYSTEM_TMP_IPC_ROOT "/shellui_plugin_bridge"

#define ONION_SYSTEM_TMP_ELFLDR_STATE                                      \
  ONION_SYSTEM_TMP_PID_ROOT "/onion_elfldr_9020.PID"
#define ONION_SYSTEM_TMP_ELFLDR_BUSY                                       \
  ONION_SYSTEM_TMP_PID_ROOT "/onion_elfldr_9020.busy"
#define ONION_SYSTEM_TMP_APP_LAUNCHED ONION_SYSTEM_TMP_ROOT "/app_launched"
#define ONION_SYSTEM_TMP_PATCH_PLUGIN ONION_SYSTEM_TMP_ROOT "/patch_plugin"
#define ONION_SYSTEM_TMP_FPS_SAMPLE ONION_SYSTEM_TMP_ROOT "/fps_sample"

static inline bool onion_system_tmp_pid_path(char *out, size_t out_sz,
                                             const char *key) {
  if (!out || out_sz == 0) {
    return false;
  }
  int n = snprintf(out, out_sz, "%s/%s.PID", ONION_SYSTEM_TMP_PID_ROOT,
                   key ? key : "");
  return n > 0 && (size_t)n < out_sz;
}
