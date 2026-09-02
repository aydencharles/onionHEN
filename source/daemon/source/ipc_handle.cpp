/* Copyright (C) 2025 OnionHEN / LightningMods
 * Crit daemon IPC command dispatch.
 * Transport (listen/accept/thread) stays in msg.cpp.
 */
#include "daemon_ops.hpp"
#include "ipc.hpp"
#include <onion/platform.h>
#include <onion/settings.hpp>
#include "onion_cjson.hpp"
#include "globalconf.hpp"
#include "plugin_manager_runtime.hpp"
#include <msg.hpp>
#include <atomic>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <strings.h>
#include <algorithm>



bool copyRecursive(const char *source, const char *destination);
bool copyFile(const char *source, const char *destination);
void calculateSize(uint64_t size, char *result);
uint64_t calculateTotalSize(const char *path);
extern "C" int unmount(const char *path, int flags);

namespace {

std::string plugin_inventory_page(size_t offset) {
  constexpr size_t kPageSize = 8;
  const std::vector<onion::plugin::InventoryEntry> inventory =
      onion::daemon::plugins::inventory();
  const size_t begin = std::min(offset, inventory.size());
  const size_t end = std::min(begin + kPageSize, inventory.size());

  cJSON *root = cJSON_CreateObject();
  cJSON *items = cJSON_CreateArray();
  cJSON_AddItemToObject(root, "plugins", items);
  for (size_t index = begin; index < end; ++index) {
    const onion::plugin::InventoryEntry &entry = inventory[index];
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", entry.descriptor.plugin_id.c_str());
    cJSON_AddStringToObject(item, "version", entry.descriptor.version.c_str());
    cJSON_AddStringToObject(item, "name", entry.descriptor.name.c_str());
    cJSON_AddBoolToObject(item, "running", entry.running());
    cJSON_AddBoolToObject(item, "auto_start", entry.auto_start());
    cJSON_AddItemToArray(items, item);
  }
  cJSON_AddNumberToObject(root, "next",
                          end < inventory.size() ? static_cast<double>(end)
                                                 : -1.0);
  return onion_cjson::print_owned(root);
}

onion::plugin::OperationResult plugin_operation(DaemonCommands command,
                                                std::string_view plugin_id) {
  switch (command) {
  case BREW_PLUGIN_START:
    return onion::daemon::plugins::start_plugin(plugin_id);
  case BREW_PLUGIN_STOP:
    return onion::daemon::plugins::stop_plugin(plugin_id);
  case BREW_PLUGIN_RELOAD:
    return onion::daemon::plugins::reload_plugin(plugin_id);
  case BREW_PLUGIN_DELETE:
    return onion::daemon::plugins::remove_plugin(plugin_id);
  default:
    return {false, "unsupported plugin operation"};
  }
}

} // namespace

void handleIPC(clientArgs *client, std::string &inputStr,
               DaemonCommands command) {

  int sender_app = client->socket;

  struct stat buffer;
  std::string path_buf, path_buf2, json_path;
  const char *path = nullptr, *dest = nullptr;
  char size_buf[0x255];

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
  case BREW_TEST_CONNECTION: {
    reply(sender_app, false, out_var);
    break;
  }
  case BREW_PLUGIN_LIST: {
    const int offset = onion_cjson::int_item(my_json.get(), "offset", 0);
    if (offset < 0) {
      reply(sender_app, true, "invalid plugin inventory offset");
      break;
    }
    const std::string page = plugin_inventory_page(static_cast<size_t>(offset));
    if (page.size() >= DAEMON_BUFF_MAX) {
      reply(sender_app, true, "plugin inventory page exceeds IPC frame");
      break;
    }
    reply(sender_app, false, page);
    break;
  }
  case BREW_PLUGIN_START:
  case BREW_PLUGIN_STOP:
  case BREW_PLUGIN_RELOAD:
  case BREW_PLUGIN_DELETE: {
    const char *plugin_id =
        onion_cjson::string_item(my_json.get(), "plugin_id");
    if (!plugin_id || !*plugin_id) {
      reply(sender_app, true, "missing plugin_id");
      break;
    }
    const onion::plugin::OperationResult result =
        plugin_operation(command, plugin_id);
    reply(sender_app, !result.success,
          result.success ? std::string("ok") : result.error);
    break;
  }
  case BREW_ENABLE_TOOLBOX: {
    LOG_DEBUG("rest: IPC BREW_ENABLE_TOOLBOX");
    if(cmd_enable_toolbox()){
        LOG_DEBUG("rest: BREW_ENABLE_TOOLBOX ok");
        reply(sender_app, false);
    } else {
        LOG_DEBUG("rest: BREW_ENABLE_TOOLBOX failed");
        reply(sender_app, true);
    }
    break;
  }
  case BREW_LAST_RET: {
    /* Tracks last reply() error for this process (not this call's local). */
    const int last = daemon_last_ipc_error();
    reply(sender_app, last != 0, last != 0 ? "1" : "0");
    break;
  }
  case BREW_UNUSED_ACTIVATE_DUMPER:
    LOG_WARN("BREW_ACTIVATE_DUMPER: unsupported (legacy dumper removed)");
    reply(sender_app, true);
    break;
  case BREW_UNUSED_DECRYPT_DIR:
    /* SELF directory decrypt removed from OnionHEN. */
    LOG_WARN("BREW_DECRYPT_DIR: unsupported (removed)");
    reply(sender_app, true);
    break;
  case BREW_UNUSED_TESTKIT_CHECK:
    /* Prefer local probe in clients; keep ordinal for IPC compatibility. */
    LOG_WARN("BREW_TESTKIT_CHECK: unsupported (removed)");
    reply(sender_app, true);
    break;
  case BREW_REMOUNT_FOLDER:
    path_buf = std::string(onion_cjson::string_item(my_json.get(), "mount_dest", ""));
    path_buf2 = std::string(onion_cjson::string_item(my_json.get(), "mount_src", ""));
    json_path = path_buf + "/sce_sys/param.json";
    LOG_DEBUG("remount source selected: %s", path_buf2.c_str());

    if(path_buf.rfind("/user") == std::string::npos && path_buf.length() <= strlen("/system_ex/app/")) {
      onion_notify(true, "notify.ipc.invalid_path", path_buf.length());
      reply(sender_app, true);
      break;
    }

    mkdir(path_buf.c_str(), 0777);

    if (if_exists(json_path.c_str())) {
      LOG_DEBUG("param.json exists, trying to unmount");
      int retries = 0;
      do {
        if (retries == 0)
          LOG_DEBUG("unmounting before remount");
        else
          LOG_ERROR("retrying attempt unmounting %d | prev. error %s", retries, strerror(errno));

        if (retries >= 20) {
          onion_notify(true, "notify.fs.unmount",
                 strerror(errno));
          reply(sender_app, true);
          break;
        }
        retries++;

      } while (unmount(path_buf.c_str(), MNT_FORCE) < 0);
    }

    if (!remount(path_buf2.c_str(), path_buf.c_str(), MNT_FORCE)) {
      if (errno == EBADF || errno == EPERM ||
          errno == EIO) { // if anyone repots a game not mounting til the 2nd
                          // time look at this
        LOG_DEBUG("remount failed, trying to unmount first");
        unmount(path_buf.c_str(), MNT_FORCE);
      }
      if (!remount(path_buf2.c_str(), path_buf.c_str(), MNT_UPDATE)) {
        onion_notify(true, "notify.fs.remount", strerror(errno),
               path_buf2.c_str());
        LOG_ERROR("remount error: %s Path: %s", strerror(errno),
                   path_buf2.c_str());
        reply(sender_app, true);
        break;
      } 
    }

    reply(sender_app, false);
    break;
  case BREW_STAT_CMD: {
    path = onion_cjson::string_item(my_json.get(), "path");
    if (!path || !*path) {
      LOG_ERROR("BREW_STAT_CMD: missing path");
      reply(sender_app, true);
      break;
    }
    if (stat(path, &buffer) == 0) {
      snprintf(size_buf, sizeof(size_buf), "%ld", buffer.st_size);
      LOG_DEBUG("%s exists | size %s", path, size_buf);
      reply(sender_app, false, size_buf);
    } else {
      LOG_ERROR("error for %s | %s", path, strerror(errno));
      reply(sender_app, true);
    }
    break;
  }
  case BREW_CALC_DIR_SIZE: {
    path = onion_cjson::string_item(my_json.get(), "path");
    if (!path || !*path) {
      LOG_ERROR("BREW_CALC_DIR_SIZE: missing path");
      reply(sender_app, true);
      break;
    }
    uint64_t size = calculateTotalSize(path);
    snprintf(size_buf, sizeof(size_buf), "%lu",
             static_cast<unsigned long>(size));
    LOG_DEBUG("calculated directory size %s", size_buf);
    reply(sender_app, false, size_buf);
    break;
  }
  case BREW_COPY_FILE: {
    path = onion_cjson::string_item(my_json.get(), "path");
    dest = onion_cjson::string_item(my_json.get(), "dest");
    if (!path || !*path || !dest || !*dest) {
      LOG_ERROR("BREW_COPY_FILE: missing path/dest");
      reply(sender_app, true);
      break;
    }
    if (copyFile(path, dest)) {
      reply(sender_app, false);
    } else {
      LOG_ERROR("error for %s | %s", path, strerror(errno));
      reply(sender_app, true);
    }
    break;
  }
  case BREW_COPY_DIR: {
    path = onion_cjson::string_item(my_json.get(), "path");
    dest = onion_cjson::string_item(my_json.get(), "dest");
    if (!path || !*path || !dest || !*dest) {
      LOG_ERROR("BREW_COPY_DIR: missing path/dest");
      reply(sender_app, true);
      break;
    }
    snprintf(size_buf, sizeof(size_buf), "%lu",
             static_cast<unsigned long>(calculateTotalSize(path)));
    if (copyRecursive(path, dest)) {
      reply(sender_app, false, size_buf);
    } else {
      LOG_ERROR("error for %s | %s", path, strerror(errno));
      reply(sender_app, true);
    }
    break;
  }
  case BREW_DELETE_DIR: {
    path = onion_cjson::string_item(my_json.get(), "path");
    if (!path || !*path) {
      LOG_ERROR("BREW_DELETE_DIR: missing path");
      reply(sender_app, true);
      break;
    }
    if (rmtree(path)) {
      reply(sender_app, false);
    } else {
      reply(sender_app, true);
    }
    break;
  }
  case BREW_TEST_SB_FILE: {
    path = onion_cjson::string_item(my_json.get(), "path");
    if (!path || !*path) {
      LOG_ERROR("BREW_TEST_SB_FILE: missing path");
      reply(sender_app, true);
      break;
    }
    reply(sender_app, !test_sb_file(path));
    break;
  }
  case BREW_DAEMON_PID: {
    snprintf(size_buf, sizeof(size_buf), "%d", getpid());
    reply(sender_app, false, size_buf);
    break;
  }
  case BREW_UNUSED_1: {
    // This command is not used anymore but kept for backwards compatibility
    onion_notify(true, "notify.ipc.unused");
    reply(sender_app, true);
    break;
  }
  case BREW_ADJUST_FAN_SPEED: {
    int speed = onion_cjson::int_item(my_json.get(), "speed");
    int enabled = onion_cjson::int_item(my_json.get(), "enabled");
    LOG_DEBUG("Adjusting Fan Speed to: %d", speed);
    if (speed < onion::kFanThresholdMinCelsius ||
        speed > onion::kFanThresholdMaxCelsius) {
      onion_notify(true, "notify.fan.invalid_speed", speed);
      reply(sender_app, true);
      break;
    }

    if (!enabled) {
      onion_notify(true, "notify.fan.disabled");
      restore_automatic_fan();
      const onion::Settings saved = g_settings.update([](onion::Settings &s) {
        s.enable_fan_speed = false;
      });
      if (onion::settings_save(saved)) {
        SettingsNoteDiskWritten();
      } else {
        LOG_ERROR("Fan disable: failed to persist settings");
      }
      reply(sender_app, false);
      break;
    }

    if (set_fan_threshold(speed)) {
      onion_notify(true, "notify.fan.adjusted", speed);
      const onion::Settings saved = g_settings.update([speed](onion::Settings &s) {
        s.enable_fan_speed = true;
        s.fan_threshold = speed;
      });
      if (onion::settings_save(saved)) {
        SettingsNoteDiskWritten();
      } else {
        LOG_ERROR("Fan enable: memory updated but disk twin save failed");
      }
      reply(sender_app, false);
    } else {
      onion_notify(true, "notify.fan.adjust_failed");
      reply(sender_app, true);
    }
    break;
  }
  case BREW_KILL_DAEMON: {
    /* Soft self-exit only (legacy). Prefer BREW_SHUTDOWN_STACK for full teardown. */
    is_handler_enabled = false;
    reply(sender_app, false);
    usleep(50 * 1000);
    exit(1337);
    break;
  }
  case BREW_SHUTDOWN_STACK: {
    /* Reply first so Unix/TCP clients get a frame before we tear down. */
    LOG_INFO("BREW_SHUTDOWN_STACK from client");
    reply(sender_app, false, "shutting down");
    usleep(100 * 1000);
    cmd_shutdown_onion_stack();
    break;
  }
  case BREW_FORCE_KILL_PID: {
    int pid = onion_cjson::int_item(my_json.get(), "pid");
    /* <=1: invalid / system; never TerminateProcess(1) (hangs ShellUI IPC). */
    if (pid <= 1) {
      LOG_ERROR("Invalid/system PID for FORCE_KILL: %d", pid);
      reply(sender_app, true);
      break;
    }

    ForceKillProc(pid);
    reply(sender_app, false);
    break;
  }
  case BREW_RELOAD_SETTINGS: {
    LoadSettings(/*force=*/true);
    onion_notify(true, "notify.settings.reloaded");
    reply(sender_app, false);
    break;
  }
  case BREW_CHMOD_DIR: {
	LOG_DEBUG("BREW_CHMOD_DIR called");
    path = onion_cjson::string_item(my_json.get(), "path");
    if(!path) {
      LOG_ERROR("Invalid path for chmod");
      reply(sender_app, true);
      break;
	}
   // kernel_set_ucred_authid(getpid(), 0x4801000000000013L);
	  change_permissions_recursive(path);
	  reply(sender_app, false);
    break;
  }
  default:
    onion_notify(true, "notify.ipc.unknown_command", command);
    reply(sender_app, true);
    break;
  }
}
