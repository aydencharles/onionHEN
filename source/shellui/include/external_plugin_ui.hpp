#pragma once

#include "ps5_settings_ui.hpp"
#include "dynamic_ui_runtime.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::external_plugins {

enum class Action {
  None,
  Started,
  Stopped,
  Deleted,
  AutoStartChanged,
};

struct DispatchResult {
  bool owned = false;
  bool success = false;
  Action action = Action::None;
  std::string plugin_id;
};

struct InventoryResult {
  bool available = false;
  std::vector<std::string> matched_settings;
};

/** Append inventory links and report data availability separately from display content. */
InventoryResult append_inventory(
    ps5ui::Page &page,
    const std::vector<dynamic_ui::PluginSettingsLink> &settings);
void generate_config_xml(std::string &xml_buffer, std::string_view plugin_id);
DispatchResult dispatch(std::string_view control_id, std::string_view value);

} // namespace onion::shellui::external_plugins
