#include "plugin_sprx_pages.hpp"

#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace onion::shellui::plugin_pages {
namespace {

int sprx_sort_rank(const SprxInventoryItem &entry) {
  if (entry.loaded_for_current_target)
    return 0;
  if (entry.configured && entry.matches_current_target && entry.enabled)
    return 1;
  if (entry.configured && entry.matches_current_target)
    return 2;
  if (!entry.configured)
    return 3;
  return 4;
}

std::string join_dependencies(const std::vector<std::string> &deps) {
  std::string out;
  for (const std::string &dep : deps) {
    if (!out.empty())
      out += ", ";
    out += dep;
  }
  return out;
}

} // namespace

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
  if (entry.loaded_for_current_target) {
    if (entry.configured && !entry.enabled)
      return toolbox_i18n::tr("sprx.status.loaded_disabled");
    if (entry.configured && entry.auto_start)
      return toolbox_i18n::tr("sprx.status.loaded_autostart");
    return toolbox_i18n::tr("sprx.status.loaded");
  }
  if (!entry.configured) {
    if (!entry.current_target_title_id.empty())
      return toolbox_i18n::tr("sprx.status.unconfigured_ready");
    return toolbox_i18n::tr("sprx.status.unconfigured");
  }
  if (!entry.enabled) {
    if (entry.matches_current_target)
      return toolbox_i18n::tr("sprx.status.disabled_matches");
    if (!entry.target_summary.empty())
      return toolbox_i18n::format("sprx.status.disabled_target_fmt",
                                  entry.target_summary.c_str());
    return toolbox_i18n::tr("sprx.status.disabled");
  }
  if (entry.matches_current_target) {
    if (entry.auto_start)
      return toolbox_i18n::tr("sprx.status.matches_autostart");
    return toolbox_i18n::tr("sprx.status.matches_manual");
  }
  if (!entry.current_target_title_id.empty()) {
    if (!entry.target_summary.empty())
      return toolbox_i18n::format("sprx.status.other_target_fmt",
                                  entry.target_summary.c_str());
    return toolbox_i18n::tr("sprx.status.inactive");
  }
  if (!entry.target_summary.empty()) {
    return toolbox_i18n::format(entry.auto_start
                                    ? "sprx.status.autostart_target_fmt"
                                    : "sprx.status.manual_target_fmt",
                                entry.target_summary.c_str());
  }
  return toolbox_i18n::tr(entry.auto_start ? "sprx.status.enabled_autostart"
                                           : "sprx.status.enabled");
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
  std::string current_tid;
  for (const SprxInventoryItem &entry : sprx) {
    if (!entry.current_target_title_id.empty()) {
      current_tid = entry.current_target_title_id;
      break;
    }
  }
  if (!current_tid.empty()) {
    page.label("id_external_sprx_target_ctx",
               toolbox_i18n::format("sprx.current_target_fmt",
                                    current_tid.c_str()));
  }

  std::vector<size_t> order(sprx.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const SprxInventoryItem &lhs = sprx[a];
    const SprxInventoryItem &rhs = sprx[b];
    const int rank_l = sprx_sort_rank(lhs);
    const int rank_r = sprx_sort_rank(rhs);
    if (rank_l != rank_r)
      return rank_l < rank_r;
    if (lhs.priority != rhs.priority)
      return lhs.priority > rhs.priority;
    return lhs.id < rhs.id;
  });

  for (size_t idx : order) {
    const SprxInventoryItem &entry = sprx[idx];
    page.link(std::string(kSprxItemPrefix) + entry.id, entry.id,
              toolbox::sprx_config_xml(entry.id), sprx_list_status(entry));
  }
}

void fill_sprx_config(ps5ui::Page &page, const SprxInventoryItem &entry) {
  const bool can_load_now =
      !entry.loaded_for_current_target &&
      !entry.current_target_title_id.empty() &&
      (!entry.configured || entry.matches_current_target);

  if (entry.loaded_for_current_target) {
    page.label("id_external_sprx_status_" + entry.id,
               toolbox_i18n::tr("sprx.status.loaded_detail"));
  } else if (can_load_now) {
    page.button(std::string(kSprxLoadPrefix) + entry.id,
                toolbox_i18n::tr("sprx.load_now"),
                entry.current_target_title_id,
                toolbox_i18n::tr("sprx.load_now.sub"));
  } else {
    page.label("id_external_sprx_status_" + entry.id, sprx_list_status(entry));
  }

  if (entry.configured) {
    page.toggle(std::string(kSprxEnabledPrefix) + entry.id,
                toolbox_i18n::tr("sprx.enabled"), entry.enabled,
                toolbox_i18n::tr("sprx.enabled.sub"))
        .toggle(std::string(kSprxAutoStartPrefix) + entry.id,
                toolbox_i18n::tr("sprx.autostart"), entry.auto_start,
                toolbox_i18n::tr("sprx.autostart.sub"));
  }

  if (!entry.current_target_title_id.empty()) {
    page.button(std::string(kSprxBindCurrentPrefix) + entry.id,
                toolbox_i18n::tr("sprx.bind_current"),
                entry.current_target_title_id,
                toolbox_i18n::tr("sprx.bind_current.sub"));
  }
  page.button(std::string(kSprxBindAllPrefix) + entry.id,
              toolbox_i18n::tr("sprx.bind_all"), std::string("PPSA*, CUSA*"),
              toolbox_i18n::tr("sprx.bind_all.sub"));

  const std::string target_label =
      entry.target_summary.empty() ? toolbox_i18n::tr("sprx.meta.unconfigured")
                                   : entry.target_summary;
  page.label("id_external_sprx_target_" + entry.id,
             toolbox_i18n::format("sprx.meta.target_fmt", target_label.c_str()))
      .label("id_external_sprx_path_" + entry.id,
             toolbox_i18n::format("sprx.meta.path_fmt", entry.path.c_str()));
  if (entry.priority != 0) {
    page.label("id_external_sprx_priority_" + entry.id,
               toolbox_i18n::format("sprx.meta.priority_fmt", entry.priority));
  }
  if (!entry.dependencies.empty()) {
    const std::string deps = join_dependencies(entry.dependencies);
    page.label("id_external_sprx_deps_" + entry.id,
               toolbox_i18n::format("sprx.meta.deps_fmt", deps.c_str()));
  }

  if (entry.configured) {
    page.button(std::string(kSprxDeletePrefix) + entry.id,
                toolbox_i18n::tr("sprx.delete"), std::nullopt,
                toolbox_i18n::tr(entry.loaded_for_current_target
                                     ? "sprx.delete.loaded_sub"
                                     : "sprx.delete.sub"),
                std::nullopt, ps5ui::Style::None,
                toolbox_i18n::tr("sprx.delete.confirm"),
                toolbox_i18n::tr("account.activate.confirm_phrase"));
  }
}

} // namespace onion::shellui::plugin_pages
