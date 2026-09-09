/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress cheats domain */
#include "onpress.hpp"
#include "shellui_state.hpp"
#include "toolbox_route.hpp"
#include <cstring>

static OnPressResult prefix_id_cheat(OnPressContext &ctx) {
  if (ctx.id.rfind("id_cheat_", 0) != 0) {
    return OnPressResult::NotMine;
  }
  // Dynamic cheats are not stock Settings entries: never SaveSettings / oOnPress.
  ctx.dirty = false;

  if (!g_ui.is_current_game_open) {
    notify("notify.cheats.game_not_running");
    LOG_ERROR("Failed to activate %s, game is not running", ctx.id.c_str());
    return OnPressResult::Consumed;
  }
  const std::string payload = ctx.id.substr(std::strlen("id_cheat_"));
  const size_t separator = payload.find('|');
  if (separator == std::string::npos) {
    return OnPressResult::Consumed;
  }
  const std::string session_id = payload.substr(0, separator);
  if (session_id.empty()) {
    return OnPressResult::Consumed;
  }
  const std::string cheat_key = payload.substr(separator + 1);
  std::string reply;
  const bool enabled = ctx.value == "1";
  if (IPC_Client::getInstance(true).ToggleGameCheat(session_id, cheat_key,
                                                    enabled, reply)) {
    const char *name =
        !ctx.title.empty() ? ctx.title.c_str() : reply.c_str();
    notify("notify.cheats.toggle_banner", name,
           onion_notify_tr(enabled ? "notify.common.on" : "notify.common.off"));
  } else {
    LOG_ERROR("Failed to activate cheat key %s: %s", cheat_key.c_str(),
              reply.empty() ? "no detail" : reply.c_str());
    if (!reply.empty())
      notify("notify.cheats.engine_status", reply.c_str());
    else
      notify("notify.cheats.activate_failed", ctx.title.c_str());
  }
  // Dynamic cheat controls are consumed before stock Settings handling.
  return OnPressResult::Consumed;
}

static OnPressResult prefix_id_cheat_view(OnPressContext &ctx) {
  if (ctx.id.rfind("id_cheat_view_", 0) != 0) {
    return OnPressResult::NotMine;
  }
  ctx.dirty = false;
  return OnPressResult::Consumed;
}

static const OnPressPrefixEntry kPrefix[] = {
    {"id_cheat_view_", prefix_id_cheat_view},
    {"id_cheat_", prefix_id_cheat},
};

const OnPressPrefixEntry *onpress_cheats_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
