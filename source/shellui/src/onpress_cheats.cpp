/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress cheats domain */
#include "onpress.hpp"
#include "shellui_state.hpp"
#include "toolbox_route.hpp"
#include <cstring>

void ParseCheatID(const char *id, char *tid, int *cheat_id);

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
  char tid[32];
  int cheat_id;
  std::string reply;
  ParseCheatID(ctx.id.c_str(), tid, &cheat_id);
  LOG_DEBUG("Getting PID for %s", ctx.id.c_str());
  int pid = onion_find_pid_ex(tid, false, true, true);
  if (pid < 0) {
    notify("notify.cheats.no_pid", ctx.title.c_str());
    LOG_ERROR("Failed to get pid for %s", tid);
    return OnPressResult::Consumed;
  }
  LOG_DEBUG("Got proc for %s, tid %s, pid %i", ctx.id.c_str(), tid, pid);
  if (IPC_Client::getInstance(true).ToggleGameCheat(pid, tid, cheat_id,
                                                    reply)) {
    (void)g_ui.reset_cheats_if_tid_changed(tid);
    const bool enabled = ctx.value == "1";
    g_ui.set_cheat_enabled(cheat_id, enabled);
    const char *name =
        !ctx.title.empty() ? ctx.title.c_str() : reply.c_str();
    notify("notify.cheats.toggle_banner", name,
           onion_notify_tr(enabled ? "notify.common.on" : "notify.common.off"));
  } else {
    LOG_ERROR("Failed to activate cheat %d for %s: %s", cheat_id, tid,
              reply.empty() ? "no detail" : reply.c_str());
    if (!reply.empty())
      notify("notify.cheats.engine_status", reply.c_str());
    else
      notify("notify.cheats.activate_failed", ctx.title.c_str());
  }
  // Consumed: stock SettingPage.OnPressed null-derefs unknown dynamic ids
  // (id_cheat_<TID>_<n> + toggle_switch) after our IPC/notify path.
  return OnPressResult::Consumed;
}

static const OnPressPrefixEntry kPrefix[] = {
    {"id_cheat_", prefix_id_cheat},
};

const OnPressPrefixEntry *onpress_cheats_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
