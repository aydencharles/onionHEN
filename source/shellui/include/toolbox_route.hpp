/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure resource-name → toolbox page routing (host-testable, no Mono/PS5).
 * Used by GetManifestResourceStream_Hook.
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace toolbox {

/** Which dynamic (or special) page to serve for a Legacy settings resource. */
enum class Page : unsigned char {
  None = 0,           /**< unknown → original stream */
  DebugSettings,      /**< embedded toolbox XML */
  Payloads,
  Cheats,
  AutoPayloads,
  Account,
  Plapps,
  CheatProgress,
  RemotePlay,
  SuperuserPass,      /**< recognized; still use original stream */
  RedirectOgDebug,    /**< og_debug.xml → debug_settings resource */
};

struct ResourceNames {
  std::string_view payloads_xml;
  std::string_view debug_settings_xml;
  std::string_view cheats_xml;
};

struct RouteInput {
  std::string_view resource;
  ResourceNames names;
  bool cheats_shortcut = false;
  bool cheats_shortcut_not_open = false;
};

/** Flag snapshot after matching resource. */
struct RouteFlags {
  bool is_payloads = false;
  bool is_su_menu = false;
  bool is_debug_settings = false;
  bool is_cheats = false;
  bool is_auto_payload = false;
  bool is_account = false;
  bool is_plapps = false;
  bool is_cheat_progress = false;
};

struct RouteResult {
  Page page = Page::None;
  RouteFlags flags{};
  bool shortcut_forced_cheats = false;
  bool clear_cheat_shortcuts_after = false;
};

RouteResult resolve_resource(const RouteInput &in);

/** Fixed Legacy resource paths (Sony Settings.Plugins module name is fixed). */
inline constexpr std::string_view kAutoPayloadsXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.auto_payloads.xml";
inline constexpr std::string_view kAccountXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.account.xml";
inline constexpr std::string_view kPlappsXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.plapps.xml";
inline constexpr std::string_view kCheatProgressXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.cheat_progress.xml";
inline constexpr std::string_view kRemotePlayXml =
  "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.remote_play.xml";
inline constexpr std::string_view kSuperuserXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.superuser.xml";
inline constexpr std::string_view kOgDebugXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.og_debug.xml";

// ---- Cheat map helpers (host-testable) ----

constexpr std::size_t kCheatMapSize = 256;

inline bool reset_cheat_map_if_tid_changed(std::string &current_tid, int *map,
                                           std::size_t map_n,
                                           std::string_view new_tid) {
  if (!map || map_n == 0)
    return false;
  if (current_tid == new_tid)
    return false;
  current_tid.assign(new_tid);
  std::memset(map, 0, map_n * sizeof(int));
  return true;
}

inline void set_cheat_enabled(int *map, std::size_t map_n, int cheat_id,
                              bool enabled) {
  if (!map || cheat_id < 0 || static_cast<std::size_t>(cheat_id) >= map_n)
    return;
  map[cheat_id] = enabled ? 1 : 0;
}

inline bool get_cheat_enabled(const int *map, std::size_t map_n, int cheat_id) {
  if (!map || cheat_id < 0 || static_cast<std::size_t>(cheat_id) >= map_n)
    return false;
  return map[cheat_id] != 0;
}

} // namespace toolbox
