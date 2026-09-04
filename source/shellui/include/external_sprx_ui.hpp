#pragma once

#include "ps5_settings_ui.hpp"

#include <string>
#include <string_view>

namespace onion::shellui::external_sprx {

enum class Action {
  None,
  EnabledChanged,
  Deleted,
};

struct DispatchResult {
  bool owned = false;
  bool success = false;
  Action action = Action::None;
  std::string id;
};

/** Builds the catalog page; SPRX modules intentionally have no stop/unload UI. */
void append_inventory(ps5ui::Page &page);
DispatchResult dispatch(std::string_view control_id, std::string_view value);

} // namespace onion::shellui::external_sprx
