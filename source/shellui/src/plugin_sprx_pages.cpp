#include "plugin_sprx_pages.hpp"

#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

namespace onion::shellui::plugin_pages {

std::string plugin_list_status(bool running, bool auto_start) {
  if (running && auto_start)
    return toolbox_i18n::tr("plugins.external.status.running_autostart");
  if (running)
    return toolbox_i18n::tr("plugins.external.status.running");
  if (auto_start)
    return toolbox_i18n::tr("plugins.external.status.stopped_autostart");
  return toolbox_i18n::tr("plugins.external.status.stopped");
}

std::string sprx_list_status(const SprxInventoryItem &entry) {
  if (entry.loaded_for_current_target)
    return toolbox_i18n::tr("sprx.status.loaded");
  if (entry.matches_current_target)
    return toolbox_i18n::tr("sprx.status.matches");
  return toolbox_i18n::tr("sprx.status.inactive");
}

void append_plugin_list_links(
    ps5ui::Page &page, const std::vector<PluginInventoryItem> &plugins) {
  for (const PluginInventoryItem &plugin : plugins)
    page.link(std::string(kPluginItemPrefix) + plugin.plugin_id, plugin.name,
              toolbox::external_plugin_config_xml(plugin.plugin_id),
              plugin_list_status(plugin.running, plugin.auto_start));
}

void fill_plugin_config(ps5ui::Page &page, const PluginInventoryItem &plugin,
                        const dynamic_ui::PluginSettingsLink *settings) {
  page.toggle(std::string(kPluginRunPrefix) + plugin.plugin_id,
              toolbox_i18n::tr("plugins.external.run"), plugin.running,
              toolbox_i18n::tr("plugins.external.run.sub"))
      .toggle(std::string(kPluginAutoStartPrefix) + plugin.plugin_id,
              toolbox_i18n::tr("plugins.external.autostart"), plugin.auto_start,
              toolbox_i18n::tr("plugins.external.autostart.sub"));
  if (settings)
    page.link("id_external_plugin_settings_" + plugin.plugin_id, settings->title,
              settings->resource,
              settings->description.empty()
                  ? std::optional<std::string>{}
                  : std::optional<std::string>{settings->description});
  page.button(std::string(kPluginDeletePrefix) + plugin.plugin_id,
              toolbox_i18n::tr("plugins.external.delete"), std::nullopt,
              std::nullopt, std::nullopt, ps5ui::Style::None,
              toolbox_i18n::tr("plugins.external.delete.confirm"),
              toolbox_i18n::tr("account.activate.confirm_phrase"));
}

void append_sprx_list_links(ps5ui::Page &page,
                            const std::vector<SprxInventoryItem> &sprx) {
  for (const SprxInventoryItem &entry : sprx)
    page.link(std::string(kSprxItemPrefix) + entry.id, entry.id,
              toolbox::sprx_config_xml(entry.id), sprx_list_status(entry));
}

void fill_sprx_config(ps5ui::Page &page, const SprxInventoryItem &entry) {
  page.label("id_external_sprx_status_" + entry.id, sprx_list_status(entry))
      .label("id_external_sprx_path_" + entry.id, entry.path)
      .toggle(std::string(kSprxEnabledPrefix) + entry.id,
              toolbox_i18n::tr("sprx.enabled"), entry.enabled,
              toolbox_i18n::tr("sprx.enabled.sub"))
      .button(std::string(kSprxDeletePrefix) + entry.id,
              toolbox_i18n::tr("sprx.delete"), std::nullopt,
              toolbox_i18n::tr("sprx.delete.sub"), std::nullopt,
              ps5ui::Style::None, toolbox_i18n::tr("sprx.delete.confirm"));
}

} // namespace onion::shellui::plugin_pages
