/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure resource-name → toolbox page routing (host-testable, no Mono/PS5).
 * Used by GetManifestResourceStream_Hook.
 */
#pragma once

#include <string_view>

namespace toolbox {

/** Which dynamic (or special) page to serve for a Legacy settings resource. */
enum class Page : unsigned char {
  None = 0,           /**< unknown → original stream */
  DebugSettings,      /**< embedded toolbox XML */
  Payloads,
  Plugins,            /**< built-in plugins (kstuff/ftpsrv) */
  PluginConfig,       /**< per-plugin configuration page (plugin_config.xml) */
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
  bool is_plugins = false;
  bool is_plugin_config = false;
  bool is_su_menu = false;
  bool is_debug_settings = false;
  bool is_cheats = false;
  bool is_auto_payload = false;
  bool is_account = false;
  bool is_plapps = false;
  bool is_cheat_progress = false;
  bool is_remote_play = false;
};

struct RouteResult {
  Page page = Page::None;
  RouteFlags flags{};
  bool shortcut_forced_cheats = false;
  bool clear_cheat_shortcuts_after = false;
};

RouteResult resolve_resource(const RouteInput &in);

/** Child routes whose page-stack pop should restore the owning parent route. */
constexpr bool restores_parent_on_pop(Page page) {
  return page == Page::CheatProgress || page == Page::RemotePlay ||
         page == Page::PluginConfig;
}

/** Fixed Legacy resource paths (Sony Settings.Plugins module name is fixed). */
inline constexpr std::string_view kAutoPayloadsXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.auto_payloads.xml";
inline constexpr std::string_view kPluginsXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.plugins.xml";
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

} // namespace toolbox
