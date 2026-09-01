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

const char *util_webui_language_code(void) {
  /* The web UI ships dictionaries for every catalog locale; map the resolved
   * notify language to its matching code so ui_lang=system follows the
   * console language for all of them. */
  switch (onion_notify_get_language()) {
  case ONION_NOTIFY_LANG_ZH_HANS:
    return "zh-Hans";
  case ONION_NOTIFY_LANG_AR:
    return "ar";
  case ONION_NOTIFY_LANG_ZH_HANT:
    return "zh-Hant";
  case ONION_NOTIFY_LANG_JA:
    return "ja";
  case ONION_NOTIFY_LANG_FR:
    return "fr";
  case ONION_NOTIFY_LANG_DE:
    return "de";
  case ONION_NOTIFY_LANG_KO:
    return "ko";
  case ONION_NOTIFY_LANG_ES:
    return "es";
  case ONION_NOTIFY_LANG_PT_BR:
    return "pt-BR";
  case ONION_NOTIFY_LANG_IT:
    return "it";
  case ONION_NOTIFY_LANG_RU:
    return "ru";
  case ONION_NOTIFY_LANG_PL:
    return "pl";
  case ONION_NOTIFY_LANG_TH:
    return "th";
  case ONION_NOTIFY_LANG_EN:
  default:
    return "en";
  }
}
