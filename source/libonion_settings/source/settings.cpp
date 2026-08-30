/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <onion/settings.hpp>

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// Header-only inih-style parser already used across the tree.
#include <ini.h>

namespace onion {
namespace {

const char *g_last_loaded = "";

bool path_exists(const char *path) {
  struct stat st {};
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool dir_writable_parent(const char *path) {
  // Best-effort: try open with create; callers still check write result.
  (void)path;
  return true;
}

int atoi_def(const char *s, int def) {
  return s ? atoi(s) : def;
}

bool streq_ci(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  while (*a && *b) {
    if (std::tolower(static_cast<unsigned char>(*a)) !=
        std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

const char *ini_get(IniParser *parser, const char *key) {
  return ini_parser_get(parser, key, nullptr);
}

bool parse_bool(const char *s, bool def) {
  if (!s) {
    return def;
  }
  if (streq_ci(s, "true") || streq_ci(s, "yes") || streq_ci(s, "on") ||
      std::strcmp(s, "1") == 0) {
    return true;
  }
  if (streq_ci(s, "false") || streq_ci(s, "no") || streq_ci(s, "off") ||
      std::strcmp(s, "0") == 0) {
    return false;
  }
  return def;
}

int parse_int_range(const char *s, int def, int min, int max) {
  const int v = atoi_def(s, def);
  return v < min || v > max ? def : v;
}

int parse_language(const char *s, int def) {
  if (streq_ci(s, "system")) {
    return kUiLanguageSystem;
  }
  if (streq_ci(s, "zh-Hant") || streq_ci(s, "zh_hant") ||
      streq_ci(s, "zh-TW") || streq_ci(s, "zh_tw") ||
      streq_ci(s, "zh-HK") || streq_ci(s, "zh_hk")) {
    return kUiLanguageZhHant;
  }
  if (streq_ci(s, "zh-Hans") || streq_ci(s, "zh_hans") ||
      streq_ci(s, "zh")) {
    return kUiLanguageZhHans;
  }
  if (streq_ci(s, "en") || streq_ci(s, "english")) {
    return kUiLanguageEn;
  }
  if (streq_ci(s, "ar") || streq_ci(s, "arabic") ||
      streq_ci(s, "ar-SA") || streq_ci(s, "ar_sa")) {
    return kUiLanguageAr;
  }
  if (streq_ci(s, "ja") || streq_ci(s, "japanese") ||
      streq_ci(s, "ja-JP") || streq_ci(s, "ja_jp")) {
    return kUiLanguageJa;
  }
  if (streq_ci(s, "fr") || streq_ci(s, "french") ||
      streq_ci(s, "fr-FR") || streq_ci(s, "fr_fr") ||
      streq_ci(s, "francais") || streq_ci(s, "français")) {
    return kUiLanguageFr;
  }
  if (streq_ci(s, "de") || streq_ci(s, "german") ||
      streq_ci(s, "de-DE") || streq_ci(s, "de_de") ||
      streq_ci(s, "deutsch")) {
    return kUiLanguageDe;
  }
  if (streq_ci(s, "ko") || streq_ci(s, "korean") ||
      streq_ci(s, "ko-KR") || streq_ci(s, "ko_kr")) {
    return kUiLanguageKo;
  }
  if (streq_ci(s, "es") || streq_ci(s, "spanish") ||
      streq_ci(s, "es-ES") || streq_ci(s, "es_es") ||
      streq_ci(s, "es-MX") || streq_ci(s, "es_mx") ||
      streq_ci(s, "es-419") || streq_ci(s, "es_419")) {
    return kUiLanguageEs;
  }
  if (streq_ci(s, "pt-BR") || streq_ci(s, "pt_br") ||
      streq_ci(s, "pt-br") || streq_ci(s, "pt") ||
      streq_ci(s, "portuguese") || streq_ci(s, "pt-PT") ||
      streq_ci(s, "pt_pt")) {
    return kUiLanguagePtBr;
  }
  if (streq_ci(s, "it") || streq_ci(s, "italian") ||
      streq_ci(s, "it-IT") || streq_ci(s, "it_it") ||
      streq_ci(s, "italiano")) {
    return kUiLanguageIt;
  }
  if (streq_ci(s, "ru") || streq_ci(s, "russian") ||
      streq_ci(s, "ru-RU") || streq_ci(s, "ru_ru")) {
    return kUiLanguageRu;
  }
  if (streq_ci(s, "pl") || streq_ci(s, "polish") ||
      streq_ci(s, "pl-PL") || streq_ci(s, "pl_pl") ||
      streq_ci(s, "polski")) {
    return kUiLanguagePl;
  }
  if (streq_ci(s, "th") || streq_ci(s, "thai") ||
      streq_ci(s, "th-TH") || streq_ci(s, "th_th")) {
    return kUiLanguageTh;
  }
  return def;
}

} // namespace

const char *language_name(int v) {
  switch (v) {
  case kUiLanguageZhHans:
    return "zh-Hans";
  case kUiLanguageEn:
    return "en";
  case kUiLanguageAr:
    return "ar";
  case kUiLanguageZhHant:
    return "zh-Hant";
  case kUiLanguageJa:
    return "ja";
  case kUiLanguageFr:
    return "fr";
  case kUiLanguageDe:
    return "de";
  case kUiLanguageKo:
    return "ko";
  case kUiLanguageEs:
    return "es";
  case kUiLanguagePtBr:
    return "pt-BR";
  case kUiLanguageIt:
    return "it";
  case kUiLanguageRu:
    return "ru";
  case kUiLanguagePl:
    return "pl";
  case kUiLanguageTh:
    return "th";
  case kUiLanguageSystem:
  default:
    return "system";
  }
}

namespace {

int parse_startup_open_after_load(const char *s, int def) {
  if (streq_ci(s, "none")) {
    return kStartupOpenNone;
  }
  if (streq_ci(s, "home_menu")) {
    return kStartupOpenHomeMenu;
  }
  return def;
}

const char *startup_open_after_load_name(int v) {
  return v == kStartupOpenHomeMenu ? "home_menu" : "none";
}

int parse_log_level(const char *s, int def) {
  if (streq_ci(s, "off")) return kLogLevelOff;
  if (streq_ci(s, "error")) return kLogLevelError;
  if (streq_ci(s, "warn") || streq_ci(s, "warning")) return kLogLevelWarn;
  if (streq_ci(s, "info")) return kLogLevelInfo;
  if (streq_ci(s, "debug")) return kLogLevelDebug;
  if (streq_ci(s, "trace")) return kLogLevelTrace;
  return def;
}

const char *log_level_name(int v) {
  switch (v) {
  case kLogLevelOff:
    return "off";
  case kLogLevelError:
    return "error";
  case kLogLevelWarn:
    return "warn";
  case kLogLevelDebug:
    return "debug";
  case kLogLevelTrace:
    return "trace";
  case kLogLevelInfo:
  default:
    return "info";
  }
}

bool parse_libhijacker_backend(const char *s, bool def) {
  if (streq_ci(s, "default")) {
    return false;
  }
  if (streq_ci(s, "libhijacker")) {
    return true;
  }
  return def;
}

const char *cheat_backend_name(bool libhijacker) {
  return libhijacker ? "libhijacker" : "default";
}

int parse_cheats_mirror(const char *s, int def) {
  if (streq_ci(s, "auto")) {
    return kCheatsMirrorAuto;
  }
  if (streq_ci(s, "github")) {
    return kCheatsMirrorGithub;
  }
  if (streq_ci(s, "cnb") || streq_ci(s, "cnb.cool")) {
    return kCheatsMirrorCnb;
  }
  return def;
}

const char *cheats_mirror_name(int v) {
  switch (v) {
  case kCheatsMirrorGithub:
    return "github";
  case kCheatsMirrorCnb:
    return "cnb";
  case kCheatsMirrorAuto:
  default:
    return "auto";
  }
}

std::string trim_copy(const std::string &value) {
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }
  return value.substr(first, last - first);
}

bool is_upper_alnum(char c) {
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isdigit(uc) || std::isupper(uc);
}

bool valid_exact_title_id(const std::string &value) {
  if (value.size() != 9) {
    return false;
  }
  for (char c : value) {
    if (!is_upper_alnum(c)) {
      return false;
    }
  }
  return true;
}

bool valid_title_id_prefix(const std::string &value) {
  if (value.empty() || value.size() >= 9) {
    return false;
  }
  for (char c : value) {
    if (!is_upper_alnum(c)) {
      return false;
    }
  }
  return true;
}

template <std::size_t N>
bool parse_allowlist_csv(const char *raw, bool (*validator)(const std::string &),
                         std::array<std::string, N> *values,
                         std::size_t *count) {
  if (!raw || !values || !count) {
    return false;
  }

  const std::string input = trim_copy(raw);
  std::array<std::string, N> parsed{};
  std::size_t parsed_count = 0;

  if (streq_ci(input.c_str(), "none")) {
    *values = std::move(parsed);
    *count = 0;
    return true;
  }
  if (input.empty()) {
    return false;
  }

  std::size_t start = 0;
  while (start <= input.size()) {
    const std::size_t comma = input.find(',', start);
    const std::string token = trim_copy(input.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start));
    if (!validator(token)) {
      return false;
    }

    bool duplicate = false;
    for (std::size_t i = 0; i < parsed_count; ++i) {
      if (parsed[i] == token) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      if (parsed_count >= N) {
        return false;
      }
      parsed[parsed_count++] = token;
    }

    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }

  *values = std::move(parsed);
  *count = parsed_count;
  return true;
}

template <std::size_t N>
std::string serialize_allowlist_csv(const std::array<std::string, N> &values,
                                    std::size_t count) {
  if (count == 0) {
    return "none";
  }
  if (count > N) {
    count = N;
  }

  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0) {
      out += ',';
    }
    out += values[i];
  }
  return out;
}

bool parse_fan_control(const char *s, bool def) {
  if (streq_ci(s, "automatic")) {
    return false;
  }
  if (streq_ci(s, "temperature_threshold")) {
    return true;
  }
  return def;
}

const char *fan_control_name(bool enabled) {
  return enabled ? "temperature_threshold" : "automatic";
}

int parse_overlay_edge(const char *s, int def) {
  if (streq_ci(s, "top")) {
    return 0;
  }
  if (streq_ci(s, "bottom")) {
    return 2;
  }
  return def;
}

const char *overlay_edge_name(int pos) {
  return pos == 2 || pos == 3 ? "bottom" : "top";
}

int parse_overlay_align(const char *s, int def) {
  if (streq_ci(s, "left")) {
    return kOverlayAlignLeft;
  }
  if (streq_ci(s, "center")) {
    return kOverlayAlignCenter;
  }
  if (streq_ci(s, "right")) {
    return kOverlayAlignRight;
  }
  return def;
}

const char *overlay_align_name(int align) {
  if (align == kOverlayAlignLeft) {
    return "left";
  }
  if (align == kOverlayAlignRight) {
    return "right";
  }
  return "center";
}

bool parse_per_core_cpu(const char *s, bool def) {
  if (streq_ci(s, "average")) {
    return false;
  }
  if (streq_ci(s, "per_core")) {
    return true;
  }
  return def;
}

const char *cpu_usage_mode_name(bool per_core) {
  return per_core ? "per_core" : "average";
}

int parse_cheats_shortcut(const char *s, int def) {
  if (streq_ci(s, "off")) {
    return 0;
  }
  if (streq_ci(s, "r3_l3")) {
    return 1;
  }
  if (streq_ci(s, "l2_triangle")) {
    return 2;
  }
  if (streq_ci(s, "long_options")) {
    return 3;
  }
  if (streq_ci(s, "long_share")) {
    return 4;
  }
  if (streq_ci(s, "share")) {
    return 5;
  }
  return def;
}

const char *cheats_shortcut_name(int v) {
  switch (v) {
  case 1:
    return "r3_l3";
  case 2:
    return "l2_triangle";
  case 3:
    return "long_options";
  case 4:
    return "long_share";
  case 5:
    return "share";
  case 0:
  default:
    return "off";
  }
}

int parse_toolbox_shortcut(const char *s, int def) {
  if (streq_ci(s, "off")) {
    return 0;
  }
  if (streq_ci(s, "l2_r3")) {
    return 1;
  }
  if (streq_ci(s, "long_share")) {
    return 2;
  }
  if (streq_ci(s, "share")) {
    return 3;
  }
  return def;
}

const char *toolbox_shortcut_name(int v) {
  switch (v) {
  case 1:
    return "l2_r3";
  case 2:
    return "long_share";
  case 3:
    return "share";
  case 0:
  default:
    return "off";
  }
}

std::string bool_text(bool value) { return value ? "true" : "false"; }

bool apply_parser(IniParser *parser, Settings *out) {
  const int version = atoi_def(ini_get(parser, "meta.schema_version"), -1);
  if (version != kSettingsSchemaVersion) {
    return false;
  }

  out->schema_version = version;
  out->ui_lang =
      parse_language(ini_get(parser, "toolbox.language"), out->ui_lang);
  out->startup_open_after_load = parse_startup_open_after_load(
      ini_get(parser, "startup.open_after_load"),
      out->startup_open_after_load);
  out->log_level =
      parse_log_level(ini_get(parser, "logging.level"), out->log_level);
  out->display_tids = parse_bool(
      ini_get(parser, "home_screen.show_title_ids"), out->display_tids);
  out->onionhen_game_opts =
      parse_bool(ini_get(parser, "game_menu.show_onionhen_options"),
                 out->onionhen_game_opts);
  out->libhijacker_cheats = parse_libhijacker_backend(
      ini_get(parser, "cheats.memory_backend"), out->libhijacker_cheats);
  out->cheats_mirror = parse_cheats_mirror(ini_get(parser, "cheats.mirror"),
                                           out->cheats_mirror);
  out->app_jailbreak_enabled =
      parse_bool(ini_get(parser, "app_jailbreak.enabled"),
                 out->app_jailbreak_enabled);
  out->debug_app_jb_msg =
      parse_bool(ini_get(parser, "app_jailbreak.debug_notifications"),
                 out->debug_app_jb_msg);
  if (const char *exact = ini_get(parser, "app_jailbreak.exact_title_ids")) {
    (void)parse_allowlist_csv(
        exact, valid_exact_title_id,
        &out->app_jailbreak_allowlist.exact_title_ids,
        &out->app_jailbreak_allowlist.exact_title_id_count);
  }
  if (const char *prefixes =
          ini_get(parser, "app_jailbreak.title_id_prefixes")) {
    (void)parse_allowlist_csv(
        prefixes, valid_title_id_prefix,
        &out->app_jailbreak_allowlist.title_id_prefixes,
        &out->app_jailbreak_allowlist.title_id_prefix_count);
  }
  out->enable_fan_speed =
      parse_fan_control(ini_get(parser, "cooling.fan_control"),
                        out->enable_fan_speed);
  out->fan_threshold =
      parse_int_range(ini_get(parser, "cooling.temperature_threshold_celsius"),
                      out->fan_threshold, kFanThresholdMinCelsius,
                      kFanThresholdMaxCelsius);
  out->overlay_enabled =
      parse_bool(ini_get(parser, "overlay.enabled"), out->overlay_enabled);
  out->overlay_background = parse_bool(
      ini_get(parser, "overlay.background"), out->overlay_background);
  out->overlay_pos =
      parse_overlay_edge(ini_get(parser, "overlay.edge"), out->overlay_pos);
  out->overlay_align =
      parse_overlay_align(ini_get(parser, "overlay.align"), out->overlay_align);
  out->overlay_cpu =
      parse_bool(ini_get(parser, "overlay.show_cpu"), out->overlay_cpu);
  out->all_cpu_usage =
      parse_per_core_cpu(ini_get(parser, "overlay.cpu_usage_mode"),
                         out->all_cpu_usage);
  out->overlay_gpu =
      parse_bool(ini_get(parser, "overlay.show_gpu"), out->overlay_gpu);
  out->overlay_fps =
      parse_bool(ini_get(parser, "overlay.show_fps"), out->overlay_fps);
  out->overlay_ram =
      parse_bool(ini_get(parser, "overlay.show_memory"), out->overlay_ram);
  out->overlay_ip = parse_bool(ini_get(parser, "overlay.show_ip_address"),
                               out->overlay_ip);
  out->cheats_shortcut_opt =
      parse_cheats_shortcut(ini_get(parser, "shortcuts.cheats_menu"),
                            out->cheats_shortcut_opt);
  out->toolbox_shortcut_opt =
      parse_toolbox_shortcut(ini_get(parser, "shortcuts.toolbox"),
                             out->toolbox_shortcut_opt);
  out->kstuff_autoload =
      parse_bool(ini_get(parser, "kstuff.autoload"), out->kstuff_autoload);
  out->ftp_autoload =
      parse_bool(ini_get(parser, "ftp.autoload"), out->ftp_autoload);
  out->ftp_port = parse_int_range(ini_get(parser, "ftp.port"), out->ftp_port,
                                  1, 65535);
  return true;
}

bool write_path(const char *path, const std::string &body) {
  if (!path || !dir_writable_parent(path)) {
    return false;
  }
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd < 0) {
    return false;
  }
  ssize_t n = write(fd, body.data(), body.size());
  close(fd);
  return n == static_cast<ssize_t>(body.size());
}

bool try_load_path(const char *path, Settings *out) {
  if (!path_exists(path)) {
    return false;
  }
  /* IniParser is about 51 KiB.  This function also runs on daemon IPC client
     threads, where keeping it on the stack can exhaust the thread stack in
     unoptimised/debug builds. */
  std::unique_ptr<IniParser> parser(new (std::nothrow) IniParser{});
  if (!parser || !ini_parser_load(parser.get(), path)) {
    return false;
  }
  Settings parsed = *out;
  if (!apply_parser(parser.get(), &parsed)) {
    return false;
  }
  *out = parsed;
  g_last_loaded = path;
  return true;
}

} // namespace

std::string settings_serialize(const Settings &in) {
  std::string b;
  b.reserve(4096);
  b += "# OnionHEN configuration\n";
  b += "#\n";
  b += "# This file uses semantic schema version 1.\n";
  b += "# Boolean fields accept true or false.\n";
  b += "# Keep key names lowercase and do not add inline comments after values.\n";
  b += "\n";
  b += "[meta]\n";
  b += "# schema_version identifies this config format.\n";
  b += "# Available values: 1\n";
  b += "schema_version=" + std::to_string(kSettingsSchemaVersion) + "\n";
  b += "\n";
  b += "[toolbox]\n";
  b += "# language controls the Toolbox UI and notification language.\n";
  b += "# Available values: system, zh-Hans, zh-Hant, en, ja, ko, fr, de, it, es, pt-BR, pl, ru, ar, th\n";
  b += "# system follows the PS5 system language when it can be detected.\n";
  b += "language=" + std::string(language_name(in.ui_lang)) + "\n";
  b += "\n";
  b += "[startup]\n";
  b += "# open_after_load chooses which page opens after OnionHEN finishes loading.\n";
  b += "# Available values: none, home_menu\n";
  b += "open_after_load=" +
       std::string(startup_open_after_load_name(in.startup_open_after_load)) +
       "\n";
  b += "\n";
  b += "[logging]\n";
  b += "# level controls how much OnionHEN records to its log files.\n";
  b += "# Available values: off, error, warn, info, debug, trace\n";
  b += "# Raise to debug when reproducing an issue for a bug report.\n";
  b += "# Release builds compile out trace, so trace behaves as debug.\n";
  b += "level=" + std::string(log_level_name(in.log_level)) + "\n";
  b += "\n";
  b += "[home_screen]\n";
  b += "# show_title_ids displays app Title IDs on the PS5 home screen.\n";
  b += "# Available values: true, false\n";
  b += "show_title_ids=" + bool_text(in.display_tids) + "\n";
  b += "\n";
  b += "[game_menu]\n";
  b += "# show_onionhen_options adds OnionHEN entries to the game options menu.\n";
  b += "# Available values: true, false\n";
  b += "show_onionhen_options=" + bool_text(in.onionhen_game_opts) + "\n";
  b += "\n";
  b += "[cheats]\n";
  b += "# memory_backend selects the cheat memory access implementation.\n";
  b += "# Available values: default, libhijacker\n";
  b += "memory_backend=" + std::string(cheat_backend_name(in.libhijacker_cheats)) +
       "\n";
  b += "# mirror selects the git host for online cheat catalogs.\n";
  b += "# Available values: auto, github, cnb\n";
  b += "# auto uses cnb.cool when the UI/system language is zh-Hans, otherwise GitHub.\n";
  b += "mirror=" + std::string(cheats_mirror_name(in.cheats_mirror)) + "\n";
  b += "\n";
  b += "[app_jailbreak]\n";
  b += "# enabled controls the App lifecycle and sandbox event listeners. "
       "When false,\n";
  b += "# OnionHEN does not register either listener.\n";
  b += "# Available values: true, false\n";
  b += "enabled=" + bool_text(in.app_jailbreak_enabled) + "\n";
  b += "# debug_notifications shows a notification when OnionHEN jailbreaks an app.\n";
  b += "# Available values: true, false\n";
  b += "debug_notifications=" + bool_text(in.debug_app_jb_msg) + "\n";
  b += "# exact_title_ids is a comma-separated list of exact 9-character Title IDs.\n";
  b += "# Use none to disable all exact Title IDs; maximum 20 entries.\n";
  b += "exact_title_ids=" +
       serialize_allowlist_csv(
           in.app_jailbreak_allowlist.exact_title_ids,
           in.app_jailbreak_allowlist.exact_title_id_count) +
       "\n";
  b += "# title_id_prefixes is a comma-separated list of uppercase prefixes.\n";
  b += "# Use none to disable prefix matching; bare wildcards are not accepted.\n";
  b += "title_id_prefixes=" +
       serialize_allowlist_csv(
           in.app_jailbreak_allowlist.title_id_prefixes,
           in.app_jailbreak_allowlist.title_id_prefix_count) +
       "\n";
  b += "\n";
  b += "[cooling]\n";
  b += "# fan_control chooses between stock fan behavior and a manual threshold.\n";
  b += "# Available values: automatic, temperature_threshold\n";
  b += "fan_control=" + std::string(fan_control_name(in.enable_fan_speed)) + "\n";
  b += "# temperature_threshold_celsius is used when fan_control is temperature_threshold.\n";
  b += "# Available values: 0 through 100\n";
  b += "temperature_threshold_celsius=" + std::to_string(in.fan_threshold) + "\n";
  b += "\n";
  b += "[overlay]\n";
  b += "# enabled shows or hides the complete game monitor bar.\n";
  b += "# Available values: true, false\n";
  b += "enabled=" + bool_text(in.overlay_enabled) + "\n";
  b += "# background controls the translucent panel behind the monitor bar.\n";
  b += "# Available values: true, false\n";
  b += "background=" + bool_text(in.overlay_background) + "\n";
  b += "# edge chooses the screen edge used by the monitor bar.\n";
  b += "# Available values: top, bottom\n";
  b += "edge=" + std::string(overlay_edge_name(in.overlay_pos)) + "\n";
  b += "# align packs metrics to the left, center, or right of the bar.\n";
  b += "# Available values: left, center, right\n";
  b += "align=" + std::string(overlay_align_name(in.overlay_align)) + "\n";
  b += "# show_cpu displays CPU temperature and usage.\n";
  b += "# Available values: true, false\n";
  b += "show_cpu=" + bool_text(in.overlay_cpu) + "\n";
  b += "# cpu_usage_mode controls whether CPU usage is averaged or per core.\n";
  b += "# Available values: average, per_core\n";
  b += "cpu_usage_mode=" + std::string(cpu_usage_mode_name(in.all_cpu_usage)) +
       "\n";
  b += "# show_gpu displays GPU temperature and usage.\n";
  b += "# Available values: true, false\n";
  b += "show_gpu=" + bool_text(in.overlay_gpu) + "\n";
  b += "# show_fps displays the skip-hook in-game frame rate.\n";
  b += "# Available values: true, false\n";
  b += "show_fps=" + bool_text(in.overlay_fps) + "\n";
  b += "# show_memory displays memory usage.\n";
  b += "# Available values: true, false\n";
  b += "show_memory=" + bool_text(in.overlay_ram) + "\n";
  b += "# show_ip_address displays the console LAN IP address.\n";
  b += "# Available values: true, false\n";
  b += "show_ip_address=" + bool_text(in.overlay_ip) + "\n";
  b += "\n";
  b += "[shortcuts]\n";
  b += "# cheats_menu controls the shortcut that opens the cheats menu.\n";
  b += "# Available values: off, r3_l3, l2_triangle, long_options, long_share, share\n";
  b += "cheats_menu=" + std::string(cheats_shortcut_name(in.cheats_shortcut_opt)) +
       "\n";
  b += "# toolbox controls the shortcut that opens the OnionHEN Toolbox.\n";
  b += "# Available values: off, l2_r3, long_share, share\n";
  b += "toolbox=" + std::string(toolbox_shortcut_name(in.toolbox_shortcut_opt)) +
       "\n";
  b += "\n";
  b += "[kstuff]\n";
  b += "# autoload loads kstuff when OnionHEN starts.\n";
  b += "# Available values: true, false\n";
  b += "autoload=" + bool_text(in.kstuff_autoload) + "\n";
  b += "\n[ftp]\n";
  b += "# autoload starts the built-in FTP server the next time OnionHEN launches.\n";
  b += "# Available values: true, false\n";
  b += "autoload=" + bool_text(in.ftp_autoload) + "\n";
  b += "# port selects the TCP listen port for the built-in server.\n";
  b += "# Available values: 1 through 65535\n";
  b += "port=" + std::to_string(in.ftp_port) + "\n";
  return b;
}

bool settings_load_file(const char *path, Settings *out) {
  if (!out || !path) {
    return false;
  }
  *out = Settings{};
  return try_load_path(path, out);
}

bool settings_save_file(const char *path, const Settings &in) {
  if (!path) {
    return false;
  }
  return write_path(path, settings_serialize(in));
}

bool settings_load(Settings *out) {
  if (!out) {
    return false;
  }
  *out = Settings{}; // defaults
  g_last_loaded = "";

  // Prefer elevated path, then shellui sandbox view.
  if (try_load_path(kConfigPathPrimary, out)) {
    return true;
  }
  if (try_load_path(kConfigPathShellui, out)) {
    return true;
  }
  return false;
}

bool settings_save(const Settings &in) {
  const std::string body = settings_serialize(in);
  bool ok = false;
  if (write_path(kConfigPathPrimary, body)) {
    ok = true;
  }
  if (write_path(kConfigPathShellui, body)) {
    ok = true;
  }
  return ok;
}

bool settings_ensure_default() {
  if (path_exists(kConfigPathPrimary) || path_exists(kConfigPathShellui)) {
    return true;
  }
  Settings def{};
  return settings_save(def);
}

const char *settings_last_loaded_path() { return g_last_loaded; }

static time_t path_mtime(const char *path) {
  if (!path) {
    return 0;
  }
  struct stat st {};
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    return 0;
  }
  return st.st_mtime;
}

time_t settings_config_newest_mtime() {
  const time_t a = path_mtime(kConfigPathPrimary);
  const time_t b = path_mtime(kConfigPathShellui);
  return a > b ? a : b;
}

bool settings_config_is_newer_than(time_t since) {
  const time_t newest = settings_config_newest_mtime();
  if (newest == 0) {
    return false;
  }
  return newest > since;
}

} // namespace onion
