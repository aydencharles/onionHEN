/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress system settings domain */
#include "onpress.hpp"
#include "toolbox_i18n.hpp"
#include <onion/notify_i18n.h>
#include <cstdlib>

static OnPressResult id_start_opt(OnPressContext &ctx) {
  char *end = nullptr;
  const long selected = std::strtol(ctx.value.c_str(), &end, 10);
  if (end == ctx.value.c_str() || *end != '\0' ||
      (selected != onion::kStartupOpenNone &&
       selected != onion::kStartupOpenHomeMenu)) {
    LOG_WARN("Rejected unsupported startup destination: %s",
             ctx.value.c_str());
    return OnPressResult::EarlyReturn;
  }

  const int destination = static_cast<int>(selected);
  if (destination == g_settings.startup_open_after_load) {
    return OnPressResult::EarlyReturn;
  }

  g_settings.startup_open_after_load = destination;
  LOG_INFO("Startup destination: %s",
           destination == onion::kStartupOpenHomeMenu ? "home_menu" : "none");
  return OnPressResult::Handled;
}

static OnPressResult id_log_level(OnPressContext &ctx) {
  char *end = nullptr;
  const long selected = std::strtol(ctx.value.c_str(), &end, 10);
  if (end == ctx.value.c_str() || *end != '\0' ||
      selected < onion::kLogLevelOff ||
      selected > static_cast<long>(ONION_LOG_COMPILE_LEVEL)) {
    LOG_WARN("Rejected unsupported log level: %s", ctx.value.c_str());
    return OnPressResult::EarlyReturn;
  }

  const int level = static_cast<int>(selected);
  if (level == g_settings.log_level) {
    return OnPressResult::EarlyReturn;
  }

  LOG_INFO("Changing log level from %s to %s",
           onion_log_level_name(
               static_cast<onion_log_level>(g_settings.log_level)),
           onion_log_level_name(static_cast<onion_log_level>(level)));
  g_settings.log_level = level;
  ctx.reload_main = true;
  ctx.reload_util = true;
  return OnPressResult::Handled;
}

static OnPressResult id_debug_jb(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.debug_app_jb_msg) {
    LOG_WARN("Debug JB already %s",
                g_settings.debug_app_jb_msg ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.debug_app_jb_msg = !g_settings.debug_app_jb_msg;
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_app_jailbreak_enabled(OnPressContext &ctx) {
  const bool enabled = value_as_int(ctx);
  if (enabled == g_settings.app_jailbreak_enabled) {
    LOG_WARN("App jailbreak listener already %s",
             enabled ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.app_jailbreak_enabled = enabled;
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_custom_game_opts(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.onionhen_game_opts) {
    LOG_WARN("OnionHEN Game Options already %s",
                g_settings.onionhen_game_opts ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.onionhen_game_opts = !g_settings.onionhen_game_opts;
  LOG_DEBUG("OnionHEN Game Options: %s",
              g_settings.onionhen_game_opts ? "Enabled" : "Disabled");
  return OnPressResult::Handled;
}

static OnPressResult id_ui_lang(OnPressContext &ctx) {
  int v = atoi(ctx.value.c_str());
  if (v < onion::kUiLanguageSystem || v > onion::kUiLanguageTh)
    v = onion::kUiLanguageSystem;
  if (v == g_settings.ui_lang)
    return OnPressResult::EarlyReturn;
  g_settings.ui_lang = v;
  LOG_DEBUG("UI language: %s", onion::language_name(v));
  toolbox_i18n::apply_system_or_ui_lang(v);
  /* Language is process-local. This only re-reads config.ini in daemon/util
     (LoadSettings); it does not inject or restart SceShellUI. Cheat toasts
     are sent by util, so it must apply the new language too. */
  ctx.reload_main = true;
  ctx.reload_util = true;
  /* XML is built when the page opens; current tree stays in the old language. */
  notify("notify.lang.saved");
  return OnPressResult::Handled;
}

static OnPressResult id_rest_1(OnPressContext &ctx) {
  g_settings.rest_mode_delay_seconds = atol(ctx.value.c_str());
  return OnPressResult::Handled;
}

static OnPressResult id_enable_fan_speed(OnPressContext &ctx) {
  if (atol(ctx.value.c_str()) == g_settings.enable_fan_speed) {
    LOG_WARN("Fan speed control already %s",
                g_settings.enable_fan_speed ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.enable_fan_speed = !g_settings.enable_fan_speed;
  IPC_Client::getInstance(false).Set_Fan_Threshold(g_settings.fan_threshold,
                                                   g_settings.enable_fan_speed);
  return OnPressResult::Handled;
}

static OnPressResult id_fan_speed(OnPressContext &ctx) {
  int &fan_speed = g_settings.fan_threshold;
  fan_speed = atoi(ctx.value.c_str());
  if (!g_settings.enable_fan_speed) {
    notify("notify.fan.not_enabled");
    return OnPressResult::EarlyReturn;
  }
  LOG_DEBUG("Setting fan speed to %d%%", fan_speed);
  IPC_Client::getInstance(false).Set_Fan_Threshold(fan_speed,
                                                   g_settings.enable_fan_speed);
  return OnPressResult::Handled;
}

static OnPressResult id_cheats_shortcut(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.cheats_shortcut_opt) {
    LOG_WARN("Cheats_shortcut already %i", g_settings.cheats_shortcut_opt);
    return OnPressResult::EarlyReturn;
  }
  Cheats_Shortcut opt = (Cheats_Shortcut)atoi(ctx.value.c_str());
  if (opt == CHEATS_SINGLE_SHARE) {
    if (g_settings.toolbox_shortcut_opt == TOOLBOX_SINGLE_SHARE) {
      notify("notify.shortcut.toolbox_cheats");
      return OnPressResult::EarlyReturn;
    }
  } else if (opt == CHEATS_LONG_SHARE) {
    if (g_settings.toolbox_shortcut_opt == TOOLBOX_LONG_SHARE) {
      notify("notify.shortcut.toolbox_cheats_long");
      return OnPressResult::EarlyReturn;
    }
  }
  g_settings.cheats_shortcut_opt = static_cast<int>(opt);
  return OnPressResult::Handled;
}

static OnPressResult id_toolbox_shortcut(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.toolbox_shortcut_opt) {
    LOG_WARN("toolbox_shortcut_opt already %i",
                g_settings.toolbox_shortcut_opt);
    return OnPressResult::EarlyReturn;
  }
  Toolbox_Shortcut opt = (Toolbox_Shortcut)atoi(ctx.value.c_str());
  if (opt == TOOLBOX_SINGLE_SHARE) {
    if (g_settings.cheats_shortcut_opt == CHEATS_SINGLE_SHARE) {
      notify("notify.shortcut.cheats_toolbox");
      return OnPressResult::EarlyReturn;
    }
  } else if (opt == TOOLBOX_LONG_SHARE) {
    if (g_settings.cheats_shortcut_opt == CHEATS_LONG_SHARE) {
      notify("notify.shortcut.cheats_toolbox_long");
      return OnPressResult::EarlyReturn;
    }
  }
  g_settings.toolbox_shortcut_opt = static_cast<int>(opt);
  return OnPressResult::Handled;
}

static const OnPressExactEntry kExact[] = {
    {"id_start_opt", id_start_opt},
    {"id_log_level", id_log_level},
    {"id_app_jailbreak_enabled", id_app_jailbreak_enabled},
    {"id_debug_jb", id_debug_jb},
    {"id_custom_game_opts", id_custom_game_opts},
    {"id_ui_lang", id_ui_lang},
    {"id_rest_1", id_rest_1},
    {"id_enable_fan_speed", id_enable_fan_speed},
    {"id_fan_speed", id_fan_speed},
    {"id_cheats_shortcut", id_cheats_shortcut},
    {"id_toolbox_shortcut", id_toolbox_shortcut},
};

const OnPressExactEntry *onpress_system_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
