/* Copyright (C) 2025 OnionHEN / LightningMods
 * Extracted from hook_functions.cpp — hook_boot
 */
#include "hooked_funcs.hpp"
#include "debug_settings_route_runtime.hpp"
#include "detour.h"
#include "ipc.hpp"
#include <msg.hpp>
#include <pthread.h>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <cstring>

#include "shellui_state.hpp"
#include <onion/platform.h>

std::string Mono_to_String(MonoString *str);

static constexpr const char kHomeTopNavUri[] = "OnionHEN?NavUI=1";
static constexpr const char kLegacyHomeTopNavUri[] = "OnionHEN?NavUI";

static bool is_home_top_nav_uri(const std::string &uri) {
  return uri == kHomeTopNavUri || uri == kLegacyHomeTopNavUri;
}

bool handle_uri_boot_common(MonoString* uri, int opt, MonoString* titleIdForBootAction) {
    std::string uri_string = Mono_to_String(uri);
    std::string titleId = titleIdForBootAction ? Mono_to_String(titleIdForBootAction) : "";
    
    LOG_DEBUG("Boot: %s (%s), OPT %i", 
                uri_string.c_str(), 
                !titleId.empty() ? titleId.c_str() : "NULL", 
                opt);
  
    if(uri_string == "OnionHEN?Cheats") {
      LOG_DEBUG("cheats_shortcut URI detected");
      g_ui.cheats_shortcut_activated = true;
      return true; // Signal to redirect
    }
    else if(uri_string == "OnionHEN?Cheats_not_open") {
      LOG_DEBUG("cheats_shortcut (not open) URI detected");
      g_ui.cheats_shortcut_activated_not_open = true;
      return true;
    }
    else if (uri_string == "OnionHEN?DL_UPDATE") {
        LOG_DEBUG("DL_UPDATE URI detected");
        
        return true; // Signal to redirect
    }
    else if (is_home_top_nav_uri(uri_string)) {
      LOG_DEBUG("HomeUI top-nav OnionHEN URI detected");
      return true;
    }

    return false; // No redirect needed
  }
  
  bool uri_boot_hook(MonoString* uri, int opt, MonoString* titleIdForBootAction) {
    if (!shellui_hooks_are_ready())
      return boot_orig ? boot_orig(uri, opt, titleIdForBootAction) : false;

    if(handle_uri_boot_common(uri, opt, titleIdForBootAction)) {
      return boot_orig(
          mono_string_new(Root_Domain, shellui_debug_settings_toolbox_uri()), opt,
          titleIdForBootAction);
    }

    // On 11.6+, appdb Debug Settings and other deeplinks must avoid the RN screen.
    const std::string original_uri = Mono_to_String(uri);
    const std::string rewritten = shellui_rewrite_debug_settings_route(original_uri);
    if (rewritten != original_uri) {
      LOG_DEBUG("Boot: rewrite debug_settings → old: %s", rewritten.c_str());
      return boot_orig(mono_string_new(Root_Domain, rewritten.c_str()), opt,
                       titleIdForBootAction);
    }

    return boot_orig(uri, opt, titleIdForBootAction);
  }
  
  bool uri_boot_hook_2(MonoString* uri, int opt) {
    if (!shellui_hooks_are_ready())
      return boot_orig_2 ? boot_orig_2(uri, opt) : false;

    const std::string original_uri = Mono_to_String(uri);
    LOG_DEBUG("uri_boot_hook_2: %s, opt: %i", original_uri.c_str(), opt);
    if(handle_uri_boot_common(uri, opt, nullptr)) {
      // Redirect to debug settings (no titleId parameter for older fw).
      return boot_orig_2(
          mono_string_new(Root_Domain,
                          shellui_debug_settings_toolbox_uri_simple()),
          opt);
    }

    const std::string rewritten = shellui_rewrite_debug_settings_route(original_uri);
    if (rewritten != original_uri) {
      LOG_DEBUG("Boot2: rewrite debug_settings → old: %s", rewritten.c_str());
      return boot_orig_2(mono_string_new(Root_Domain, rewritten.c_str()), opt);
    }

    return boot_orig_2(uri, opt);
  }

  GamePadData GetData_hook(int deviceIndex) {
    if (!shellui_hooks_are_ready())
      return GetData ? GetData(deviceIndex) : GamePadData{};

    GamePadData result;
    bool cheas_sc_activated = false;
    bool toolbox_sc_activated = false;
  
    const std::chrono::milliseconds LONG_PRESS_DURATION(1000); // 1 second
  
    // Static variables for Cheats shortcut hold detection
    static bool cheats_pressed = false;
    static std::chrono::steady_clock::time_point cheats_press_start;
    static bool cheats_long_press_triggered = false;
  
    // Static variables for Toolbox shortcut hold detection
    static bool toolbox_pressed = false;
    static std::chrono::steady_clock::time_point toolbox_press_start;
    static bool toolbox_long_press_triggered = false;

  
    result = GetData(deviceIndex);

    // Cheats Shortcut
    if (g_settings.cheats_shortcut_opt != CHEATS_SC_OFF) {
      bool cheats_buttons_held = false;
  
      switch (g_settings.cheats_shortcut_opt) {
      case R3_L3:
        cheats_buttons_held = (result.Buttons & R3) && (result.Buttons & L3);
        break;
      case L2_TRIANGLE:
        cheats_buttons_held = (result.Buttons & L2) && (result.Buttons & Triangle);
        break;
      case LONG_OPTIONS:
        cheats_buttons_held = (result.Buttons & Option);
        break;
      default:
        break;
      }
  
      if (cheats_buttons_held) {
        if (!cheats_pressed) {
          cheats_pressed = true;
          cheats_press_start = std::chrono::steady_clock::now();
          cheats_long_press_triggered = false;
          LOG_DEBUG("Cheats buttons pressed - starting timer");
        } else {
          auto current_time = std::chrono::steady_clock::now();
          auto hold_duration = std::chrono::duration_cast < std::chrono::milliseconds > (
            current_time - cheats_press_start
          );
  
          // Log every 500ms to track progress
          static auto last_log_time = std::chrono::steady_clock::now();
          if (std::chrono::duration_cast < std::chrono::milliseconds > (
              current_time - last_log_time) >= std::chrono::milliseconds(500)) {
              LOG_DEBUG("Cheats buttons held for %lld ms (need %lld ms)",
              hold_duration.count(),
              LONG_PRESS_DURATION.count());
            last_log_time = current_time;
          }
  
          if (hold_duration >= LONG_PRESS_DURATION && !cheats_long_press_triggered) {
            LOG_DEBUG("Cheats long press threshold reached! Duration: %lld ms",
              hold_duration.count());
            cheas_sc_activated = true;
            cheats_long_press_triggered = true;
          }
        }
      } else {
        if (cheats_pressed) {
          auto current_time = std::chrono::steady_clock::now();
          auto hold_duration = std::chrono::duration_cast < std::chrono::milliseconds > (
            current_time - cheats_press_start
          );
          LOG_DEBUG("Cheats buttons released after %lld ms (needed %lld ms)",
            hold_duration.count(),
            LONG_PRESS_DURATION.count());
        }
        cheats_pressed = false;
        cheats_long_press_triggered = false;
      }
  
      if (cheas_sc_activated) {
        LOG_DEBUG("Cheats Shortcut Activated");
        GoToURI("OnionHEN?Cheats");
        result.Buttons = None; // Clear the Select button to prevent triggering other actions
        cheas_sc_activated = false; // Reset the flag
      }
    }
  
    // Toolbox Shortcut
    if (g_settings.toolbox_shortcut_opt != TOOLBOX_SC_OFF) {
      bool toolbox_buttons_held = false;
  
      switch (g_settings.toolbox_shortcut_opt) {
      case L2_R3:
        toolbox_buttons_held = (result.Buttons & L2) && (result.Buttons & R3);
        break;
      default:
        break;
      }
  
      if (toolbox_buttons_held) {
        if (!toolbox_pressed) {
          toolbox_pressed = true;
          toolbox_press_start = std::chrono::steady_clock::now();
          toolbox_long_press_triggered = false;
        } else {
          auto current_time = std::chrono::steady_clock::now();
          auto hold_duration = std::chrono::duration_cast < std::chrono::milliseconds > (
            current_time - toolbox_press_start
          );
  
          if (hold_duration >= LONG_PRESS_DURATION && !toolbox_long_press_triggered) {
            toolbox_sc_activated = true;
            toolbox_long_press_triggered = true;
          }
        }
      } else {
        toolbox_pressed = false;
        toolbox_long_press_triggered = false;
      }
  
      if (toolbox_sc_activated) {
        LOG_DEBUG("Toolbox Shortcut Activated");
        GoToURI(shellui_debug_settings_toolbox_uri());
        result.Buttons = None; // Clear the Select button to prevent triggering other actions
      }
    }
  
    if (result.Buttons & Option) {
      LOG_DEBUG("Option button pressed");
    }
  
    return result;
  }
