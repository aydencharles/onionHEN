/* Copyright (C) 2025 OnionHEN / LightningMods - OnPress built-in plugins */

#include "onpress.hpp"
#include "shellui_payload_state.hpp"
#include "external_plugin_ui.hpp"
#include "plugin_progress.hpp"
#include "plugin_sprx_pages.hpp"
#include "toolbox_i18n.hpp"

namespace {

OnPressResult external_plugin_control(OnPressContext &ctx) {
  ctx.dirty = false;
  std::string plugin_id;
  if (onion::shellui::plugin_pages::split_control_id(
          ctx.id, onion::shellui::plugin_pages::kPluginRunPrefix, plugin_id, 9, 9)) {
    const bool start = ctx.value == "1" || ctx.value == "true";
    plugin_progress_show(plugin_id, start);
    if (!plugin_progress_open_page()) {
      notify("plugins.external.operation_failed_fmt", plugin_id.c_str());
    }
    return OnPressResult::Consumed;
  }

  const onion::shellui::external_plugins::DispatchResult result =
      onion::shellui::external_plugins::dispatch(ctx.id, ctx.value);
  if (!result.owned) return OnPressResult::NotMine;

  if (result.action ==
          onion::shellui::external_plugins::Action::AutoStartChanged &&
      result.success)
    return OnPressResult::Consumed;

  const char *key = "plugins.external.operation_failed_fmt";
  if (result.success) {
    using onion::shellui::external_plugins::Action;
    switch (result.action) {
    case Action::Started:
      key = "plugins.external.started_fmt";
      break;
    case Action::Stopped:
      key = "plugins.external.stopped_fmt";
      break;
    case Action::Deleted:
      key = "plugins.external.deleted_fmt";
      break;
    case Action::AutoStartChanged:
      break;
    case Action::None:
      break;
    }
  }
  const std::string message = toolbox_i18n::format(key, result.plugin_id.c_str());
  notify("%s", message.c_str());
  return OnPressResult::Consumed;
}

} // namespace

/*
 * The Plugins page lists each plugin as a <link> that the stock settings UI
 * navigates natively (file="<plugin>.xml" / "plugin_<id>.xml"). Each plugin's
 * config page then binds its controls to the shared handlers below, so
 * start/stop and scan behavior stay in one place.
 */
static const OnPressExactEntry kPluginsExact[] = {
    {"id_plugin_kstuff_autoload", onpress_kstuff_autoload},
    {"id_plugin_delete_kstuff", onpress_delete_kstuff},
};

static const OnPressPrefixEntry kPluginsPrefix[] = {
    {"id_external_plugin_", external_plugin_control},
};

const OnPressExactEntry *onpress_plugins_exact(size_t *count) {
  *count = sizeof(kPluginsExact) / sizeof(kPluginsExact[0]);
  return kPluginsExact;
}

const OnPressPrefixEntry *onpress_plugins_prefix(size_t *count) {
  *count = sizeof(kPluginsPrefix) / sizeof(kPluginsPrefix[0]);
  return kPluginsPrefix;
}
