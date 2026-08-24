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
  std::string cheat_name;
  ParseCheatID(ctx.id.c_str(), tid, &cheat_id);
  LOG_DEBUG("Getting PID for %s", ctx.id.c_str());
  int pid = onion_find_pid_ex(tid, false, true, true);
  if (pid < 0) {
    notify("notify.cheats.no_pid",
           cheat_name.c_str());
    LOG_ERROR("Failed to get pid for %s", tid);
    return OnPressResult::Consumed;
  }
  LOG_DEBUG("Got proc for %s, tid %s, pid %i", ctx.id.c_str(), tid, pid);
  if (IPC_Client::getInstance(true).ToggleGameCheat(pid, tid, cheat_id,
                                                    cheat_name)) {
    (void)g_ui.reset_cheats_if_tid_changed(tid);
    const bool enabled = ctx.value == "1";
    g_ui.set_cheat_enabled(cheat_id, enabled);
    notify("notify.cheats.toggle_banner", cheat_name.c_str(),
           onion_notify_tr(enabled ? "notify.common.on" : "notify.common.off"));
  } else {
    LOG_ERROR("Failed to activate cheat %d for %s: %s", cheat_id, tid,
              cheat_name.empty() ? "no detail" : cheat_name.c_str());
    notify("notify.cheats.activate_failed", cheat_name.c_str());
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
