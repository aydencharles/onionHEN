/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Toolbox / settings UI runtime state. All ShellUI session state lives on g_ui.
 */
#pragma once

#include "shellui_types.hpp"
#include "toolbox_route.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <onion/ipc_client.hpp>

/** Settings page / resource-stream context for ShellUI hooks. */
struct ToolboxUiState {
  toolbox::Page active_page = toolbox::Page::None;
  toolbox::Page parent_page = toolbox::Page::None;
  toolbox::Page child_page = toolbox::Page::None;

  bool cheats_shortcut_activated = false;
  bool cheats_shortcut_activated_not_open = false;

  bool is_game_open = true;
  bool is_current_game_open = true;
  std::string current_menu_tid;

  std::vector<PayloadEntry> payloads_list;

  std::vector<PluginInventoryItem> external_plugins;
  std::vector<SprxInventoryItem> external_sprx;

  /* The plugin whose config page is active; a registry key ("kstuff"/…). */
  std::string active_plugin;

  struct ParentContext {
    toolbox::Page page;
    std::string plugin;
  };
  std::vector<ParentContext> parent_pages;

  void set_active_page(toolbox::Page page) {
    if (toolbox::restores_parent_on_pop(page) && active_page != page) {
      parent_pages.push_back({active_page, active_plugin});
      parent_page = active_page;
      child_page = page;
    } else if (page != active_page && !toolbox::restores_parent_on_pop(page)) {
      parent_pages.clear();
      parent_page = toolbox::Page::None;
      child_page = toolbox::Page::None;
    }
    active_page = page;
  }

  bool is_active_page(toolbox::Page page) const {
    return active_page == page;
  }

  void leave_page(toolbox::Page page) {
    if (child_page == page && active_page == page && !parent_pages.empty()) {
      active_page = parent_pages.back().page;
      active_plugin = std::move(parent_pages.back().plugin);
      parent_pages.pop_back();
      parent_page = parent_pages.empty() ? toolbox::Page::None : parent_pages.back().page;
      child_page = parent_pages.empty() ? toolbox::Page::None : active_page;
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

};

extern ToolboxUiState g_ui;
