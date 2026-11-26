/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/notify_i18n.h>

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/* Generated from source/i18n locale catalogs at build time. */
#include "notify_i18n_catalog.inc"

static atomic_int gLanguage = ATOMIC_VAR_INIT(ONION_NOTIFY_LANG_EN);

static int language_is_known(onion_notify_language_t language) {
  return language >= ONION_NOTIFY_LANG_ZH_HANS &&
         language <= ONION_NOTIFY_LANG_PT_PT;
}

static const char *locale_id_for_language(onion_notify_language_t language) {
  switch (language) {
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
  case ONION_NOTIFY_LANG_NL:
    return "nl";
  case ONION_NOTIFY_LANG_FI:
    return "fi";
  case ONION_NOTIFY_LANG_SV:
    return "sv";
  case ONION_NOTIFY_LANG_DA:
    return "da";
  case ONION_NOTIFY_LANG_NO:
    return "no";
  case ONION_NOTIFY_LANG_TR:
    return "tr";
  case ONION_NOTIFY_LANG_CS:
    return "cs";
  case ONION_NOTIFY_LANG_HU:
    return "hu";
  case ONION_NOTIFY_LANG_EL:
    return "el";
  case ONION_NOTIFY_LANG_RO:
    return "ro";
  case ONION_NOTIFY_LANG_VI:
    return "vi";
  case ONION_NOTIFY_LANG_ID:
    return "id";
  case ONION_NOTIFY_LANG_UK:
    return "uk";
  case ONION_NOTIFY_LANG_PT_PT:
    return "pt-PT";
  case ONION_NOTIFY_LANG_EN:
  default:
    return "en";
  }
}

static int locale_index_for_language(onion_notify_language_t language) {
  const char *id = locale_id_for_language(language);
  int i;

  for (i = 0; i < I18N_LOCALE_COUNT; ++i) {
    if (strcmp(kI18nLocaleIds[i], id) == 0)
      return i;
  }
  return kI18nLocaleFallback;
}

void onion_notify_set_language(onion_notify_language_t language) {
  if (!language_is_known(language))
    language = ONION_NOTIFY_LANG_EN;
  atomic_store_explicit(&gLanguage, language, memory_order_relaxed);
}

onion_notify_language_t onion_notify_get_language(void) {
  return (onion_notify_language_t)atomic_load_explicit(&gLanguage,
                                                        memory_order_relaxed);
}

onion_notify_language_t onion_notify_resolve_language(int ui_language,
                                                       int system_language) {
  switch (ui_language) {
  case 1:
    return ONION_NOTIFY_LANG_ZH_HANS;
  case 2:
    return ONION_NOTIFY_LANG_EN;
  case 3:
    return ONION_NOTIFY_LANG_AR;
  case 4:
    return ONION_NOTIFY_LANG_ZH_HANT;
  case 5:
    return ONION_NOTIFY_LANG_JA;
  case 6:
    return ONION_NOTIFY_LANG_FR;
  case 7:
    return ONION_NOTIFY_LANG_DE;
  case 8:
    return ONION_NOTIFY_LANG_KO;
  case 9:
    return ONION_NOTIFY_LANG_ES;
  case 10:
    return ONION_NOTIFY_LANG_PT_BR;
  case 11:
    return ONION_NOTIFY_LANG_IT;
  case 12:
    return ONION_NOTIFY_LANG_RU;
  case 13:
    return ONION_NOTIFY_LANG_PL;
  case 14:
    return ONION_NOTIFY_LANG_TH;
  case 15:
    return ONION_NOTIFY_LANG_NL;
  case 16:
    return ONION_NOTIFY_LANG_FI;
  case 17:
    return ONION_NOTIFY_LANG_SV;
  case 18:
    return ONION_NOTIFY_LANG_DA;
  case 19:
    return ONION_NOTIFY_LANG_NO;
  case 20:
    return ONION_NOTIFY_LANG_TR;
  case 21:
    return ONION_NOTIFY_LANG_CS;
  case 22:
    return ONION_NOTIFY_LANG_HU;
  case 23:
    return ONION_NOTIFY_LANG_EL;
  case 24:
    return ONION_NOTIFY_LANG_RO;
  case 25:
    return ONION_NOTIFY_LANG_VI;
  case 26:
    return ONION_NOTIFY_LANG_ID;
  case 27:
    return ONION_NOTIFY_LANG_UK;
  case 28:
    return ONION_NOTIFY_LANG_PT_PT;
  default:
    break;
  }

  /* SCE_SYSTEM_SERVICE_PARAM_ID_LANG */
  switch (system_language) {
  case 0: /* Japanese */
    return ONION_NOTIFY_LANG_JA;
  case 2:  /* French */
  case 22: /* French (Canada) */
    return ONION_NOTIFY_LANG_FR;
  case 3:  /* Spanish */
  case 20: /* Spanish (Latin America) */
    return ONION_NOTIFY_LANG_ES;
  case 4: /* German */
    return ONION_NOTIFY_LANG_DE;
  case 5: /* Italian */
    return ONION_NOTIFY_LANG_IT;
  case 6: /* Dutch */
    return ONION_NOTIFY_LANG_NL;
  case 7: /* Portuguese (Portugal) */
    return ONION_NOTIFY_LANG_PT_PT;
  case 17: /* Portuguese (Brazil) */
    return ONION_NOTIFY_LANG_PT_BR;
  case 8: /* Russian */
    return ONION_NOTIFY_LANG_RU;
  case 9: /* Korean */
    return ONION_NOTIFY_LANG_KO;
  case 10: /* Traditional Chinese */
    return ONION_NOTIFY_LANG_ZH_HANT;
  case 11: /* Simplified Chinese */
    return ONION_NOTIFY_LANG_ZH_HANS;
  case 12: /* Finnish */
    return ONION_NOTIFY_LANG_FI;
  case 13: /* Swedish */
    return ONION_NOTIFY_LANG_SV;
  case 14: /* Danish */
    return ONION_NOTIFY_LANG_DA;
  case 15: /* Norwegian */
    return ONION_NOTIFY_LANG_NO;
  case 16: /* Polish */
    return ONION_NOTIFY_LANG_PL;
  case 19: /* Turkish */
    return ONION_NOTIFY_LANG_TR;
  case 21: /* Arabic */
    return ONION_NOTIFY_LANG_AR;
  case 23: /* Czech */
    return ONION_NOTIFY_LANG_CS;
  case 24: /* Hungarian */
    return ONION_NOTIFY_LANG_HU;
  case 25: /* Greek */
    return ONION_NOTIFY_LANG_EL;
  case 26: /* Romanian */
    return ONION_NOTIFY_LANG_RO;
  case 27: /* Thai */
    return ONION_NOTIFY_LANG_TH;
  case 28: /* Vietnamese */
    return ONION_NOTIFY_LANG_VI;
  case 29: /* Indonesian */
    return ONION_NOTIFY_LANG_ID;
  case 30: /* Ukrainian */
    return ONION_NOTIFY_LANG_UK;
  default:
    return ONION_NOTIFY_LANG_EN;
  }
}

void onion_notify_apply_ui_language(int ui_language, int system_language) {
  onion_notify_set_language(
      onion_notify_resolve_language(ui_language, system_language));
}

const char *onion_notify_tr(const char *key) {
  const int idx = locale_index_for_language(onion_notify_get_language());
  size_t i;

  if (!key)
    return "";

  for (i = 0; i < sizeof(kTranslations) / sizeof(kTranslations[0]); ++i) {
    if (strcmp(kTranslations[i].key, key) == 0)
      return kTranslations[i].text[idx];
  }
  return key;
}
