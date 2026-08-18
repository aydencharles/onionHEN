/* Copyright (C) 2025 OnionHEN / LightningMods
 * Util daemon IPC command dispatch.
 * Transport (listen/accept/thread) stays in msg.cpp.
 */
#include <onion/platform.h>
#include "ipc.hpp"
#include "rest_mode.hpp"
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

extern "C" void ftp_server_apply(bool enabled);

void reply(int sender_socket, bool error, std::string out_var = "Nothing");
extern "C" {
bool load_payload(const char *path);
int launchApp(const char *titleId);
}
std::string GetPS5Version(const std::string &jsonpath);
std::vector<uint8_t> readFile(std::string filename);

void handleIPC(clientArgs *client, std::string &inputStr,
               DaemonCommands command) {

  int sender_app = client->socket;

  std::string path_buf, path_buf2, json_path;

  char temp[0x255];
  std::string out_var = "Nothing"; // default send var

  LOG_INFO("Received IPC command 0x%X", command);
  // LOG_INFO("Received IPC data: %s", inputStr.c_str());

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
  case BREW_UTIL_SHELLUI_ON_STANDBY: {
    LOG_INFO("ShellUI on standby");
    onion::rest_mode::on_standby();
    reply(sender_app, false);
    break;
  }
  case BREW_UTIL_TOGGLE_FTP: {
    const bool enabled = onion_cjson::bool_item(my_json.get(), "enabled");
    onion::Settings settings{};
    (void)onion::settings_load(&settings);
    settings.ftp_server = enabled;
    if (!onion::settings_save(settings)) {
      LOG_ERROR("FTP toggle: failed to persist settings");
      reply(sender_app, true);
      break;
    }
    LOG_INFO("FTP toggle -> %s", enabled ? "on" : "off");
    ftp_server_apply(enabled);
    reply(sender_app, false);
    break;
  }
  case BREW_UTIL_UNUSED_KLOG:
  case BREW_UTIL_UNUSED_DPI:
    /* Klog (9081) and DirectPKGInstaller remain removed; ordinals are kept for IPC compat. */
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

    std::string tmp, game_version;
    bool is_PS5 = tid.rfind("PPSA", 0) == 0; // Check if tid starts with "PPSA"
    if (is_PS5) {
      // Attempt to load JSON files for PS5 games
      tmp = "/system_data/priv/appmeta/" + tid + "/param.json";
      if (!if_exists(tmp.c_str())) {
        LOG_INFO("%s: json %s does not exist", tid.c_str(), tmp.c_str());
        tmp = "/system_data/priv/appmeta/external/" + tid + "/param.json";

        if (!if_exists(tmp.c_str())) {
          LOG_INFO("%s: json %s does not exist", tid.c_str(), tmp.c_str());
          tmp = "/system_ex/app/" + tid + "/sce_sys/param.json";
          if (!if_exists(tmp.c_str())) {
            LOG_INFO("%s: json %s does not exist", tid.c_str(), tmp.c_str());
            onion_notify(true, "notify.game.version_failed");
            reply(sender_app, true);
            break;
          }
        }
      }

      game_version = GetPS5Version(tmp);
      if (game_version.empty()) {
        onion_notify(true, "notify.game.version_failed");
        LOG_ERROR("Failed to get game version for PS5 Game");
        reply(sender_app, true);
        break;
      }
    } else {
      // Attempt to load SFO files for PS4 games
      tmp = "/system_data/priv/appmeta/" + tid + "/param.sfo";
      if (!if_exists(tmp.c_str())) {
        LOG_INFO("%s: sfo %s does not exist", tid.c_str(), tmp.c_str());
        tmp = "/system_data/priv/appmeta/external/" + tid + "/param.sfo";
        if (!if_exists(tmp.c_str())) {
          LOG_INFO("%s: sfo %s does not exist", tid.c_str(), tmp.c_str());
          onion_notify(true, "notify.game.version_failed");
          reply(sender_app, true);
          break;
        }
      }

      std::vector<uint8_t> sfo_data = readFile(tmp);
      if (sfo_data.empty()) {
        onion_notify(true, "notify.game.sfo_failed");
        reply(sender_app, true);
        break;
      }

      SfoReader sfo(sfo_data);
      // VERSION key holds the original version, it doesn't change if updated
      try {
          std::string version_str = sfo.GetValueFor<std::string>("VERSION");
          std::string app_ver_str = sfo.GetValueFor<std::string>("APP_VER");

          float version_val = std::stof(version_str);
          float app_ver_val = std::stof(app_ver_str);

          game_version = (version_val > app_ver_val) ? version_str : app_ver_str;
      }
      catch (const std::exception& e) {
          // Fallback to APP_VER if there's an issue
          game_version = sfo.GetValueFor<std::string>("APP_VER");
      }
    }

    LOG_INFO("Version: %s", game_version.c_str());
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
    std::string version =
        std::string(onion_cjson::string_item(my_json.get(), "version", ""));
    int pid = onion_cjson::int_item(my_json.get(), "pid");
    int appid = onion_cjson::int_item(my_json.get(), "appid");
    std::string shm_path = "/user/data/OnionHEN/" + title_id + "_cheats";

    auto &cheats = onion::cheats::CheatService::instance();
    cheats.ensureDir();
    if (cheats.exportList(title_id, version, pid, appid, shm_path) == 0) {
      reply(sender_app, false, shm_path);
    } else {
      onion_notify(true, "notify.cheats.none", title_id.c_str(),
             version.c_str());
      reply(sender_app, true);
    }
    break;
  }

  case BREW_UTIL_TOGGLE_CHEAT: {
    std::string title_id =
        std::string(onion_cjson::string_item(my_json.get(), "tid", ""));
    std::string version =
        std::string(onion_cjson::string_item(my_json.get(), "version", ""));
    int pid = onion_cjson::int_item(my_json.get(), "pid");
    int appid = onion_cjson::int_item(my_json.get(), "appid");
    int cheat_id = onion_cjson::int_item(my_json.get(), "cheat_id");
    std::string status;

    LOG_INFO("Received toggle command for cheat %d on %s PID %d",
                 cheat_id, title_id.c_str(), pid);

    auto &cheats = onion::cheats::CheatService::instance();
    if (cheats.toggle(pid, appid, title_id, version, cheat_id, status) == 0) {
      LOG_INFO("Cheat toggle ok: %s", status.c_str());
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
    const auto started = CheatSyncService::instance().start(
        g_settings.snapshot(),
        (catalog && catalog[0]) ? catalog : nullptr,
        (mirror && mirror[0]) ? mirror : nullptr);
    if (started == CheatSyncService::StartResult::AlreadyRunning) {
      onion_notify(true, "notify.cheats.sync.busy");
      reply(sender_app, false, "{\"state\":\"already_running\"}");
    } else if (started == CheatSyncService::StartResult::Rejected) {
      reply(sender_app, true, "{\"state\":\"rejected\"}");
    } else {
      reply(sender_app, false, "{\"state\":\"started\"}");
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
    char body[640];
    std::snprintf(body, sizeof(body),
                  "{\"state\":\"%s\",\"mirror\":\"%s\","
                  "\"catalog\":\"%s\",\"error\":\"%s\",\"phase\":\"%s\","
                  "\"progress\":%d,\"completed\":%zu,\"total\":%zu}",
                  state,
                  st.mirror == onion::cheats::sync::CheatMirrorId::Cnb
                      ? "cnb"
                      : "github",
                  st.catalog_id.c_str(), st.error.c_str(),
                  st.phase.c_str(), st.progress_percent, st.completed,
                  st.total);
    reply(sender_app, false, body);
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
