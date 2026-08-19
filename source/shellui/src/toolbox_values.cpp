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

#include <onion/platform.h>

#include <cstring>
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
    {"id_overlay_ip",
     +[]() -> std::string { return bool_str(g_settings.overlay_ip); }},
    {"id_all_cpu_usage",
     +[]() -> std::string { return bool_str(g_settings.all_cpu_usage); }},
    {"id_overlay_cpu",
     +[]() -> std::string { return bool_str(g_settings.overlay_cpu); }},
    {"id_overlay_ram",
     +[]() -> std::string { return bool_str(g_settings.overlay_ram); }},
    {"id_kstuff_autoload",
     +[]() -> std::string { return bool_str(g_settings.kstuff_autoload); }},
    {"id_ftp_autoload",
     +[]() -> std::string { return bool_str(g_settings.ftp_autoload); }},
    {"id_shadowmount_autoload",
     +[]() -> std::string { return bool_str(g_settings.shadowmount_autoload); }},
    {"id_disp_titleids",
     +[]() -> std::string { return bool_str(g_settings.display_tids); }},
    {"id_enable_fan_speed",
     +[]() -> std::string { return bool_str(g_settings.enable_fan_speed); }},
    {"id_rest_1",
     +[]() -> std::string {
       return int_str(static_cast<int>(g_settings.rest_mode_delay_seconds));
     }},
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

bool try_payload_list_value(const std::string &id, std::string &out) {
  for (const auto &entry : g_ui.payloads_list) {
    if (entry.id != id)
      continue;
    out = bool_str(shellui_payload_resolve_recorded_pid(entry.tid.c_str(),
                                                        nullptr, 0) > 1);
    return true;
  }
  return false;
}

bool try_auto_payload_value(const std::string &id, std::string &out) {
  for (const auto &entry : g_ui.auto_payloads_list) {
    if (entry.id != id)
      continue;
    const std::string auto_path = entry.shellui_path + ".auto_start";
    out = bool_str(if_exists(auto_path.c_str()));
    return true;
  }
  return false;
}

bool try_cheat_value(const std::string &id, std::string &out) {
  if (id.find("id_cheat_") == std::string::npos)
    return false;
  if (!g_ui.is_current_game_open)
    return false;

  char tid[32] = {};
  int cheat_id = 0;
  ParseCheatID(id.c_str(), tid, &cheat_id);
  out = bool_str(g_ui.get_cheat_enabled(cheat_id));
  return true;
}

} // namespace

std::string resolve_toolbox_control_value(const std::string &id) {
  std::string value;

  if (try_payload_list_value(id, value))
    return value;
  if (try_auto_payload_value(id, value))
    return value;
  if (try_exact_value(id, value))
    return value;
  if (try_cheat_value(id, value))
    return value;

  return {};
}
