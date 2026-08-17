/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "toolbox_i18n.hpp"
#include <onion/notify_i18n.h>

#ifndef ONION_HOST_TEST
#include "external_symbols.hpp"
#endif

#include <cstdio>
#include <cstring>

namespace toolbox_i18n {
namespace {

#include "toolbox_i18n_catalog.inc"

const Entry *find_entry(const char *key) {
  if (!key)
    return nullptr;
  for (const Entry &e : kTable) {
    if (std::strcmp(e.key, key) == 0)
      return &e;
  }
  return nullptr;
}

Lang lang_from_notify(onion_notify_language_t language) {
  switch (language) {
  case ONION_NOTIFY_LANG_ZH_HANS:
    return Lang::ZhHans;
  case ONION_NOTIFY_LANG_AR:
    return Lang::Ar;
  case ONION_NOTIFY_LANG_ZH_HANT:
    return Lang::ZhHant;
  case ONION_NOTIFY_LANG_JA:
    return Lang::Ja;
  case ONION_NOTIFY_LANG_FR:
    return Lang::Fr;
  case ONION_NOTIFY_LANG_DE:
    return Lang::De;
  case ONION_NOTIFY_LANG_KO:
    return Lang::Ko;
  case ONION_NOTIFY_LANG_ES:
    return Lang::Es;
  case ONION_NOTIFY_LANG_PT_BR:
    return Lang::PtBr;
  case ONION_NOTIFY_LANG_IT:
    return Lang::It;
  case ONION_NOTIFY_LANG_RU:
    return Lang::Ru;
  case ONION_NOTIFY_LANG_PL:
    return Lang::Pl;
  case ONION_NOTIFY_LANG_TH:
    return Lang::Th;
  case ONION_NOTIFY_LANG_EN:
  default:
    return Lang::En;
  }
}

onion_notify_language_t notify_from_lang(Lang lang) {
  switch (lang) {
  case Lang::ZhHans:
    return ONION_NOTIFY_LANG_ZH_HANS;
  case Lang::Ar:
    return ONION_NOTIFY_LANG_AR;
  case Lang::ZhHant:
    return ONION_NOTIFY_LANG_ZH_HANT;
  case Lang::Ja:
    return ONION_NOTIFY_LANG_JA;
  case Lang::Fr:
    return ONION_NOTIFY_LANG_FR;
  case Lang::De:
    return ONION_NOTIFY_LANG_DE;
  case Lang::Ko:
    return ONION_NOTIFY_LANG_KO;
  case Lang::Es:
    return ONION_NOTIFY_LANG_ES;
  case Lang::PtBr:
    return ONION_NOTIFY_LANG_PT_BR;
  case Lang::It:
    return ONION_NOTIFY_LANG_IT;
  case Lang::Ru:
    return ONION_NOTIFY_LANG_RU;
  case Lang::Pl:
    return ONION_NOTIFY_LANG_PL;
  case Lang::Th:
    return ONION_NOTIFY_LANG_TH;
  case Lang::En:
  default:
    return ONION_NOTIFY_LANG_EN;
  }
}

Lang lang_from_ui_value(int ui_lang) {
  switch (ui_lang) {
  case 2:
    return Lang::En;
  case 3:
    return Lang::Ar;
  case 4:
    return Lang::ZhHant;
  case 5:
    return Lang::Ja;
  case 6:
    return Lang::Fr;
  case 7:
    return Lang::De;
  case 8:
    return Lang::Ko;
  case 9:
    return Lang::Es;
  case 10:
    return Lang::PtBr;
  case 11:
    return Lang::It;
  case 12:
    return Lang::Ru;
  case 13:
    return Lang::Pl;
  case 14:
    return Lang::Th;
  case 1:
  default:
    return Lang::ZhHans;
  }
}

const char *locale_id_for_lang(Lang lang) {
  switch (lang) {
  case Lang::ZhHans:
    return "zh-Hans";
  case Lang::Ar:
    return "ar";
  case Lang::ZhHant:
    return "zh-Hant";
  case Lang::Ja:
    return "ja";
  case Lang::Fr:
    return "fr";
  case Lang::De:
    return "de";
  case Lang::Ko:
    return "ko";
  case Lang::Es:
    return "es";
  case Lang::PtBr:
    return "pt-BR";
  case Lang::It:
    return "it";
  case Lang::Ru:
    return "ru";
  case Lang::Pl:
    return "pl";
  case Lang::Th:
    return "th";
  case Lang::En:
  default:
    return "en";
  }
}

int locale_index_for_lang(Lang lang) {
  const char *id = locale_id_for_lang(lang);
  for (int i = 0; i < I18N_LOCALE_COUNT; ++i) {
    if (std::strcmp(kI18nLocaleIds[i], id) == 0)
      return i;
  }
  return kI18nLocaleFallback;
}

} // namespace

Lang active_lang() { return lang_from_notify(onion_notify_get_language()); }

int active_ui_lang_value() {
  switch (active_lang()) {
  case Lang::En:
    return 2;
  case Lang::Ar:
    return 3;
  case Lang::ZhHant:
    return 4;
  case Lang::Ja:
    return 5;
  case Lang::Fr:
    return 6;
  case Lang::De:
    return 7;
  case Lang::Ko:
    return 8;
  case Lang::Es:
    return 9;
  case Lang::PtBr:
    return 10;
  case Lang::It:
    return 11;
  case Lang::Ru:
    return 12;
  case Lang::Pl:
    return 13;
  case Lang::Th:
    return 14;
  case Lang::ZhHans:
  default:
    return 1;
  }
}

void set_lang(Lang lang) {
  if (static_cast<int>(lang) < static_cast<int>(Lang::ZhHans) ||
      lang > Lang::Th)
    lang = Lang::ZhHans;
  onion_notify_set_language(notify_from_lang(lang));
}

void apply_ui_lang(int ui_lang) {
  set_lang(lang_from_ui_value(ui_lang));
}

void apply_system_or_ui_lang(int ui_lang) {
  if (ui_lang != 0) {
    set_lang(lang_from_ui_value(ui_lang));
    return;
  }

  int system_language = 1;
#ifndef ONION_HOST_TEST
  if (sceSystemServiceParamGetInt)
    (void)sceSystemServiceParamGetInt(1, &system_language);
#endif
  set_lang(lang_from_notify(onion_notify_resolve_language(0, system_language)));
}

const char *tr(const char *key) {
  const Entry *e = find_entry(key);
  if (!e)
    return key ? key : "";
  return e->text[locale_index_for_lang(active_lang())];
}

std::string formatv(const char *key, va_list ap) {
  const char *fmt = tr(key);
  va_list measure;
  va_copy(measure, ap);
  const int needed = std::vsnprintf(nullptr, 0, fmt, measure);
  va_end(measure);
  if (needed < 0)
    return {};
  std::string out(static_cast<size_t>(needed), '\0');
  std::vsnprintf(out.data(), static_cast<size_t>(needed) + 1, fmt, ap);
  return out;
}

std::string format(const char *key, ...) {
  va_list ap;
  va_start(ap, key);
  std::string out = formatv(key, ap);
  va_end(ap);
  return out;
}

} // namespace toolbox_i18n
