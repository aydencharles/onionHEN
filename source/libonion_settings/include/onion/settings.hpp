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

#pragma once

// ---------------------------------------------------------------------------
// Single product config schema for daemon / util / shellui.
//
// Paths:
//   primary  /data/OnionHEN/config.ini          (elevated daemons)
//   shellui  /user/data/OnionHEN/config.ini     (SceShellUI sandbox view)
// Load tries both; save writes all writable targets so the files stay twins.
//
// Process-local store: use SettingsStore for multi-threaded daemons (snapshot
// under mutex). Multi-process copies of Settings are expected and OK.
//
// Force C++ linkage even when a parent includes us under extern "C"
// (e.g. util common_utils.h inside extern "C" blocks).
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C++" {

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <utility>

namespace onion {

// Canonical filesystem paths.
inline constexpr const char *kConfigPathPrimary = "/data/OnionHEN/config.ini";
inline constexpr const char *kConfigPathShellui = "/user/data/OnionHEN/config.ini";

// Semantic config schema. This schema starts at version 1.
inline constexpr int kSettingsSchemaVersion = 1;

inline constexpr int kUiLanguageSystem = 0;
inline constexpr int kUiLanguageZhHans = 1;
inline constexpr int kUiLanguageEn = 2;
inline constexpr int kUiLanguageAr = 3;
inline constexpr int kUiLanguageZhHant = 4;
inline constexpr int kUiLanguageJa = 5;
inline constexpr int kUiLanguageFr = 6;
inline constexpr int kUiLanguageDe = 7;
inline constexpr int kUiLanguageKo = 8;
inline constexpr int kUiLanguageEs = 9;
inline constexpr int kUiLanguagePtBr = 10;
inline constexpr int kUiLanguageIt = 11;
inline constexpr int kUiLanguageRu = 12;
inline constexpr int kUiLanguagePl = 13;
inline constexpr int kUiLanguageTh = 14;

inline constexpr int kStartupOpenNone = 0;
inline constexpr int kStartupOpenHomeMenu = 1;

// Mirrors onion_log_level from <onion/log.h>. Duplicated rather than included
// so libonion_settings stays independent of libonion_platform; the processes
// that bridge the two (see onion_apply_log_settings) static_assert that the
// values still line up.
inline constexpr int kLogLevelOff = 0;
inline constexpr int kLogLevelError = 1;
inline constexpr int kLogLevelWarn = 2;
inline constexpr int kLogLevelInfo = 3;
inline constexpr int kLogLevelDebug = 4;
inline constexpr int kLogLevelTrace = 5;

inline constexpr int kFanThresholdMinCelsius = 0;
inline constexpr int kFanThresholdMaxCelsius = 100;
inline constexpr int kFanAutomaticThresholdCelsius = 77;

inline constexpr int clamp_fan_threshold(int celsius) {
  if (celsius < kFanThresholdMinCelsius)
    return kFanThresholdMinCelsius;
  if (celsius > kFanThresholdMaxCelsius)
    return kFanThresholdMaxCelsius;
  return celsius;
}

inline constexpr std::size_t kMaxAppJailbreakExactTitleIds = 20;
inline constexpr std::size_t kMaxAppJailbreakTitleIdPrefixes = 20;

struct AppJailbreakAllowlist {
  std::array<std::string, kMaxAppJailbreakExactTitleIds> exact_title_ids = {
      "ITEM00001", "NPXS39041", "PKGI13337", "PKGI12345", "TOOL00001"};
  std::size_t exact_title_id_count = 5;

  std::array<std::string, kMaxAppJailbreakTitleIdPrefixes>
      title_id_prefixes = {"LAPY"};
  std::size_t title_id_prefix_count = 1;
};

struct Settings {
  // [startup]
  // Page to open after OnionHEN finishes loading.
  int startup_open_after_load = kStartupOpenNone;

  // [rest_mode]
  uint64_t rest_mode_delay_seconds = 10;

  // [cheats], [app_jailbreak]
  bool libhijacker_cheats = false;
  bool app_jailbreak_enabled = true;
  bool debug_app_jb_msg = false;
  AppJailbreakAllowlist app_jailbreak_allowlist{};

  // [home_screen], [game_menu]
  bool display_tids = false;
  bool onionhen_game_opts = true;

  // [cooling]
  bool enable_fan_speed = false;
  int fan_threshold = kFanAutomaticThresholdCelsius;

  // [overlay]
  /** Master visibility switch for the complete ShellUI game monitor bar. */
  bool overlay_enabled = true;
  /** Show the translucent background panel behind the game monitor bar. */
  bool overlay_background = true;
  bool overlay_ram = true;
  bool overlay_cpu = true;
  bool overlay_gpu = true;
  bool overlay_ip = false;
  /** Per-core CPU usage mode on the overlay (id_all_cpu_usage). */
  bool all_cpu_usage = false;
  int overlay_pos = 0; // 0/1 top edge, 2/3 bottom edge

  // [shortcuts]
  int cheats_shortcut_opt = 0;
  int toolbox_shortcut_opt = 0;

  // [kstuff]
  // Load the embedded/override kstuff payload when OnionHEN starts.
  bool kstuff_autoload = true;

  // [toolbox]
  // 0 = system (default), 1 = zh-Hans, 2 = en, 3 = ar, 4 = zh-Hant,
  // 5 = ja, 6 = fr, 7 = de, 8 = ko, 9 = es, 10 = pt-BR, 11 = it,
  // 12 = ru, 13 = pl
  int ui_lang = kUiLanguageSystem;

  // [logging]
  // Runtime log threshold, matching onion_log_level (1=error .. 5=trace).
  // Lets a user raise verbosity for a bug report without a debug payload;
  // levels stripped at compile time cannot be re-enabled this way.
  int log_level = kLogLevelInfo;

  // Meta
  int schema_version = kSettingsSchemaVersion;
};

// Thread-safe process-local settings (daemon / util IPC + worker threads).
// ShellUI may keep a plain Settings if UI work is single-threaded; prefer this
// store when readers and writers can race.
class SettingsStore {
public:
  SettingsStore() = default;
  explicit SettingsStore(const Settings &s) : s_(s) {}

  Settings snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return s_;
  }

  void store(const Settings &s) {
    std::lock_guard<std::mutex> lock(mu_);
    s_ = s;
  }

  /** Mutate under lock; returns a copy of the new value. */
  template <typename Fn>
  Settings update(Fn &&fn) {
    std::lock_guard<std::mutex> lock(mu_);
    fn(s_);
    return s_;
  }

private:
  mutable std::mutex mu_;
  Settings s_{};
};

// Fill `out` with defaults then overlay values from the first readable config path.
// Returns true if a file was loaded; false means defaults only (file missing).
bool settings_load(Settings *out);

// Load/save a single explicit path (host tests and tooling).
bool settings_load_file(const char *path, Settings *out);
bool settings_save_file(const char *path, const Settings &in);

// Canonical toolbox.language token for a stored ui_lang value.
const char *language_name(int ui_lang);

// Serialize full schema to annotated INI text (no I/O).
std::string settings_serialize(const Settings &in);

// Serialize full schema. Writes every path that can be opened for write.
// Returns true if at least one write succeeded.
bool settings_save(const Settings &in);

// Create default full-schema INI if no config exists on any path.
// Returns true if a file was created (or already existed after load attempt).
bool settings_ensure_default();

// Path that was last successfully loaded (empty if defaults only).
const char *settings_last_loaded_path();

/**
 * Max st_mtime across both twin config paths (0 if neither exists).
 * Use for daemon reload gating so either path's write invalidates the cache.
 */
time_t settings_config_newest_mtime();

/**
 * True if any twin path has mtime strictly greater than `since`.
 * When no config file exists, returns false (nothing newer on disk).
 */
bool settings_config_is_newer_than(time_t since);

} // namespace onion

} // extern "C++"
#endif /* __cplusplus */
