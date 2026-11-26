/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_acquire.h"

#include <onion/log.h>
#include <onion/notify.h>

#include <stdbool.h>
#include <stdlib.h>

#include "bootstrap_cache.h"
#include "bootstrap_payload.h"

/* LZMA-unpack the embedded bootstrapper, or NULL after notifying the user. */
static uint8_t *decompress_bootstrapper(size_t *out_size) {
  uint8_t *elf = NULL;
  size_t elf_size = 0;

  switch (bootstrap_payload_decompress(&elf, &elf_size)) {
  case BOOTSTRAP_PAYLOAD_OK:
    break;
  case BOOTSTRAP_PAYLOAD_NOMEM:
    onion_notify(0, "notify.boot.decompress_nomem");
    return NULL;
  case BOOTSTRAP_PAYLOAD_LZMA:
    onion_notify(0, "notify.boot.decompress_failed");
    return NULL;
  case BOOTSTRAP_PAYLOAD_INVALID:
  default:
    LOG_ERROR("Invalid OnionHEN payload! unable to unpack it!");
    return NULL;
  }

  *out_size = elf_size;
  return elf;
}

uint8_t *bootstrap_acquire(size_t *out_size) {
  uint8_t expected_sha1[SHA1_DIGEST_SIZE];
  bool from_cache = false;
  uint8_t *elf = NULL;

  if (!out_size)
    return NULL;

  const bool have_sha1 = bootstrap_payload_expected_sha1(expected_sha1);
  const size_t expected_size = bootstrap_payload_expected_size();

  (void)bootstrap_cache_prepare_dir();

  /* The cache can only be trusted against the digest recorded at pack time, so
   * a build without one always decompresses. */
  if (have_sha1 && expected_size > 0) {
    elf = bootstrap_cache_try_load(ONIONHEN_CACHED_ELF, expected_size,
                                   expected_sha1);
    from_cache = elf != NULL;
  }

  if (!elf) {
    LOG_DEBUG("Bootstrapping OnionHEN.elf...");
    elf = decompress_bootstrapper(out_size);
    if (!elf)
      return NULL;
  } else {
    *out_size = expected_size;
  }

  if (!from_cache && have_sha1)
    bootstrap_cache_store_if_valid(ONIONHEN_CACHED_ELF, elf, *out_size,
                                   expected_size);

  return elf;
}
