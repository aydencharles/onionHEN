#pragma once

#include "ps5_settings_ui.hpp"

#include <string>
#include <string_view>

namespace onion::shellui::external_sprx {

enum class Action {
  None,
  LoadNow,
  Reloaded,
  EnabledChanged,
  AutoStartChanged,
  BoundCurrent,
  BoundAll,
  Deleted,
};

struct DispatchResult {
  bool owned = false;
  bool success = false;
  Action action = Action::None;
  std::string id;
  std::string error;
  bool was_loaded = false;
  bool toggle_value = false;
};

/** Builds the SPRX list (configured + discovered); SPRX modules cannot unload. */
bool append_inventory(ps5ui::Page &page);
void generate_config_xml(std::string &xml_buffer, std::string_view id);
DispatchResult dispatch(std::string_view control_id, std::string_view value);

} // namespace onion::shellui::external_sprx
