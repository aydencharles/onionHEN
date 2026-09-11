#include "external_plugin_ui.hpp"

#include "plugin_sprx_pages.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "dynamic_ui_runtime.hpp"
#include "settings_page_refresh.hpp"

#include <onion/ipc_client.hpp>

#include <algorithm>
#include <ranges>
#include <vector>

namespace onion::shellui::external_plugins {
namespace {

using plugin_pages::kPluginAutoStartPrefix;
using plugin_pages::kPluginDeletePrefix;
using plugin_pages::kPluginRunPrefix;
using plugin_pages::split_control_id;

bool refresh_inventory() {
  std::vector<PluginInventoryItem> plugins;
  const bool loaded = IPC_Client::getInstance(false).ListPlugins(plugins);
  if (loaded)
    g_ui.external_plugins = std::move(plugins);
  return loaded;
}

PluginInventoryItem *find_plugin(std::string_view plugin_id) {
  const auto it = std::ranges::find(g_ui.external_plugins, plugin_id,
                                    &PluginInventoryItem::plugin_id);
  return it == g_ui.external_plugins.end() ? nullptr : &*it;
}

} // namespace

InventoryResult append_inventory(
    ps5ui::Page &page,
    const std::vector<dynamic_ui::PluginSettingsLink> &settings) {
  InventoryResult result;
  if (!refresh_inventory()) {
    page.label("id_external_plugins_unavailable",
               toolbox_i18n::tr("plugins.external.unavailable"));
    return result;
  }
  result.available = true;
  if (g_ui.external_plugins.empty())
    return result;

  plugin_pages::append_plugin_list_links(page, g_ui.external_plugins);
  for (const dynamic_ui::PluginSettingsLink &link : settings) {
    if (find_plugin(link.plugin_id))
      result.matched_settings.push_back(link.control_id);
  }
  return result;
}

static bool config_model(ps5ui::Node &model, std::string_view plugin_id) {
  const bool loaded = refresh_inventory();
  const PluginInventoryItem *found = loaded ? find_plugin(plugin_id) : nullptr;

  ps5ui::Page page("id_plugin_config",
                   found ? found->name : std::string(plugin_id));
  if (!loaded) {
    page.label("id_external_plugins_unavailable",
               toolbox_i18n::tr("plugins.external.unavailable"));
  } else if (!found) {
    page.label("id_external_plugin_missing",
               toolbox_i18n::tr("plugins.external.missing"));
  } else {
    const dynamic_ui::PluginSettingsLink *settings = nullptr;
    const auto links = dynamic_ui::plugin_settings_links();
    const auto it = std::ranges::find(links, plugin_id,
                                      &dynamic_ui::PluginSettingsLink::plugin_id);
    if (it != links.end())
      settings = &*it;
    plugin_pages::fill_plugin_config(page, *found, settings);
  }
  model = page.root();
  return loaded;
}

void generate_config_xml(std::string &xml_buffer, std::string_view plugin_id) {
  ps5ui::Node model;
  config_model(model, plugin_id);
  xml_buffer = settings::publish(model, [id = std::string(plugin_id)](ps5ui::Node &next) {
    return config_model(next, id);
  });
}

DispatchResult dispatch(std::string_view control_id, std::string_view value) {
  DispatchResult result;
  IPC_Client &client = IPC_Client::getInstance(false);
  if (split_control_id(control_id, kPluginRunPrefix, result.plugin_id, 9, 9)) {
    result.owned = true;
    const bool start = value == "1" || value == "true";
    result.success = start ? client.StartPlugin(result.plugin_id)
                           : client.StopPlugin(result.plugin_id);
    result.action = start ? Action::Started : Action::Stopped;
    if (result.success)
      if (PluginInventoryItem *plugin = find_plugin(result.plugin_id))
        plugin->running = start;
    return result;
  }
  if (split_control_id(control_id, kPluginDeletePrefix, result.plugin_id, 9,
                       9)) {
    result.owned = true;
    result.success = client.DeletePlugin(result.plugin_id);
    result.action = Action::Deleted;
    if (result.success)
      std::erase_if(g_ui.external_plugins, [&](const PluginInventoryItem &plugin) {
        return plugin.plugin_id == result.plugin_id;
      });
    return result;
  }
  if (split_control_id(control_id, kPluginAutoStartPrefix, result.plugin_id, 9,
                       9)) {
    result.owned = true;
    const bool enabled = value == "1" || value == "true";
    result.success = client.SetPluginAutoStart(result.plugin_id, enabled);
    result.action = Action::AutoStartChanged;
    if (result.success)
      if (PluginInventoryItem *plugin = find_plugin(result.plugin_id))
        plugin->auto_start = enabled;
  }
  return result;
}

} // namespace onion::shellui::external_plugins
