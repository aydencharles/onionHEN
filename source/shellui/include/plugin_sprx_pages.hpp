#pragma once

#include "dynamic_ui_runtime.hpp"
#include "ps5_settings_ui.hpp"

#include <onion/ipc_client.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::plugin_pages {

inline constexpr std::string_view kPluginRunPrefix = "id_external_plugin_run_";
inline constexpr std::string_view kPluginDeletePrefix =
    "id_external_plugin_delete_";
inline constexpr std::string_view kPluginAutoStartPrefix =
    "id_external_plugin_autostart_";
inline constexpr std::string_view kPluginItemPrefix =
    "id_external_plugin_item_";
inline constexpr std::string_view kSprxEnabledPrefix =
    "id_external_sprx_enabled_";
inline constexpr std::string_view kSprxDeletePrefix = "id_external_sprx_delete_";
inline constexpr std::string_view kSprxItemPrefix = "id_external_sprx_item_";

inline bool split_control_id(std::string_view control_id,
                             std::string_view prefix, std::string &id,
                             std::size_t min_len, std::size_t max_len) {
  if (!control_id.starts_with(prefix))
    return false;
  const std::string_view suffix = control_id.substr(prefix.size());
  if (suffix.size() < min_len || suffix.size() > max_len)
    return false;
  id.assign(suffix);
  return true;
}

std::string plugin_list_status(bool running, bool auto_start);
std::string sprx_list_status(const SprxInventoryItem &entry);

void append_plugin_list_links(
    ps5ui::Page &page, const std::vector<PluginInventoryItem> &plugins);
void fill_plugin_config(ps5ui::Page &page, const PluginInventoryItem &plugin,
                        const dynamic_ui::PluginSettingsLink *settings);

void append_sprx_list_links(ps5ui::Page &page,
                            const std::vector<SprxInventoryItem> &sprx);
void fill_sprx_config(ps5ui::Page &page, const SprxInventoryItem &entry);

} // namespace onion::shellui::plugin_pages
