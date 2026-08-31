/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Extracted from mono_utils.cpp for module locality.
 */

#include "hooked_funcs.hpp"
#include "toolbox_i18n.hpp"
#include <onion/platform.h>
#include <onion/ready.h>
#include <onion/log_settings.hpp>
#include "ipc.hpp" // shellui_log + IPC_Client
#include <onion/settings.hpp>

void apply_overlay_layout() {
  const float screen_w = g_overlay_layout.screen_w;
  const float screen_h = g_overlay_layout.screen_h;
  onion::overlay::Metrics metrics{};
  metrics.enabled = g_settings.overlay_enabled;
  metrics.show_fps = g_settings.overlay_fps;
  metrics.show_cpu = g_settings.overlay_cpu;
  metrics.show_gpu = g_settings.overlay_gpu;
  metrics.show_ram = g_settings.overlay_ram;
  metrics.show_ip = g_settings.overlay_ip;
  metrics.show_fan = g_settings.overlay_fan;
  metrics.per_core_cpu = g_settings.all_cpu_usage;
  g_overlay_layout = onion::overlay::compute_overlay_layout(
      screen_w, screen_h,
      onion::overlay::bar_edge_from_pos(g_settings.overlay_pos),
      onion::overlay::bar_align_from_value(g_settings.overlay_align), metrics);
}

void apply_overlay_layout(float screen_w, float screen_h) {
  g_overlay_layout.screen_w = screen_w;
  g_overlay_layout.screen_h = screen_h;
  apply_overlay_layout();
}

bool LoadSettings()
{
  onion::Settings s{};
  /* false from settings_load means defaults only — not a hard failure. */
  const bool from_file = onion::settings_load(&s);
  const onion_log_level effective = onion::apply_log_settings(s);
  if (!from_file) {
    LOG_ERROR("config.ini missing; using defaults");
  } else {
    LOG_DEBUG("Loaded settings from %s", onion::settings_last_loaded_path());
  }
  if (effective != static_cast<onion_log_level>(s.log_level)) {
    LOG_WARN("ShellUI log level '%s' unavailable in this build; using '%s'",
             onion_log_level_name(static_cast<onion_log_level>(s.log_level)),
             onion_log_level_name(effective));
  }

  // Process-local store (UI thread); twin disk paths via settings_load/save.
  g_settings = s;
  toolbox_i18n::apply_system_or_ui_lang(s.ui_lang);
  /* Clear stale fps_overlay marker left by older shellui builds. */
  onion_ready_clear(ONION_FLAG_FPS_OVERLAY);
  apply_overlay_layout();
  /* Always true once defaults-or-file applied (prx boot requires success). */
  return true;
}

bool SaveSettings()
{
  if (!onion::settings_save(g_settings)) {
    LOG_ERROR("Failed to save settings to any config path");
    return false;
  }
  LOG_DEBUG("Saved settings (primary + shellui paths when writable)");
  return true;
}

void settings_commit(bool reload_main, bool reload_util)
{
  if (!SaveSettings()) {
    return;
  }
  if (reload_main) {
    IPC_Client::getInstance(false).Reload_Daemon_Settings();
  }
  if (reload_util) {
    IPC_Client::getInstance(true).Reload_Daemon_Settings();
  }
  /* Apply last so failures while persisting/propagating remain visible even
     when the newly selected level is off or more restrictive. */
  (void)onion::apply_log_settings(g_settings);
}
