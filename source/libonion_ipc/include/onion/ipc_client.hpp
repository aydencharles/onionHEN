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

#pragma once

#include <msg.hpp>
#include <onion/log.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

// ---------------------------------------------------------------------------
// Shared injectee-side IPC client (crit + util daemons).
//
// Design rules:
//   - One connection object per target (crit vs util). Never flip a flag on a
//     shared singleton — that races MainDaemonSocket / UtilDaemonSocket.
//   - IPCSendCommand is serialized with a per-instance mutex (shellui hooks +
//     background threads may share the same singleton).
//   - Implementation lives in the .cpp (not header-only).
//   - JSON string fields are built with cJSON (escape-safe).
// ---------------------------------------------------------------------------

// Logging: the shared LOG_* macros from <onion/log.h>, included above.

// Optional: host may install a notify sink (e.g. shellui bubble). Default: none.
using OnionIpcNotifyFn = void (*)(const char *text);
void onion_ipc_set_notify(OnionIpcNotifyFn fn);

class IPC_Client {
public:
  IPC_Client(const IPC_Client &) = delete;
  IPC_Client &operator=(const IPC_Client &) = delete;

  // Two true singletons: crit (false) and util (true). Safe to call from
  // concurrent contexts against different targets.
  static IPC_Client &getInstance(bool is_util_daemon);

  bool is_util() const { return util_daemon_; }

  void set_recv_timeout_ms(int ms) {
    recv_timeout_ms_.store(ms, std::memory_order_relaxed);
  }
  int recv_timeout_ms() const {
    return recv_timeout_ms_.load(std::memory_order_relaxed);
  }

  // Low-level transport
  int OpenConnection(const char *path);
  bool IPCOpenConnection();
  bool IPCOpenIfNotConnected();
  int IPCReceiveData(IPCMessage &msg, std::string &ipc_msg);
  int IPCSendData(const IPCMessage &msg);
  int IPCCloseConnection();
  bool IPCSendCommand(DaemonCommands cmd, std::string &ipc_msg1,
                      std::string ipc_msg2 = "");

  // High-level commands
  int GetDaemonPid();
  IPC_Ret ToggleSetting(DaemonCommands cmd, bool turn_on);
  /** Query the in-process FTP module without inspecting a PID marker. */
  bool FtpStatus();
  /** Ask util to rebind an enabled FTP listener after network resume. */
  bool RecoverFtp();
  void KillDaemon();
  /** Crit Unix IPC: util → restart ShellUI → daemon exit (BREW_SHUTDOWN_STACK). */
  void ShutdownStack();
  void ForceKillPID(int pid);
  IPC_Ret CopyFile(std::string src, std::string dest);
  IPC_Ret LaunchPayload(std::string payload_path, std::string tid);
  bool GameVerFromTid(std::string tid, std::string &out_ver);
  bool Remount(const char *src, const char *dest);
  bool GetGameCheats(const std::string &tid, const std::string &ver,
                     std::string &cheats, int pid = 0, int appid = 0);
  bool ToggleGameCheat(int pid, const std::string &tid, int cheat_index,
                       std::string &cheat_enabled,
                       const std::string &version = "");
  bool DownloadCheats(const char *catalog, const char *mirror,
                      std::string &out);
  bool CheatSyncStatus(std::string &out);
  bool CancelCheatSync(uint32_t task_id, std::string &out);
  void Reload_Daemon_Settings();
  bool Launch_Elfldr();
  bool Set_Fan_Threshold(int temp, bool enabled);
  /** Crit: inject ShellUI toolbox (BREW_ENABLE_TOOLBOX). */
  bool EnableToolbox();

  // Kept for call-site readability (matches historical public field).
  // Prefer is_util(); do not reassign after construction.
  const bool util_daemon;

private:
  explicit IPC_Client(bool util_daemon, int recv_timeout_ms);

  bool util_daemon_;
  int socket_fd_;
  std::atomic<int> recv_timeout_ms_;
  mutable std::mutex mu_;

  const char *socket_path() const;
  bool require_util(const char *what) const;
  bool require_crit(const char *what) const;

  /** Unlocked: full-frame send of IPCMessage. */
  int send_frame_unlocked(const IPCMessage &msg);
  /** Unlocked: full-frame recv into msg; forces NUL on payload. */
  int recv_frame_unlocked(IPCMessage &msg);
};
