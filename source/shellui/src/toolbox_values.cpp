/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Shared value providers for toolbox UI binding (XML path + optional hooks).
 */
#include "toolbox_values.hpp"

#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "shellui_payload_state.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "dynamic_ui_runtime.hpp"
#include "plugin_sprx_pages.hpp"

#include <onion/platform.h>
#include <onion/ipc_client.hpp>

#include <algorithm>
#include <cstring>
#include <ranges>
#include <string>

namespace {

std::string bool_str(bool v) { return v ? "1" : "0"; }
std::string int_str(int v) { return std::to_string(v); }

struct ExactValueEntry {
  const char *id;
  std::string (*get)();
};

const ExactValueEntry kExactValues[] = {
    {"id_lm_test", +[]() -> std::string { return "0"; }},
    {"id_start_opt",
     +[]() -> std::string {
       return int_str(g_settings.startup_open_after_load);
     }},
    {"id_overlay_enabled",
     +[]() -> std::string { return bool_str(g_settings.overlay_enabled); }},
    {"id_overlay_background",
     +[]() -> std::string { return bool_str(g_settings.overlay_background); }},
    {"id_overlay_gpu",
     +[]() -> std::string { return bool_str(g_settings.overlay_gpu); }},
    {"id_overlay_fps",
     +[]() -> std::string { return bool_str(g_settings.overlay_fps); }},
    {"id_overlay_ip",
     +[]() -> std::string { return bool_str(g_settings.overlay_ip); }},
    {"id_overlay_fan",
     +[]() -> std::string { return bool_str(g_settings.overlay_fan); }},
    {"id_all_cpu_usage",
     +[]() -> std::string { return bool_str(g_settings.all_cpu_usage); }},
    {"id_overlay_cpu",
     +[]() -> std::string { return bool_str(g_settings.overlay_cpu); }},
    {"id_overlay_ram",
     +[]() -> std::string { return bool_str(g_settings.overlay_ram); }},
    {"id_plugin_kstuff_autoload",
     +[]() -> std::string { return bool_str(g_settings.kstuff_autoload); }},
    {"id_disp_titleids",
     +[]() -> std::string { return bool_str(g_settings.display_tids); }},
    {"id_enable_fan_speed",
     +[]() -> std::string { return bool_str(g_settings.enable_fan_speed); }},
    {"id_fan_speed",
     +[]() -> std::string { return int_str(g_settings.fan_threshold); }},
    {"id_cheats_shortcut",
     +[]() -> std::string { return int_str(g_settings.cheats_shortcut_opt); }},
    {"id_cheats_mirror",
     +[]() -> std::string { return int_str(g_settings.cheats_mirror); }},
    {"id_ui_lang", +[]() -> std::string { return int_str(g_settings.ui_lang); }},
    {"id_log_level",
     +[]() -> std::string {
       const int effective =
           g_settings.log_level > ONION_LOG_COMPILE_LEVEL
               ? static_cast<int>(ONION_LOG_COMPILE_LEVEL)
               : g_settings.log_level;
       return int_str(effective);
     }},
    {"id_debug_jb",
     +[]() -> std::string { return bool_str(g_settings.debug_app_jb_msg); }},
    {"id_app_jailbreak_enabled",
     +[]() -> std::string {
       return bool_str(g_settings.app_jailbreak_enabled);
     }},
    {"id_custom_game_opts",
     +[]() -> std::string { return bool_str(g_settings.onionhen_game_opts); }},
    {"id_overlay_change_pos",
     +[]() -> std::string { return int_str(g_settings.overlay_pos); }},
    {"id_overlay_align",
     +[]() -> std::string { return int_str(g_settings.overlay_align); }},
    {"id_overlay_font_size",
     +[]() -> std::string { return int_str(g_settings.overlay_font_size); }},
    {"id_overlay_fps_order",
     +[]() -> std::string {
       return int_str(
           onion::overlay_metric_position(g_settings.overlay_order,
                                          onion::kOverlayMetricFps));
     }},
    {"id_overlay_cpu_order",
     +[]() -> std::string {
       return int_str(
           onion::overlay_metric_position(g_settings.overlay_order,
                                          onion::kOverlayMetricCpu));
     }},
    {"id_overlay_gpu_order",
     +[]() -> std::string {
       return int_str(
           onion::overlay_metric_position(g_settings.overlay_order,
                                          onion::kOverlayMetricGpu));
     }},
    {"id_overlay_ram_order",
     +[]() -> std::string {
       return int_str(onion::overlay_metric_position(
           g_settings.overlay_order, onion::kOverlayMetricMemory));
     }},
    {"id_overlay_ip_order",
     +[]() -> std::string {
       return int_str(
           onion::overlay_metric_position(g_settings.overlay_order,
                                          onion::kOverlayMetricIp));
     }},
    {"id_overlay_fan_order",
     +[]() -> std::string {
       return int_str(
           onion::overlay_metric_position(g_settings.overlay_order,
                                          onion::kOverlayMetricFan));
     }},
    /* Exact list id only — not id_toolbox_shortcut_N list_items. */
    {"id_toolbox_shortcut",
     +[]() -> std::string { return int_str(g_settings.toolbox_shortcut_opt); }},
};

bool try_exact_value(const std::string &id, std::string &out) {
  for (const auto &e : kExactValues) {
    if (id == e.id) {
      out = e.get();
      return true;
    }
  }
  return false;
}

bool try_payload_control_value(const std::string &id, std::string &out) {
  constexpr std::string_view kRunPrefix = "id_payload_run_";
  constexpr std::string_view kAutoStartPrefix = "id_payload_autostart_";
  constexpr std::string_view kPriorityPrefix = "id_payload_priority_";
  constexpr std::string_view kDelayPrefix = "id_payload_delay_";
  std::string_view payload_id;
  bool is_run = false;
  bool is_priority = false;
  bool is_delay = false;
  if (id.starts_with(kRunPrefix)) {
    payload_id = std::string_view(id).substr(kRunPrefix.size());
    is_run = true;
  } else if (id.starts_with(kAutoStartPrefix)) {
    payload_id = std::string_view(id).substr(kAutoStartPrefix.size());
  } else if (id.starts_with(kPriorityPrefix)) {
    payload_id = std::string_view(id).substr(kPriorityPrefix.size());
    is_priority = true;
  } else if (id.starts_with(kDelayPrefix)) {
    payload_id = std::string_view(id).substr(kDelayPrefix.size());
    is_delay = true;
  } else {
    return false;
  }
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id != payload_id)
      continue;
    if (is_priority || is_delay) {
      OnionPayloadConfig config;
      onion_payload_config_load(entry.shellui_path.c_str(), &config);
      out = int_str(is_priority ? config.priority : config.delay_seconds);
      return true;
    }
    out = bool_str(is_run
                       ? shellui_payload_resolve_recorded_pid(entry.tid.c_str(),
                                                              nullptr, 0) > 1
                       : if_exists((entry.shellui_path + ".auto_start").c_str()));
    return true;
  }
  return false;
}

bool try_cheat_value(const std::string &id, std::string &out) {
  bool enabled = false;
  if (!g_ui.cheat_toggle_value(id, &enabled))
    return false;
  out = bool_str(enabled);
  return true;
}

bool try_external_plugin_value(const std::string &id, std::string &out) {
  using onion::shellui::plugin_pages::kPluginAutoStartPrefix;
  using onion::shellui::plugin_pages::kPluginRunPrefix;
  using onion::shellui::plugin_pages::split_control_id;

  std::string plugin_id;
  bool is_run = false;
  if (split_control_id(id, kPluginRunPrefix, plugin_id, 9, 9)) {
    is_run = true;
  } else if (!split_control_id(id, kPluginAutoStartPrefix, plugin_id, 9, 9)) {
    return false;
  }
  const auto it = std::ranges::find(g_ui.external_plugins, plugin_id,
                                    &PluginInventoryItem::plugin_id);
  if (it == g_ui.external_plugins.end())
    return false;
  out = bool_str(is_run ? it->running : it->auto_start);
  return true;
}

bool try_external_sprx_value(const std::string &id, std::string &out) {
  using onion::shellui::plugin_pages::kSprxEnabledPrefix;
  using onion::shellui::plugin_pages::split_control_id;

  std::string sprx_id;
  if (!split_control_id(id, kSprxEnabledPrefix, sprx_id, 1, 31))
    return false;
  const auto it =
      std::ranges::find(g_ui.external_sprx, sprx_id, &SprxInventoryItem::id);
  if (it == g_ui.external_sprx.end())
    return false;
  out = bool_str(it->enabled);
  return true;
}

bool try_dynamic_control_value(const std::string &id, std::string &out) {
  return onion::shellui::dynamic_ui::resolve_control_value(id, out);
}

} // namespace

std::string resolve_toolbox_control_value(const std::string &id) {
  std::string value;

  if (try_payload_control_value(id, value))
    return value;
  if (try_exact_value(id, value))
    return value;
  if (try_cheat_value(id, value))
    return value;
  if (try_external_plugin_value(id, value))
    return value;
  if (try_external_sprx_value(id, value))
    return value;
  if (try_dynamic_control_value(id, value))
    return value;

  return {};
}
