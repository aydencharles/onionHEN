/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <onion/ipc_client.hpp>
#include <onion/ipc_server.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "onion_cjson.hpp"

extern "C" {
#include <ps5/klog.h>
}

namespace {

OnionIpcNotifyFn g_notify = nullptr;


std::string json_object_str(cJSON *j) {
  return onion_cjson::print_owned(j);
}

std::string json_kv_string(const char *k1, const char *v1) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, k1, v1 ? v1 : "");
  return json_object_str(j);
}

std::string json_kv_string2(const char *k1, const char *v1, const char *k2,
                            const char *v2) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, k1, v1 ? v1 : "");
  cJSON_AddStringToObject(j, k2, v2 ? v2 : "");
  return json_object_str(j);
}

std::string json_kv_number(const char *key, double value) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, key, value);
  return json_object_str(j);
}

std::string json_empty_object() { return json_object_str(cJSON_CreateObject()); }

void maybe_notify(const char *text) {
  if (g_notify && text) {
    g_notify(text);
  }
}

} // namespace

void onion_ipc_set_notify(OnionIpcNotifyFn fn) { g_notify = fn; }

IPC_Client::IPC_Client(bool util_daemon, int recv_timeout_ms)
    : util_daemon(util_daemon), util_daemon_(util_daemon), socket_fd_(-1),
      recv_timeout_ms_(recv_timeout_ms) {}

IPC_Client &IPC_Client::getInstance(bool is_util_daemon) {
  // Independent lifetimes and sockets — never share one object and flip flags.
  static IPC_Client crit(/*util=*/false, /*timeout_ms=*/25 * 1000);
  static IPC_Client util(/*util=*/true, /*timeout_ms=*/25 * 1000);
  return is_util_daemon ? util : crit;
}

const char *IPC_Client::socket_path() const {
  return util_daemon_ ? UTIL_IPC_SOC : CRIT_IPC_SOC;
}

bool IPC_Client::require_util(const char *what) const {
  if (util_daemon_) {
    return true;
  }
  LOG_DEBUG("%s: command is util-only", what);
  return false;
}

bool IPC_Client::require_crit(const char *what) const {
  if (!util_daemon_) {
    return true;
  }
  LOG_DEBUG("%s: command is crit-only", what);
  return false;
}

int IPC_Client::OpenConnection(const char *path) {
  const int soc = onion::ipc_unix_connect(path);
  if (soc < 0) {
    LOG_ERROR("Failed to connect to %s: %s", path, strerror(errno));
    return -1;
  }
  return soc;
}

bool IPC_Client::IPCOpenConnection() {
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
  socket_fd_ = OpenConnection(socket_path());
  return socket_fd_ >= 0;
}

bool IPC_Client::IPCOpenIfNotConnected() {
  if (socket_fd_ >= 0) {
    return true;
  }
  return IPCOpenConnection();
}

int IPC_Client::send_frame_unlocked(const IPCMessage &msg) {
  if (socket_fd_ < 0) {
    return -1;
  }
  const int ret = onion::ipc_network_send_full(
      socket_fd_, &msg, static_cast<int32_t>(sizeof(msg)));
  if (ret < 0) {
    LOG_ERROR("IPCSendData failed: %s", strerror(errno));
  } else {
    LOG_DEBUG("IPCSendData sent %i bytes", ret);
  }
  return ret;
}

int IPC_Client::recv_frame_unlocked(IPCMessage &msg) {
  bzero(msg.msg, sizeof(msg.msg));
  if (socket_fd_ < 0) {
    return -1;
  }

  struct timeval tv;
  const int recv_timeout_ms = recv_timeout_ms_.load(std::memory_order_relaxed);
  tv.tv_sec = recv_timeout_ms / 1000;
  tv.tv_usec = (recv_timeout_ms % 1000) * 1000;
  if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    return -1;
  }

  LOG_DEBUG("Waiting for daemon response (%s)...",
              util_daemon_ ? "util" : "crit");
  const int ret = onion::ipc_network_recv_full(
      socket_fd_, &msg, static_cast<int32_t>(sizeof(msg)));
  if (ret < 0) {
    LOG_ERROR("recv failed: %s", strerror(errno));
    return ret;
  }
  if (!onion::ipc_frame_is_complete(ret)) {
    LOG_DEBUG("short IPC frame %d (want %zu)", ret, sizeof(msg));
    return -1;
  }
  onion::ipc_message_force_nul(msg);
  return ret;
}

int IPC_Client::IPCReceiveData(IPCMessage &msg, std::string &ipc_msg) {
  const int ret = recv_frame_unlocked(msg);
  if (ret < 0) {
    return ret;
  }
  LOG_DEBUG("Daemon returned: %i", msg.error);

  const int reply_error = msg.error;
  if (reply_error != 0) {
    LOG_ERROR("Daemon returned an error: %i", reply_error);
  }

  if (strlen(msg.msg) <= 2) {
    LOG_DEBUG("Daemon message is empty");
    return reply_error != 0 ? reply_error : -1;
  }

  onion_cjson::Root j(msg.msg);
  if (!j) {
    LOG_ERROR("Failed to parse json: %s",
                cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown error");
    return reply_error != 0 ? reply_error : -1;
  }

  const char *var = onion_cjson::string_item(j.get(), "var");
  if (!var) {
    LOG_DEBUG("Daemon message does not contain the return obj");
    return reply_error != 0 ? reply_error : -1;
  }

  ipc_msg = var;
  LOG_DEBUG("Daemon IPC return obj: %s", ipc_msg.c_str());
  return reply_error;
}

int IPC_Client::IPCSendData(const IPCMessage &msg) {
  return send_frame_unlocked(msg);
}

int IPC_Client::IPCCloseConnection() {
  if (socket_fd_ < 0) {
    return -1;
  }
  close(socket_fd_);
  socket_fd_ = -1;
  return 0;
}

bool IPC_Client::IPCSendCommand(DaemonCommands cmd, std::string &ipc_msg1,
                                std::string ipc_msg2) {
  std::lock_guard<std::mutex> lock(mu_);
  std::string json;

#if SHELL_DEBUG == 1
  LOG_DEBUG("Sending command to %s daemon: 0x%X",
              util_daemon_ ? "util" : "crit", static_cast<unsigned>(cmd));
#endif

  IPCMessage msg{};
  msg.magic = 0xDEADBABE;
  msg.cmd = cmd;
  msg.error = 0;

  if (cmd == BREW_REMOUNT_FOLDER) {
    json = json_kv_string2("mount_src", ipc_msg1.c_str(), "mount_dest",
                           ipc_msg2.c_str());
  } else if (ipc_msg2.empty()) {
    if (cmd == BREW_DAEMON_PID || cmd == BREW_UTIL_DAEMON_PID ||
        cmd == BREW_TEST_CONNECTION || cmd == BREW_UTIL_TEST_CONNECTION) {
      json = json_kv_number("pid", 0);
    } else {
      json = json_kv_number("msg_1", 0);
    }
  } else {
    json = std::move(ipc_msg2);
  }

  if (!IPCOpenIfNotConnected()) {
    LOG_ERROR("Failed to open connection to daemon");
    return false;
  }

  snprintf(msg.msg, sizeof(msg.msg), "%s", json.c_str());
  onion::ipc_message_force_nul(msg);

  if (send_frame_unlocked(msg) < 0) {
    LOG_ERROR("Failed to send message to daemon");
    maybe_notify("Failed to send message to daemon");
    IPCCloseConnection();
    return false;
  }

  if (cmd == BREW_KILL_DAEMON || cmd == BREW_SHUTDOWN_STACK) {
    LOG_DEBUG("Daemon teardown cmd 0x%X sent", static_cast<unsigned>(cmd));
    /* Peer may exit before a full reply frame arrives. */
    return true;
  }

  IPC_Ret error = static_cast<IPC_Ret>(IPCReceiveData(msg, ipc_msg1));
  if (error == IPC_Ret::NO_ERROR) {
    LOG_ERROR("[Daemon] Daemon returned NO ERROR");
    return true;
  }
  LOG_ERROR("[Daemon] Daemon returned an ERROR");
  return false;
}

int IPC_Client::GetDaemonPid() {
  std::string ipc_msg;
  if (!IPCSendCommand(util_daemon_ ? BREW_UTIL_DAEMON_PID : BREW_DAEMON_PID,
                      ipc_msg)) {
    LOG_ERROR("Failed to get daemon pid");
    return -1;
  }
  LOG_DEBUG("Daemon pid: %s", ipc_msg.c_str());
  return atoi(ipc_msg.c_str());
}

IPC_Ret IPC_Client::ToggleSetting(DaemonCommands cmd, bool turn_on) {
  std::string ipc_msg;
  std::string json = json_kv_number("toggle", turn_on ? 1 : 0);
  if (!IPCSendCommand(cmd, ipc_msg, json)) {
    LOG_ERROR("Failed to toggle setting 0x%X (%d)", cmd, cmd);
    return IPC_Ret::OPERATION_FAILED;
  }
  LOG_DEBUG("Setting 0x%X (%d) toggled", cmd, cmd);
  return IPC_Ret::NO_ERROR;
}

bool IPC_Client::ShadowMountStatus() {
  std::string ipc_msg;
  if (!IPCSendCommand(BREW_UTIL_SHADOWMOUNT_STATUS, ipc_msg)) {
    LOG_ERROR("Failed to query ShadowMount service status");
    return false;
  }
  return ipc_msg == "1" || ipc_msg == "true";
}

bool IPC_Client::PkgNetStatus() {
  std::string ipc_msg;
  if (!IPCSendCommand(BREW_UTIL_PKGNET_STATUS, ipc_msg)) {
    LOG_ERROR("Failed to query pkg-server service status");
    return false;
  }
  return ipc_msg == "1" || ipc_msg == "true";
}

bool IPC_Client::SetSystemLanguage(int language) {
  if (!require_util("SetSystemLanguage")) {
    return false;
  }
  cJSON *j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, "lang", language);
  const std::string json = json_object_str(j);
  std::string ipc_msg;
  if (!IPCSendCommand(BREW_UTIL_SET_SYSTEM_LANG, ipc_msg, json)) {
    LOG_ERROR("Failed to push system language %d", language);
    return false;
  }
  return true;
}

void IPC_Client::KillDaemon() {
  std::string ipc_msg;
  IPCSendCommand(BREW_KILL_DAEMON, ipc_msg);
}

void IPC_Client::ShutdownStack() {
  if (!require_crit("ShutdownStack")) {
    return;
  }
  std::string ipc_msg;
  IPCSendCommand(BREW_SHUTDOWN_STACK, ipc_msg);
}

void IPC_Client::ForceKillPID(int pid) {
  if (!require_crit("ForceKillPID")) {
    return;
  }
  /* Never ask daemon to kill 0/1 — TerminateProcess(1) stalls ShellUI ~25s. */
  if (pid <= 1) {
    LOG_ERROR("ForceKillPID: refusing invalid/system pid=%d", pid);
    return;
  }
  std::string ipc_msg;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, "pid", pid);
  std::string json = json_object_str(j);
  IPCSendCommand(BREW_FORCE_KILL_PID, ipc_msg, json);
}

IPC_Ret IPC_Client::CopyFile(std::string src, std::string dest) {
  if (!require_crit("CopyFile")) {
    return IPC_Ret::INVALID;
  }
  std::string ipc_msg;
  std::string json =
      json_kv_string2("path", src.c_str(), "dest", dest.c_str());
  if (!IPCSendCommand(BREW_COPY_FILE, ipc_msg, json)) {
    LOG_ERROR("Failed to copy file");
    return IPC_Ret::OPERATION_FAILED;
  }
  return IPC_Ret::NO_ERROR;
}

IPC_Ret IPC_Client::LaunchPayload(std::string payload_path, std::string tid) {
  if (!require_util("LaunchPayload")) {
    return IPC_Ret::INVALID;
  }
  std::string ipc_msg;
  std::string json = json_kv_string2("payload_path", payload_path.c_str(),
                                     "title_id", tid.c_str());
  if (!IPCSendCommand(BREW_UTIL_LAUNCH_PAYLOAD, ipc_msg, json)) {
    LOG_ERROR("Failed to launch payload");
    return IPC_Ret::OPERATION_FAILED;
  }
  return IPC_Ret::NO_ERROR;
}

bool IPC_Client::GameVerFromTid(std::string tid, std::string &out_ver) {
  if (!require_util("GameVerFromTid")) {
    return false;
  }
  std::string json = json_kv_string("tid", tid.c_str());
  if (!IPCSendCommand(BREW_UTIL_GET_GAME_VER, out_ver, json)) {
    LOG_ERROR("Failed to get game name from tid");
    return false;
  }
  return true;
}

bool IPC_Client::Remount(const char *src, const char *dest) {
  if (!require_crit("Remount")) {
    return false;
  }
  std::string in = src ? src : "";
  if (!IPCSendCommand(BREW_REMOUNT_FOLDER, in, dest ? dest : "")) {
    LOG_ERROR("Failed to remount %s to %s", src, dest);
    return false;
  }
  return true;
}

bool IPC_Client::GetGameCheats(const std::string &tid, const std::string &ver,
                               std::string &cheats, int pid, int appid) {
  if (!require_util("GetGameCheats")) {
    return false;
  }
  cJSON *request = cJSON_CreateObject();
  cJSON_AddStringToObject(request, "tid", tid.c_str());
  cJSON_AddStringToObject(request, "version", ver.c_str());
  if (pid > 0) {
    cJSON_AddNumberToObject(request, "pid", pid);
  }
  if (appid > 0) {
    cJSON_AddNumberToObject(request, "appid", appid);
  }
  std::string json = json_object_str(request);
  if (!IPCSendCommand(BREW_UTIL_GET_GAME_CHEAT, cheats, json)) {
    LOG_ERROR("Failed to get cheats for %s", tid.c_str());
    return false;
  }
  return true;
}

bool IPC_Client::ToggleGameCheat(int pid, const std::string &tid,
                                 int cheat_index, std::string &cheat_enabled,
                                 const std::string &version) {
  if (!require_util("ToggleGameCheat")) {
    return false;
  }
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "tid", tid.c_str());
  cJSON_AddNumberToObject(j, "cheat_id", cheat_index);
  cJSON_AddNumberToObject(j, "pid", pid);
  cJSON_AddStringToObject(j, "version", version.c_str());
  std::string json = json_object_str(j);
  if (!IPCSendCommand(BREW_UTIL_TOGGLE_CHEAT, cheat_enabled, json)) {
    LOG_ERROR("Failed to enable cheats for %s", tid.c_str());
    return false;
  }
  return true;
}

bool IPC_Client::DownloadCheats(const char *catalog, const char *mirror,
                                std::string &out) {
  if (!require_util("DownloadCheats")) {
    return false;
  }
  cJSON *j = cJSON_CreateObject();
  if (catalog && catalog[0]) {
    cJSON_AddStringToObject(j, "catalog", catalog);
  }
  if (mirror && mirror[0]) {
    cJSON_AddStringToObject(j, "mirror", mirror);
  }
  std::string json = json_object_str(j);
  if (!IPCSendCommand(BREW_UTIL_DOWNLOAD_CHEATS, out, json)) {
    LOG_ERROR("Failed to start cheat catalog download");
    return false;
  }
  return true;
}

bool IPC_Client::CheatSyncStatus(std::string &out) {
  if (!require_util("CheatSyncStatus")) {
    return false;
  }
  if (!IPCSendCommand(BREW_UTIL_CHEAT_SYNC_STATUS, out,
                      json_empty_object())) {
    LOG_ERROR("Failed to query cheat sync status");
    return false;
  }
  return true;
}

bool IPC_Client::CancelCheatSync(uint32_t task_id, std::string &out) {
  if (!require_util("CancelCheatSync")) {
    return false;
  }
  if (!IPCSendCommand(BREW_UTIL_CANCEL_CHEAT_SYNC, out,
                      json_kv_number("task_id", task_id))) {
    LOG_ERROR("Failed to cancel cheat sync");
    return false;
  }
  return true;
}

void IPC_Client::Reload_Daemon_Settings() {
  std::string ipc_msg;
  if (!IPCSendCommand(BREW_RELOAD_SETTINGS, ipc_msg)) {
    LOG_ERROR("Failed to reload daemon settings");
  } else {
    LOG_DEBUG("Daemon settings reloaded successfully");
  }
}

bool IPC_Client::Launch_Elfldr() {
  LOG_WARN("Launch_Elfldr: unsupported "
              "(embedded 9020 is bootstrapper-managed)");
  return false;
}

bool IPC_Client::Set_Fan_Threshold(int temp, bool enabled) {
  if (!require_crit("Set_Fan_Threshold")) {
    return false;
  }
  cJSON *j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, "speed", temp);
  cJSON_AddNumberToObject(j, "enabled", enabled ? 1 : 0);
  std::string json = json_object_str(j);
  std::string ipc_msg;
  if (!IPCSendCommand(BREW_ADJUST_FAN_SPEED, ipc_msg, json)) {
    LOG_ERROR("Failed to adjust fan speed");
    return false;
  }
  return true;
}


bool IPC_Client::EnableToolbox() {
  if (!require_crit("EnableToolbox")) {
    return false;
  }
  std::string ipc_msg;
  // Historical payload titleId; daemon ignores body for enable path.
  std::string json = json_kv_string("titleId", "ETAH00002");
  if (!IPCSendCommand(BREW_ENABLE_TOOLBOX, ipc_msg, json)) {
    LOG_ERROR("EnableToolbox failed");
    return false;
  }
  return true;
}

bool IPC_Client::ListPlugins(std::vector<PluginInventoryItem> &plugins) {
  if (!require_crit("ListPlugins")) return false;
  plugins.clear();
  int offset = 0;
  for (;;) {
    cJSON *request = cJSON_CreateObject();
    cJSON_AddNumberToObject(request, "offset", offset);
    std::string response;
    if (!IPCSendCommand(BREW_PLUGIN_LIST, response,
                        json_object_str(request)))
      return false;

    onion_cjson::Root root(response);
    cJSON *items = root ? onion_cjson::item(root.get(), "plugins") : nullptr;
    if (!cJSON_IsArray(items)) return false;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, items) {
      const char *plugin_id = onion_cjson::string_item(item, "id");
      const char *version = onion_cjson::string_item(item, "version");
      const char *name = onion_cjson::string_item(item, "name");
      if (!plugin_id || !version || !name) return false;
      plugins.push_back({plugin_id, version, name,
                         onion_cjson::bool_item(item, "running"),
                         onion_cjson::bool_item(item, "auto_start")});
    }

    const int next = onion_cjson::int_item(root.get(), "next", -1);
    if (next < 0) return true;
    if (next <= offset) return false;
    offset = next;
  }
}

bool IPC_Client::PluginOperation(DaemonCommands command,
                                 const std::string &plugin_id) {
  if (!require_crit("PluginOperation")) return false;
  std::string response;
  return IPCSendCommand(command, response,
                        json_kv_string("plugin_id", plugin_id.c_str()));
}

bool IPC_Client::StartPlugin(const std::string &plugin_id) {
  return PluginOperation(BREW_PLUGIN_START, plugin_id);
}

bool IPC_Client::StopPlugin(const std::string &plugin_id) {
  return PluginOperation(BREW_PLUGIN_STOP, plugin_id);
}

bool IPC_Client::ReloadPlugin(const std::string &plugin_id) {
  return PluginOperation(BREW_PLUGIN_RELOAD, plugin_id);
}

bool IPC_Client::DeletePlugin(const std::string &plugin_id) {
  return PluginOperation(BREW_PLUGIN_DELETE, plugin_id);
}
