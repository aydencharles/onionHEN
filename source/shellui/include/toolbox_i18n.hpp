/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Lightweight i18n for dynamic toolbox XML.
 */
#pragma once

#include <cstdarg>
#include <string>
#include <string_view>

namespace toolbox_i18n {

/** Runtime language after resolving the stored UI language setting. */
enum class Lang : int {
  ZhHans = 0,
  En = 1,
  Ar = 2,
  ZhHant = 3,
  Ja = 4,
  Fr = 5,
  De = 6,
  Ko = 7,
  Es = 8,
  PtBr = 9,
  It = 10,
  Ru = 11,
  Pl = 12,
  Th = 13,
  Nl = 14,
  Fi = 15,
  Sv = 16,
  Da = 17,
  No = 18,
  Tr = 19,
  Cs = 20,
  Hu = 21,
  El = 22,
  Ro = 23,
  Vi = 24,
  Id = 25,
  Uk = 26,
  PtPt = 27,
};

/** Active language for tr() (from settings or explicit set). */
Lang active_lang();

/** Active resolved language as an explicit UI language setting value. */
int active_ui_lang_value();

/**
 * Apply an explicit UI language setting value.
 * 1=zh-Hans, 2=en, 3=ar, 4=zh-Hant, 5=ja, 6=fr, 7=de, 8=ko, 9=es,
 * 10=pt-BR, 11=it, 12=ru, 13=pl, 14=th, 15=nl, 16=fi, 17=sv, 18=da,
 * 19=no, 20=tr, 21=cs, 22=hu, 23=el, 24=ro, 25=vi, 26=id, 27=uk,
 * 28=pt-PT. Invalid values fall back to zh-Hans.
 */
void apply_ui_lang(int ui_lang);

/**
 * Apply the stored UI language setting.
 * 0=system, 1=zh-Hans, 2=en, 3=ar, 4=zh-Hant, 5=ja, 6=fr, 7=de, 8=ko,
 * 9=es, 10=pt-BR, 11=it, 12=ru, 13=pl, 14=th, 15=nl, 16=fi, 17=sv,
 * 18=da, 19=no, 20=tr, 21=cs, 22=hu, 23=el, 24=ro, 25=vi, 26=id,
 * 27=uk, 28=pt-PT.
 * 0 queries SCE_SYSTEM_SERVICE_PARAM_ID_LANG. Call this only when
 * SystemService can answer (not under PTRACE_AUTHID). A failed query
 * leaves the current language unchanged; XML and notifications then
 * reuse the resolved language.
 */
void apply_system_or_ui_lang(int ui_lang);

/** Override without persisting (tests / temporary). */
void set_lang(Lang lang);

/**
 * Look up a UI string by stable key.
 * Missing key returns the key itself (visible failure).
 */
const char *tr(const char *key);

/** Convenience: tr(key) as std::string. */
inline std::string trs(const char *key) { return tr(key); }

/** Lookup tr(key) and apply printf placeholders. */
std::string formatv(const char *key, va_list ap);
std::string format(const char *key, ...);

} // namespace toolbox_i18n
