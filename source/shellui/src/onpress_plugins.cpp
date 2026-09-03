/* Copyright (C) 2025 OnionHEN / LightningMods - OnPress built-in plugins */

#include "onpress.hpp"
#include "shellui_payload_state.hpp"
#include "external_plugin_ui.hpp"
#include "toolbox_i18n.hpp"

namespace {

OnPressResult external_plugin_control(OnPressContext &ctx) {
  ctx.dirty = false;
  const onion::shellui::external_plugins::DispatchResult result =
      onion::shellui::external_plugins::dispatch(ctx.id, ctx.value);
  if (!result.owned) return OnPressResult::NotMine;

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
    case Action::Reloaded:
      key = "plugins.external.reloaded_fmt";
      break;
    case Action::Deleted:
      key = "plugins.external.deleted_fmt";
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
 * The Plugins page lists each built-in plugin as a <link> that the stock
 * settings UI navigates natively (file="<plugin>.xml"). Each plugin's config
 * page then binds its controls to the shared handlers below, so start/stop and
 * scan behavior stay in one place.
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
