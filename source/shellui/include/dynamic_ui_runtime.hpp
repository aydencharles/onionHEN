#pragma once

#include <onion/plugin_ui.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace ps5ui {
class Page;
}

namespace onion::shellui::dynamic_ui {

using ActionSink = bool (*)(plugin_ui::Handle handle,
                            const plugin_ui::Node &node,
                            std::string_view value, void *context);

enum class DispatchResult {
  NotOwned = 0,
  Accepted,
  Unavailable,
};

void configure(uint32_t system_version);
void replace_snapshot(plugin_ui::RegistrySnapshot snapshot);
void set_action_sink(ActionSink sink, void *context);
void append_plugin_links(ps5ui::Page &page);
bool render_resource(std::string_view resource, std::string &out_xml);
DispatchResult dispatch_control(std::string_view control_id,
                                std::string_view value);
/* Returns true when another page from the same dynamic stack becomes active. */
bool leave_active_page();

} // namespace onion::shellui::dynamic_ui
