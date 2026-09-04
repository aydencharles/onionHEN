/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Toolbox / settings UI runtime state. All ShellUI session state lives on g_ui.
 */
#pragma once

#include "shellui_types.hpp"
#include "toolbox_route.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <onion/ipc_client.hpp>

/** Settings page / resource-stream context for ShellUI hooks. */
struct ToolboxUiState {
  toolbox::Page active_page = toolbox::Page::None;
  toolbox::Page parent_page = toolbox::Page::None;
  toolbox::Page child_page = toolbox::Page::None;

  bool cheats_shortcut_activated = false;
  bool cheats_shortcut_activated_not_open = false;

  std::string running_tid;
  bool is_game_open = true;
  bool is_current_game_open = true;
  std::string current_menu_tid;
  std::string current_cheat_tid;
  /* Cheat sources are independent; the visible list is not capped at 256. */
  std::vector<unsigned char> cheat_enabled_map;

  std::vector<PayloadEntry> payloads_list;
  std::vector<PayloadEntry> auto_payloads_list;
  std::vector<Payloads_Apps> payloads_apps_list;
  std::vector<GameEntry> games_list;

  std::vector<PluginInventoryItem> external_plugins;
  std::vector<SprxInventoryItem> external_sprx;

  /* The plugin whose config page is active; a registry key ("kstuff"/…). */
  std::string active_plugin;

  void set_active_page(toolbox::Page page) {
    if (toolbox::restores_parent_on_pop(page) && active_page != page) {
      parent_page = active_page;
      child_page = page;
    }
    active_page = page;
  }

  bool is_active_page(toolbox::Page page) const {
    return active_page == page;
  }

  void leave_page(toolbox::Page page) {
    if (child_page == page) {
      if (active_page == page)
        active_page = parent_page;
      parent_page = toolbox::Page::None;
      child_page = toolbox::Page::None;
      return;
    }
    if (active_page == page)
      active_page = toolbox::Page::None;
  }

  void clear_cheat_shortcuts() {
    cheats_shortcut_activated = false;
    cheats_shortcut_activated_not_open = false;
  }

  bool any_cheat_shortcut() const {
    return cheats_shortcut_activated || cheats_shortcut_activated_not_open;
  }

  bool reset_cheats_if_tid_changed(std::string_view new_tid) {
    if (current_cheat_tid == new_tid)
      return false;
    current_cheat_tid = std::string(new_tid);
    cheat_enabled_map.clear();
    return true;
  }

  void set_cheat_enabled(int cheat_id, bool enabled) {
    if (cheat_id < 0)
      return;
    const auto index = static_cast<std::size_t>(cheat_id);
    if (index >= cheat_enabled_map.size())
      cheat_enabled_map.resize(index + 1, 0);
    cheat_enabled_map[index] = enabled ? 1 : 0;
  }

  bool get_cheat_enabled(int cheat_id) const {
    return cheat_id >= 0 &&
           static_cast<std::size_t>(cheat_id) < cheat_enabled_map.size() &&
           cheat_enabled_map[static_cast<std::size_t>(cheat_id)] != 0;
  }
};

extern ToolboxUiState g_ui;
