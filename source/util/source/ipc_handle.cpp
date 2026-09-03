/* Copyright (C) 2025 OnionHEN / LightningMods
 * Util daemon IPC command dispatch.
 * Transport (listen/accept/thread) stays in msg.cpp.
 */
#include <onion/platform.h>
#include <onion/payload.h>
#include "ipc.hpp"
#include "util_language.h"
#include <msg.hpp>
#include <onion/settings.hpp>
#include "common_utils.h"
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
extern "C" {
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
}
#include "onion_cjson.hpp"
#include <elfldr_remote.h>
#include "cheats/cheat_service.hpp"
#include "cheats/runtime.h"
#include "cheats/sync/cheat_sync_service.hpp"
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <fstream>
#include <memory>
#include <sfo.hpp>
#include <sstream>
#include <string>
#include <vector>

extern bool is_handler_enabled;

void reply(int sender_socket, bool error, std::string out_var = "Nothing");
extern "C" {
int launchApp(const char *titleId);
}
namespace {

std::string make_state_json(const char *state, uint32_t task_id = 0) {
  cJSON *root = cJSON_CreateObject();
  if (!root || !cJSON_AddStringToObject(root, "state", state) ||
      !cJSON_AddNumberToObject(root, "task_id", task_id)) {
    cJSON_Delete(root);
    return {};
  }
  return onion_cjson::print_owned(root);
}

std::string make_sync_status_json(
    const onion::cheats::sync::CheatSyncStatus &status, const char *state) {
  cJSON *root = cJSON_CreateObject();
  const char *mirror = status.mirror == onion::cheats::sync::CheatMirrorId::Cnb
                           ? "cnb"
                           : "github";
  if (!root || !cJSON_AddStringToObject(root, "state", state) ||
      !cJSON_AddNumberToObject(root, "task_id", status.task_id) ||
      !cJSON_AddStringToObject(root, "mirror", mirror) ||
      !cJSON_AddStringToObject(root, "catalog", status.catalog_id.c_str()) ||
      !cJSON_AddStringToObject(root, "error", status.error.c_str()) ||
      !cJSON_AddStringToObject(root, "phase", status.phase.c_str()) ||
      !cJSON_AddNumberToObject(root, "progress", status.progress_percent) ||
      !cJSON_AddNumberToObject(root, "completed", status.completed) ||
      !cJSON_AddNumberToObject(root, "total", status.total)) {
    cJSON_Delete(root);
    return {};
  }
  return onion_cjson::print_owned(root);
}

} // namespace

void handleIPC(clientArgs *client, std::string &inputStr,
               DaemonCommands command) {

  int sender_app = client->socket;

  std::string path_buf, path_buf2, json_path;

  char temp[0x255];
  std::string out_var = "Nothing"; // default send var

  LOG_DEBUG("Received IPC command 0x%X", command);

  onion_cjson::Root my_json(inputStr);
  if (!my_json) {
    LOG_ERROR("Error parsing JSON");
    onion_notify(true, "notify.ipc.json_parse");
    reply(sender_app, true);
    return;
  }

  switch (command) {
  case BREW_UTIL_TEST_CONNECTION: {
    reply(sender_app, false, out_var);
    break;
  }
  case BREW_UTIL_UNUSED_LEGACY_SERVICE_SCAN:
  case BREW_UTIL_UNUSED_LEGACY_SERVICE_TOGGLE:
  case BREW_UTIL_UNUSED_KLOG:
  case BREW_UTIL_UNUSED_SHELLUI_ON_STANDBY:
  case BREW_UTIL_UNUSED_SHADOWMOUNT_TOGGLE:
  case BREW_UTIL_UNUSED_SHADOWMOUNT_STATUS:
  case BREW_UTIL_UNUSED_DPI_TOGGLE:
  case BREW_UTIL_UNUSED_DPI_STATUS:
    /* Removed scan-now / Klog / rest-standby IPC; ordinals stay stable. */
    LOG_WARN("Removed-service toggle: unsupported (cmd=%u)", static_cast<unsigned>(command));
    reply(sender_app, true);
    break;
  case BREW_UTIL_DAEMON_PID: {
    snprintf(temp, sizeof(temp), "%d", getpid());
    reply(sender_app, false, temp);
    break;
  }
  case BREW_UTIL_GET_GAME_VER: {
    auto tid = std::string(onion_cjson::string_item(my_json.get(), "tid", ""));
    if (tid.empty()) {
      onion_notify(true, "notify.game.tid_failed");
      reply(sender_app, true);
      break;
    }

    char game_version[32] = {0};
    if (util_resolve_game_version(tid.c_str(), game_version,
                                  sizeof(game_version)) < 0 ||
        game_version[0] == '\0') {
      onion_notify(true, "notify.game.version_failed");
      LOG_ERROR("Failed to get game version for %s", tid.c_str());
      reply(sender_app, true);
      break;
    }

    LOG_DEBUG("Resolved %s version: %s", tid.c_str(), game_version);
    reply(sender_app, false, game_version);

    break;
  }
  case BREW_UTIL_LAUNCH_PAYLOAD: {
    std::string payload_path =
        std::string(onion_cjson::string_item(my_json.get(), "payload_path", ""));
    std::string title_id =
        std::string(onion_cjson::string_item(my_json.get(), "title_id", ""));
    LOG_INFO("Launching payload %s (key: %s)", payload_path.c_str(),
                 title_id.c_str());
    if (!load_payload(payload_path.c_str())) {
      onion_notify(true, "notify.payload.load_failed",
                   payload_path.c_str(), title_id.c_str());
      reply(sender_app, true);
      break;
    }
    onion_notify(true, "notify.payload.launched",
                 payload_path.c_str(), title_id.c_str());
    reply(sender_app, false);
    break;
  }

  case BREW_UTIL_GET_GAME_CHEAT: {
    std::string title_id =
        std::string(onion_cjson::string_item(my_json.get(), "tid", ""));
    int pid = onion_cjson::int_item(my_json.get(), "pid");
    int appid = onion_cjson::int_item(my_json.get(), "appid");
    std::string shm_path = "/user/data/OnionHEN/" + title_id + "_cheats";

    auto &cheats = onion::cheats::CheatService::instance();
    cheats.ensureDir();
    if (cheats.exportList(title_id, pid, appid, shm_path) == 0) {
      reply(sender_app, false, shm_path);
    } else {
      onion_notify(true, "notify.cheats.none", title_id.c_str());
      reply(sender_app, true);
    }
    break;
  }

  case BREW_UTIL_TOGGLE_CHEAT: {
    std::string title_id =
        std::string(onion_cjson::string_item(my_json.get(), "tid", ""));
    int pid = onion_cjson::int_item(my_json.get(), "pid");
    int appid = onion_cjson::int_item(my_json.get(), "appid");
    int cheat_id = onion_cjson::int_item(my_json.get(), "cheat_id");
    std::string status;

    LOG_DEBUG("Received toggle command for cheat %d on %s PID %d", cheat_id,
              title_id.c_str(), pid);

    auto &cheats = onion::cheats::CheatService::instance();
    if (cheats.toggle(pid, appid, title_id, cheat_id, status) == 0) {
      LOG_DEBUG("Cheat toggle reply: %s", status.c_str());
      reply(sender_app, false, status);
    } else {
      LOG_ERROR("Cheat toggle failed: %s", status.c_str());
      reply(sender_app, true, status);
    }
    break;
  }
  case BREW_UTIL_LAUNCH_ELFLDR:
    /* Manual elfldr launch removed; embedded 9020 is bootstrapper-managed. */
    LOG_WARN("BREW_UTIL_LAUNCH_ELFLDR: unsupported (bootstrapper-managed)");
    reply(sender_app, true);
    break;
  case BREW_UTIL_DOWNLOAD_CHEATS: {
    const char *catalog = onion_cjson::string_item(my_json.get(), "catalog", "");
    const char *mirror = onion_cjson::string_item(my_json.get(), "mirror", "");
    using onion::cheats::sync::CheatSyncService;
    uint32_t task_id = 0;
    const auto started = CheatSyncService::instance().start(
        g_settings.snapshot(),
        (catalog && catalog[0]) ? catalog : nullptr,
        (mirror && mirror[0]) ? mirror : nullptr, &task_id);
    if (started == CheatSyncService::StartResult::AlreadyRunning) {
      onion_notify(true, "notify.cheats.sync.busy");
      const std::string body = make_state_json("already_running", task_id);
      reply(sender_app, body.empty(), body);
    } else if (started == CheatSyncService::StartResult::Rejected) {
      const std::string body = make_state_json("rejected", task_id);
      reply(sender_app, true, body);
    } else {
      const std::string body = make_state_json("started", task_id);
      reply(sender_app, body.empty(), body);
    }
    break;
  }
  case BREW_UTIL_CHEAT_SYNC_STATUS: {
    const auto st = onion::cheats::sync::CheatSyncService::instance().status();
    const char *state = "idle";
    switch (st.state) {
    case onion::cheats::sync::CheatSyncStatus::State::Running:
      state = "running";
      break;
    case onion::cheats::sync::CheatSyncStatus::State::Ok:
      state = "ok";
      break;
    case onion::cheats::sync::CheatSyncStatus::State::Error:
      state = "error";
      break;
    case onion::cheats::sync::CheatSyncStatus::State::Idle:
    default:
      state = "idle";
      break;
    }
    const std::string body = make_sync_status_json(st, state);
    reply(sender_app, body.empty(), body);
    break;
  }
  case BREW_UTIL_CANCEL_CHEAT_SYNC: {
    const int requested_task_id =
        onion_cjson::int_item(my_json.get(), "task_id", 0);
    const bool requested =
        requested_task_id > 0 &&
        onion::cheats::sync::CheatSyncService::instance().cancel(
            static_cast<uint32_t>(requested_task_id));
    const std::string body = make_state_json(
        requested ? "cancel_requested" : "cancel_ignored",
        requested_task_id > 0 ? static_cast<uint32_t>(requested_task_id) : 0);
    reply(sender_app, body.empty(), body);
    break;
  }
  case BREW_UTIL_UNUSED_DOWNLOAD_KSTUFF:
    LOG_WARN("DOWNLOAD_KSTUFF: unsupported (online download removed)");
    reply(sender_app, true);
    break;
  case BREW_UTIL_UNUSED_RELOAD_CHEATS:
    /* Old full-tree index rebuild removed; load uses file signature hot-reload. */
    LOG_WARN("RELOAD_CHEATS: unsupported (hot-reload only)");
    reply(sender_app, true);
    break;
  case BREW_UTIL_UNUSED_LEGACY_CMD_SERVER:
    LOG_WARN("LEGACY_CMD_SERVER: unsupported (TCP 9028 removed)");
    reply(sender_app, true);
    break;
  case BREW_KILL_DAEMON:{
    /* Reply before exiting — the previous order put exit(1337) first, so the
     * kill() and reply() below it were unreachable and the caller never got a
     * response. Matches the daemon's handler. */
    is_handler_enabled = false;
    reply(sender_app, false);
    usleep(50 * 1000);
    exit(1337);
    break;
  }
  case BREW_UTIL_SET_SYSTEM_LANG: {
    /* Daemon can query the SystemService at runtime; util cannot (PTRACE).
     * Re-store the raw SCE language so every consumer (notify, webui code,
     * cheat-mirror, later ui_lang re-applies) follows it. */
    const cJSON *lang = cJSON_GetObjectItemCaseSensitive(my_json.get(),
                                                         "lang");
    if (lang && cJSON_IsNumber(lang)) {
      util_store_system_language(lang->valueint);
      util_apply_ui_language(g_settings.snapshot().ui_lang);
    }
    reply(sender_app, false);
    break;
  }
  case BREW_RELOAD_SETTINGS: {
    LoadSettings();
    //onion_notify(true, "notify.settings.reloaded");
    reply(sender_app, false);
    break;
  }
  default:
    onion_notify(true, "notify.ipc.unknown_command", command);
    reply(sender_app, true);
    break;
  }
}
