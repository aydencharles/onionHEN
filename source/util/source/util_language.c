/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "util_language.h"

#include <onion/log.h>
#include <onion/notify_i18n.h>

#include <stdatomic.h>

extern int sceSystemServiceParamGetInt(int param_id, int *value);

/* SCE_SYSTEM_SERVICE_PARAM_ID_LANG; English is the safe cold-start fallback. */
static atomic_int g_system_language = ATOMIC_VAR_INIT(1);

void util_store_system_language(int language) {
  atomic_store_explicit(&g_system_language, language, memory_order_relaxed);
  LOG_DEBUG("system language runtime updated value=%d", language);
}

bool util_refresh_system_language(void) {
  int language = 1;
  const int result = sceSystemServiceParamGetInt(1, &language);
  if (result < 0 || language < 0) {
    LOG_WARN("system language query failed result=0x%08X; keeping %d",
             (unsigned int)result, util_cached_system_language());
    return false;
  }

  util_store_system_language(language);
  return true;
}

int util_cached_system_language(void) {
  return atomic_load_explicit(&g_system_language, memory_order_relaxed);
}

void util_apply_ui_language(int ui_language) {
  onion_notify_apply_ui_language(ui_language, util_cached_system_language());
}
