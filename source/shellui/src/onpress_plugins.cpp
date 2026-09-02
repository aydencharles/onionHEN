/* Copyright (C) 2025 OnionHEN / LightningMods - OnPress built-in plugins */

#include "onpress.hpp"
#include "shellui_payload_state.hpp"
#include "external_plugin_ui.hpp"
#include "toolbox_i18n.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>

namespace {

OnPressResult toggle_plugin_now(OnPressContext &ctx, DaemonCommands cmd,
                                bool (*is_running)(),
                                const char *fail_notify,
                                const char *on_notify, const char *off_notify) {
  ctx.dirty = false;
  const bool enabled = value_as_int(ctx);
  /* Stop is idempotent and must clear util's desired state even when a
   * listener failed before the UI could observe it as running. */
  if (enabled && is_running())
    return OnPressResult::EarlyReturn;

  if (IPC_Client::getInstance(true).ToggleSetting(cmd, enabled) !=
      IPC_Ret::NO_ERROR) {
    notify(fail_notify);
    return OnPressResult::EarlyReturn;
  }
  notify(enabled ? on_notify : off_notify);
  return OnPressResult::Handled;
}

OnPressResult toggle_next_boot(OnPressContext &ctx, bool &field,
                               const char *on_notify, const char *off_notify) {
  const bool enabled = value_as_int(ctx);
  if (enabled == field)
    return OnPressResult::EarlyReturn;
  field = enabled;
  notify(enabled ? on_notify : off_notify);
  return OnPressResult::Handled;
}

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

OnPressResult onpress_ftp_run(OnPressContext &ctx) {
  return toggle_plugin_now(
      ctx, BREW_UTIL_TOGGLE_FTP,
      +[]() { return IPC_Client::getInstance(true).FtpStatus(); },
      "notify.ftp.toggle_failed", "notify.ftp.enabled",
      "notify.ftp.disabled");
}

OnPressResult onpress_ftp_autoload(OnPressContext &ctx) {
  return toggle_next_boot(ctx, g_settings.ftp_autoload,
                          "notify.ftp.next_boot_on", "notify.ftp.next_boot_off");
}

OnPressResult onpress_ftp_port(OnPressContext &ctx) {
  char *end = nullptr;
  errno = 0;
  const long parsed = std::strtol(ctx.value.c_str(), &end, 10);
  if (errno != 0 || end == ctx.value.c_str() || *end != '\0' || parsed < 1 ||
      parsed > 65535) {
    notify("notify.ftp.port_invalid");
    return OnPressResult::EarlyReturn;
  }

  const int port = static_cast<int>(parsed);
  if (port == g_settings.ftp_port)
    return OnPressResult::EarlyReturn;
  g_settings.ftp_port = port;
  /* settings_commit persists the value and asks util to reconfigure a live
   * listener.  The current run state is intentionally left unchanged. */
  ctx.reload_util = true;
  notify("notify.ftp.port_changed", port);
  return OnPressResult::Handled;
}

/* ShadowMount+ run is a button: read the live module state from util, flip
 * it, and report through notifications. The page carries no toggle state for
 * this control. */
OnPressResult onpress_shadowmount_run(OnPressContext &ctx) {
  ctx.dirty = false;
  const bool running = IPC_Client::getInstance(true).ShadowMountStatus();
  if (IPC_Client::getInstance(true).ToggleSetting(BREW_UTIL_TOGGLE_SHADOWMOUNT,
                                                  !running) !=
      IPC_Ret::NO_ERROR) {
    notify("notify.shadowmount.toggle_failed");
    return OnPressResult::EarlyReturn;
  }
  notify(running ? "notify.shadowmount.disabled" : "notify.shadowmount.enabled");
  return OnPressResult::Consumed;
}

OnPressResult onpress_shadowmount_autoload(OnPressContext &ctx) {
  return toggle_next_boot(ctx, g_settings.shadowmount_autoload,
                          "notify.shadowmount.next_boot_on",
                          "notify.shadowmount.next_boot_off");
}

/* Network package installer toggles. Run controls the in-process DPI server
 * (TCP 9090) for this session only; autoload mirrors the FTP next-boot
 * behavior. */
OnPressResult onpress_pkgnet_run(OnPressContext &ctx) {
  return toggle_plugin_now(
      ctx, BREW_UTIL_TOGGLE_PKGNET,
      +[]() { return IPC_Client::getInstance(true).PkgNetStatus(); },
      "notify.pkgnet.toggle_failed", "notify.pkgnet.enabled",
      "notify.pkgnet.disabled");
}

OnPressResult onpress_pkgnet_autoload(OnPressContext &ctx) {
  return toggle_next_boot(ctx, g_settings.pkgnet_autoload,
                          "notify.pkgnet.next_boot_on",
                          "notify.pkgnet.next_boot_off");
}

/*
 * The Plugins page lists each built-in plugin as a <link> that the stock
 * settings UI navigates natively (file="<plugin>.xml"). Each plugin's config
 * page then binds its controls to the shared handlers below, so start/stop and
 * scan behavior stay in one place.
 */
static const OnPressExactEntry kPluginsExact[] = {
    {"id_plugin_kstuff_autoload", onpress_kstuff_autoload},
    {"id_plugin_delete_kstuff", onpress_delete_kstuff},
    {"id_plugin_ftpsrv_run", onpress_ftp_run},
    {"id_plugin_ftpsrv_autoload", onpress_ftp_autoload},
    {"id_plugin_ftpsrv_port", onpress_ftp_port},
    {"id_plugin_shadowmount_run", onpress_shadowmount_run},
    {"id_plugin_shadowmount_autoload", onpress_shadowmount_autoload},
    {"id_pkgnet_run", onpress_pkgnet_run},
    {"id_pkgnet_autoload", onpress_pkgnet_autoload},
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
