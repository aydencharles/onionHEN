/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Legacy Settings' normal back-button path pops through SettingPageStack
 * directly. UIManager.Pop is only a public wrapper and does not observe that
 * path, so page-owned cleanup belongs in OnPopping(outgoing, incoming).
 *
 * Existing page elements are refreshed after the incoming page is activated.
 */

#include "hooked_funcs.hpp"

#include "onpress_policy.hpp"
#include "progress_dialog.hpp"
#include "remote_play.hpp"
#include "shellui_state.hpp"
#include "dynamic_ui_runtime.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_route.hpp"

#include <onion/platform.h>

void SettingPageStackOnPopping_Hook(MonoObject *instance,
                                    MonoObject *outgoing,
                                    MonoObject *incoming) {
  if (shellui_hooks_are_ready() && outgoing) {
    onion::shellui::settings::release(outgoing);
    if (cheat_progress_handle_popping(outgoing)) {
      g_ui.leave_page(toolbox::Page::CheatProgress);
      LOG_DEBUG("cheat_progress_xml: progress page popped and state cleared");
    } else if (remote_play_handle_popping(outgoing)) {
      g_ui.leave_page(toolbox::Page::RemotePlay);
      LOG_DEBUG("remote_play_xml: page popped and parent route restored");
    } else if (g_ui.active_page == toolbox::Page::DynamicPlugin) {
      const bool dynamic_parent =
          onion::shellui::dynamic_ui::leave_active_page();
      if (!dynamic_parent)
        g_ui.leave_page(toolbox::Page::DynamicPlugin);
      LOG_DEBUG("dynamic_plugin_xml: page popped; dynamic_parent=%d",
                dynamic_parent ? 1 : 0);
    } else if (toolbox::restores_parent_on_pop(g_ui.active_page)) {
      const toolbox::Page child = g_ui.active_page;
      g_ui.leave_page(child);
      LOG_DEBUG("child page %u popped", static_cast<unsigned>(child));
    }
  }

  if (oSettingPageStackOnPopping)
    oSettingPageStackOnPopping(instance, outgoing, incoming);
}

void (*oSettingPageOnActivated)(MonoObject *, int) = nullptr;
void (*oSettingListCleanup)(MonoObject *) = nullptr;

void SettingPageOnActivated_Hook(MonoObject *page, int transition) {
  if (oSettingPageOnActivated)
    oSettingPageOnActivated(page, transition);
  // Firmware TransitionType.Pop = 2. This is reached after stack transition completion.
  if (shellui_hooks_are_ready() && page && transition == 2)
    onion::shellui::settings::refresh(page);
}

void SettingListCleanup_Hook(MonoObject *page) {
  onion::shellui::settings::release(page);
  if (oSettingListCleanup) oSettingListCleanup(page);
}
