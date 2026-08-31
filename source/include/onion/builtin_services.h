/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <strings.h>

/* Built-in service default configuration. */
#define ONION_FTPSRV_PORT 1337u
/* DPI package install server (network installer) TCP listen port. */
#define ONION_PKGNET_PORT 9090u
/* WebUI HTTP server (serves the package installer page + SSE status). */
#define ONION_WEBUI_PORT 12800u

static inline int onion_builtin_shadowmount_key(const char *key) {
  return key && strcasecmp(key, "shadowmountplus") == 0;
}

/* True for legacy user-payload names that collide with built-in services.
 * Those files may still live under payloads/; autostart must not replace a
 * built-in instance that the user has opted into. */
static inline int onion_builtin_service_key_reserved(const char *key) {
  return onion_builtin_shadowmount_key(key);
}

#ifdef __cplusplus
}
#endif
