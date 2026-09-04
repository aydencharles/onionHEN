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

/** Append installed plugin cards and return link IDs whose settings were embedded. */
std::vector<std::string> append_inventory(
    ps5ui::Page &page,
    const std::vector<dynamic_ui::PluginSettingsLink> &settings);
DispatchResult dispatch(std::string_view control_id, std::string_view value);

} // namespace onion::shellui::external_plugins
