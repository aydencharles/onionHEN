/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Process-local localization for user-facing notification text.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum onion_notify_language {
  ONION_NOTIFY_LANG_ZH_HANS = 0,
  ONION_NOTIFY_LANG_EN = 1,
  ONION_NOTIFY_LANG_AR = 2,
  ONION_NOTIFY_LANG_ZH_HANT = 3,
  ONION_NOTIFY_LANG_JA = 4,
  ONION_NOTIFY_LANG_FR = 5,
  ONION_NOTIFY_LANG_DE = 6,
  ONION_NOTIFY_LANG_KO = 7,
  ONION_NOTIFY_LANG_ES = 8,
  ONION_NOTIFY_LANG_PT_BR = 9,
  ONION_NOTIFY_LANG_IT = 10,
  ONION_NOTIFY_LANG_RU = 11,
  ONION_NOTIFY_LANG_PL = 12,
  ONION_NOTIFY_LANG_TH = 13,
} onion_notify_language_t;

/** Select the resolved language used by plain and rich notifications. */
void onion_notify_set_language(onion_notify_language_t language);

/** Return the currently selected notification language. */
onion_notify_language_t onion_notify_get_language(void);

/**
 * Resolve the shared toolbox language setting.
 * ui_language: 0=system, 1=zh-Hans, 2=en, 3=ar, 4=zh-Hant, 5=ja, 6=fr,
 * 7=de, 8=ko, 9=es, 10=pt-BR, 11=it, 12=ru, 13=pl, 14=th.
 * system_language is the value returned for SCE_SYSTEM_SERVICE_PARAM_ID_LANG
 * (0=Japanese, 2/22=French, 3/20=Spanish, 4=German, 5=Italian,
 * 7/17=Portuguese, 8=Russian, 9=Korean, 10=zh-Hant, 11=zh-Hans,
 * 16=Polish, 21=Arabic, 27=Thai; anything else follows English).
 */
onion_notify_language_t onion_notify_resolve_language(int ui_language,
                                                       int system_language);

/** Select a language from the shared setting and PS5 system language. */
void onion_notify_apply_ui_language(int ui_language, int system_language);

/** Translate a stable notification key; unknown keys are returned unchanged. */
const char *onion_notify_tr(const char *key);

#ifdef __cplusplus
}
#endif
