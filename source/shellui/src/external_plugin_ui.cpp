#include "external_plugin_ui.hpp"

#include "toolbox_i18n.hpp"

#include <onion/ipc_client.hpp>

#include <optional>
#include <vector>

namespace onion::shellui::external_plugins {
namespace {

constexpr std::string_view kRunPrefix = "id_external_plugin_run_";
constexpr std::string_view kReloadPrefix = "id_external_plugin_reload_";
constexpr std::string_view kDeletePrefix = "id_external_plugin_delete_";

bool split_control(std::string_view control_id, std::string_view prefix,
                   std::string &plugin_id) {
  if (!control_id.starts_with(prefix)) return false;
  const std::string_view suffix = control_id.substr(prefix.size());
  if (suffix.size() != 9) return false;
  plugin_id.assign(suffix);
  return true;
}

} // namespace

void append_inventory(ps5ui::Page &page) {
  std::vector<PluginInventoryItem> plugins;
  const bool loaded = IPC_Client::getInstance(false).ListPlugins(plugins);
  if (loaded && plugins.empty()) return;

  page.group(
      "id_external_plugins", toolbox_i18n::tr("plugins.external.group"),
      [&](ps5ui::Group &group) {
        if (!loaded) {
          group.label("id_external_plugins_unavailable",
                      toolbox_i18n::tr("plugins.external.unavailable"));
          return;
        }
        for (const PluginInventoryItem &plugin : plugins) {
          std::string details = plugin.plugin_id + " | v" + plugin.version;
          if (plugin.auto_start) details += " | AUTO_START";
          group.group(
              "id_external_plugin_" + plugin.plugin_id, plugin.name,
              [&](ps5ui::Group &controls) {
                controls
                    .toggle(std::string(kRunPrefix) + plugin.plugin_id,
                            toolbox_i18n::tr("plugins.external.run"),
                            plugin.running,
                            toolbox_i18n::tr("plugins.external.run.sub"))
                    .button(std::string(kReloadPrefix) + plugin.plugin_id,
                            toolbox_i18n::tr("plugins.external.reload"))
                    .button(
                        std::string(kDeletePrefix) + plugin.plugin_id,
                        toolbox_i18n::tr("plugins.external.delete"),
                        std::nullopt, std::nullopt, std::nullopt,
                        ps5ui::Style::None,
                        toolbox_i18n::tr("plugins.external.delete.confirm"),
                        toolbox_i18n::tr("account.activate.confirm_phrase"));
              },
              details);
        }
      },
      toolbox_i18n::tr("plugins.external.group.sub"));
}

DispatchResult dispatch(std::string_view control_id, std::string_view value) {
  DispatchResult result;
  IPC_Client &client = IPC_Client::getInstance(false);
  if (split_control(control_id, kRunPrefix, result.plugin_id)) {
    result.owned = true;
    const bool start = value == "1" || value == "true";
    result.success = start ? client.StartPlugin(result.plugin_id)
                           : client.StopPlugin(result.plugin_id);
    result.action = start ? Action::Started : Action::Stopped;
    return result;
  }
  if (split_control(control_id, kReloadPrefix, result.plugin_id)) {
    result.owned = true;
    result.success = client.ReloadPlugin(result.plugin_id);
    result.action = Action::Reloaded;
    return result;
  }
  if (split_control(control_id, kDeletePrefix, result.plugin_id)) {
    result.owned = true;
    result.success = client.DeletePlugin(result.plugin_id);
    result.action = Action::Deleted;
  }
  return result;
}

} // namespace onion::shellui::external_plugins
