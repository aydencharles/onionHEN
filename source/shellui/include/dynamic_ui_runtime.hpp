#pragma once

#include <onion/plugin_ui.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

/** The root settings page exposed by one plugin UI contribution. */
struct PluginSettingsLink {
  std::string plugin_id;
  std::string control_id;
  std::string title;
  std::string resource;
  std::string description;
};

void configure(uint32_t system_version);
void replace_snapshot(plugin_ui::RegistrySnapshot snapshot);
void set_action_sink(ActionSink sink, void *context);
/** Take one immutable lookup table from the current UI snapshot. */
std::vector<PluginSettingsLink> plugin_settings_links();
/** Append settings links not associated with an installed plugin card. */
void append_plugin_links(
    ps5ui::Page &page,
    const std::vector<std::string> &matched_link_ids = {});
void append_plugin_links(
    ps5ui::Page &page,
    const std::vector<PluginSettingsLink> &settings,
    const std::vector<std::string> &matched_link_ids);
bool render_resource(std::string_view resource, std::string &out_xml);
DispatchResult dispatch_control(std::string_view control_id,
                                std::string_view value);
/* Returns true when another page from the same dynamic stack becomes active. */
bool leave_active_page();

} // namespace onion::shellui::dynamic_ui
