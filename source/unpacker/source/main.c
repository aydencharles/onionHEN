/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Unpacker composition root. OnionHEN.elf embeds a LZMA-compressed
 * bootstrapper and hands it to the external elfldr on port 9021.
 *
 *   bootstrap_payload — embedded blob, size, SHA-1, LZMA
 *   bootstrap_cache   — /user/data/OnionHEN/onionhen.elf
 *   elfldr_remote     — 9021 memory send and file: URI
 *
 * Cache misses and cache-send failures fall back to decompress + memory
 * send. A failed write must never block boot.
 */

#include <elfldr_remote.h>
#include <onion/conflict.h>
#include <onion/log.h>
#include <onion/notify.h>

#include <stdint.h>
#include <stdlib.h>

#include "bootstrap_cache.h"
#include "bootstrap_payload.h"

int32_t sceKernelSendNotificationRequest(int32_t device, void *req, size_t size,
                                         int32_t blocking);

static void cache_decompressed(const uint8_t *elf, size_t elf_size,
                               size_t expected_size) {
  if (elf_size != expected_size) {
    LOG_WARN("decompressed size %zu != %zu; not caching", elf_size,
             expected_size);
    return;
  }
  if (!bootstrap_cache_commit(ONIONHEN_CACHED_ELF, elf, elf_size))
    LOG_WARN("bootstrap cache write failed; next boot will decompress");
}

int main(void) {
  onion_notify_set_send(sceKernelSendNotificationRequest);

  const char *conflict = onion_conflict_detect();
  if (conflict) {
    LOG_ERROR("refusing start: %s already running", conflict);
    onion_notify(0, "notify.boot.conflict", conflict);
    return 0;
  }

  uint8_t expected_sha1[SHA1_DIGEST_SIZE];
  const bool have_sha1 = bootstrap_payload_expected_sha1(expected_sha1);
  const size_t expected_size = bootstrap_payload_expected_size();

  (void)bootstrap_cache_prepare_dir();
  if (have_sha1 && expected_size > 0 &&
      bootstrap_cache_matches(ONIONHEN_CACHED_ELF, expected_size, expected_sha1,
                              SHA1_DIGEST_SIZE)) {
    LOG_DEBUG("bootstrap cache hit, sending file URI");
    if (elfldr_remote_send_file_to(ELFLDR_REMOTE_PORT, ONIONHEN_CACHED_ELF))
      return 0;
    LOG_WARN("bootstrap cache file URI failed; falling back to memory send");
  }

  uint8_t *elf = NULL;
  size_t elf_size = 0;
  switch (bootstrap_payload_decompress(&elf, &elf_size)) {
  case BOOTSTRAP_PAYLOAD_OK:
    break;
  case BOOTSTRAP_PAYLOAD_NOMEM:
    onion_notify(0, "Failed to allocate memory for decompressed OnionHEN payload!");
    return -1;
  case BOOTSTRAP_PAYLOAD_LZMA:
    onion_notify(0, "Failed to decompress OnionHEN payload!");
    return -1;
  case BOOTSTRAP_PAYLOAD_INVALID:
  default:
    LOG_ERROR("Invalid OnionHEN payload! unable to unpack it!");
    return 0;
  }

  LOG_DEBUG("Bootstrapping OnionHEN.elf...");
  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, elf, elf_size)) {
    onion_notify(0, "notify.elfldr.need_9021");
    free(elf);
    return -1;
  }

  if (have_sha1)
    cache_decompressed(elf, elf_size, expected_size);

  free(elf);
  return 0;
}
