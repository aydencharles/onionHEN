/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure resource-name → toolbox page routing (host-testable, no Mono/PS5).
 * Used by GetManifestResourceStream_Hook.
 */
#pragma once

#include <string>
#include <string_view>

namespace toolbox {

/** Which dynamic (or special) page to serve for a Legacy settings resource. */
enum class Page : unsigned char {
  None = 0,           /**< unknown → original stream */
  DebugSettings,      /**< embedded toolbox XML */
  Payloads,
  PayloadConfig,      /**< per-Payload configuration page */
  Sprx,
  SprxConfig,         /**< per-module SPRX catalog page (sprx_<id>.xml) */
  Plugins,            /**< built-in and externally discovered plugins */
  PluginConfig,       /**< per-plugin configuration page (kstuff.xml / plugin_<id>.xml) */
  Cheats,
  AutoPayloads,
  Account,
  CheatProgress,
  RemotePlay,
  DynamicPlugin,     /**< SDK-provided UI contribution page */
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
  bool is_payload_config = false;
  bool is_plugins = false;
  bool is_sprx = false;
  bool is_sprx_config = false;
  bool is_plugin_config = false;
  bool is_su_menu = false;
  bool is_debug_settings = false;
  bool is_cheats = false;
  bool is_auto_payload = false;
  bool is_account = false;
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
         page == Page::PayloadConfig || page == Page::PluginConfig ||
         page == Page::SprxConfig ||
         page == Page::DynamicPlugin;
}

/** Fixed Legacy resource paths (Sony Settings.Plugins module name is fixed). */
inline constexpr std::string_view kAutoPayloadsXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.auto_payloads.xml";
inline constexpr std::string_view kPluginsXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.plugins.xml";
inline constexpr std::string_view kSprxXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.sprx.xml";
inline constexpr std::string_view kAccountXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.account.xml";
inline constexpr std::string_view kCheatProgressXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.cheat_progress.xml";
inline constexpr std::string_view kRemotePlayXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.remote_play.xml";
inline constexpr std::string_view kSuperuserXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.superuser.xml";
inline constexpr std::string_view kOgDebugXml =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.og_debug.xml";

/** Sony Settings.Plugins module prefix shared by toolbox child resources. */
inline constexpr std::string_view kSettingsPluginsPrefix =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.";
inline constexpr std::string_view kExternalPluginXmlPrefix = "plugin_";
inline constexpr std::string_view kSprxConfigXmlPrefix = "sprx_";
inline constexpr std::string_view kPayloadConfigXmlPrefix = "payload_";

inline bool settings_plugins_relative(std::string_view resource,
                                      std::string_view *out) {
  if (!resource.starts_with(kSettingsPluginsPrefix))
    return false;
  const std::string_view relative =
      resource.substr(kSettingsPluginsPrefix.size());
  if (relative.empty())
    return false;
  if (out)
    *out = relative;
  return true;
}

inline bool valid_external_plugin_id(std::string_view id) {
  if (id.size() != 9)
    return false;
  for (char c : id) {
    const auto uc = static_cast<unsigned char>(c);
    const bool letter = (uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z');
    const bool digit = uc >= '0' && uc <= '9';
    if (!letter && !digit)
      return false;
  }
  return true;
}

inline bool valid_sprx_config_id(std::string_view id) {
  if (id.empty() || id.size() >= 32)
    return false;
  for (char c : id) {
    const auto uc = static_cast<unsigned char>(c);
    const bool letter = (uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z');
    const bool digit = uc >= '0' && uc <= '9';
    if (!letter && !digit && uc != '_' && uc != '-' && uc != '.')
      return false;
  }
  return true;
}

inline bool valid_payload_config_id(std::string_view id) {
  if (id.size() != 17 || id[0] != 'p')
    return false;
  for (char c : id.substr(1)) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower_hex = c >= 'a' && c <= 'f';
    if (!digit && !lower_hex)
      return false;
  }
  return true;
}

inline bool parse_prefixed_xml(std::string_view relative, std::string_view prefix,
                               std::string *out_id) {
  constexpr std::string_view kXml = ".xml";
  if (!relative.starts_with(prefix) || !relative.ends_with(kXml))
    return false;
  const std::string_view id = relative.substr(
      prefix.size(), relative.size() - prefix.size() - kXml.size());
  if (id.empty())
    return false;
  if (out_id)
    *out_id = std::string(id);
  return true;
}

inline bool parse_external_plugin_config_resource(std::string_view resource,
                                                  std::string *out_id) {
  std::string_view relative;
  if (!settings_plugins_relative(resource, &relative))
    return false;
  std::string id;
  if (!parse_prefixed_xml(relative, kExternalPluginXmlPrefix, &id) ||
      !valid_external_plugin_id(id))
    return false;
  if (out_id)
    *out_id = std::move(id);
  return true;
}

inline bool parse_sprx_config_resource(std::string_view resource,
                                       std::string *out_id) {
  std::string_view relative;
  if (!settings_plugins_relative(resource, &relative))
    return false;
  std::string id;
  if (!parse_prefixed_xml(relative, kSprxConfigXmlPrefix, &id) ||
      !valid_sprx_config_id(id))
    return false;
  if (out_id)
    *out_id = std::move(id);
  return true;
}

inline bool parse_payload_config_resource(std::string_view resource,
                                          std::string *out_id) {
  std::string_view relative;
  if (!settings_plugins_relative(resource, &relative))
    return false;
  std::string id;
  if (!parse_prefixed_xml(relative, kPayloadConfigXmlPrefix, &id) ||
      !valid_payload_config_id(id))
    return false;
  if (out_id)
    *out_id = std::move(id);
  return true;
}

inline std::string external_plugin_config_xml(std::string_view plugin_id) {
  return std::string(kExternalPluginXmlPrefix) + std::string(plugin_id) + ".xml";
}

inline std::string sprx_config_xml(std::string_view id) {
  return std::string(kSprxConfigXmlPrefix) + std::string(id) + ".xml";
}

inline std::string payload_config_xml(std::string_view id) {
  return std::string(kPayloadConfigXmlPrefix) + std::string(id) + ".xml";
}

} // namespace toolbox
