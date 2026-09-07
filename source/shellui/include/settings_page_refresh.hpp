#pragma once

#include "ps5_settings_ui.hpp"
#include "monodef.h"

#include <functional>

namespace onion::shellui::settings {

using PageSource = std::function<bool(ps5ui::Node &)>;
// Called while serving initial XML. The source must not navigate or publish.
std::string publish(const ps5ui::Node &model, PageSource source);
void bind(MonoObject *page);
void refresh(MonoObject *page);
void release(MonoObject *page);

} // namespace onion::shellui::settings
