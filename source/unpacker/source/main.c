/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Unpacker composition root. Wires the notification sink, refuses to start
 * when another OnionHEN is already running, then hands whatever
 * bootstrap_acquire() returns to the external elfldr on 9021.
 *
 * Everything else — the cache-vs-LZMA fallback ladder, the cache refresh, and
 * the user-facing failure messages — lives in bootstrap_acquire.
 *
 * A failed cache read, a stale cache or a failed send must never block boot.
 */

#include <elfldr_remote.h>
#include <onion/conflict.h>
#include <onion/log.h>
#include <onion/notify.h>

#include <stdint.h>
#include <stdlib.h>

#include "bootstrap_acquire.h"

int32_t sceKernelSendNotificationRequest(int32_t device, void *req, size_t size,
                                         int32_t blocking);

int main(void) {
  onion_notify_set_send(sceKernelSendNotificationRequest);

  const char *conflict = onion_conflict_detect();
  if (conflict) {
    LOG_ERROR("refusing start: %s already running", conflict);
    onion_notify(0, "notify.boot.conflict", conflict);
    return 0;
  }

  size_t elf_size = 0;
  uint8_t *elf = bootstrap_acquire(&elf_size);
  if (!elf)
    return -1;

  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, elf, elf_size)) {
    onion_notify(0, "notify.elfldr.need_9021");
    free(elf);
    return -1;
  }

  free(elf);
  return 0;
}
