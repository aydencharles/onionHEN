#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string>
#include <sys/param.h>
#include <onion/system_tmp.h>

#define INVAIL -1
#define CRIT_IPC_SOC ONION_SYSTEM_TMP_CRIT_SOCKET
#define UTIL_IPC_SOC ONION_SYSTEM_TMP_UTIL_SOCKET
#define DAEMON_BUFF_MAX 0x1000


enum class IPC_Ret : int {
  NO_ERROR = 0,
  INVALID = -1,
  // Distinct from INVALID so callers can tell "wrong daemon" from "op failed".
  OPERATION_FAILED = -2,
  DEFAULT_RET = -1337
};

/* Underlying type is unsigned: several stable opcodes use the high bit set. */
enum DaemonCommands : unsigned int {
  BREW_TEST_CONNECTION = 0x9000000u,
  BREW_RETURN_VALUE = 0x9000002u,
  BREW_REMOUNT_FOLDER,
  // Original 0x9000004. Keep the slot so released Itemzflow clients retain
  // the numeric ABI even though OnionHEN does not bundle the legacy dumper.
  BREW_UNUSED_ACTIVATE_DUMPER,
  BREW_STAT_CMD,
  BREW_CALC_DIR_SIZE,
  BREW_COPY_FILE,
  BREW_COPY_DIR,
  BREW_DELETE_DIR,
  BREW_UNUSED_1,// not used anymore but kept fopr backwards compatibility for now
  BREW_TEST_SB_FILE,
  BREW_DAEMON_PID,
  BREW_UNUSED_STORE_INSTALLER,
  BREW_UNUSED_DECRYPT_DIR,  // was BREW_DECRYPT_DIR (SELF directory decrypt removed)
  BREW_LAST_RET,
  BREW_UNUSED_TESTKIT_CHECK, // was BREW_TESTKIT_CHECK (local probe used instead)
  BREW_ENABLE_TOOLBOX,
  BREW_CHMOD_DIR,
  BREW_ADJUST_FAN_SPEED,

  BREW_UTIL_TEST_CONNECTION = 0x8000000u,
  BREW_UTIL_RETURN_VALUE = 0x8000002u,
  BREW_UTIL_DAEMON_PID,
  BREW_UTIL_UNUSED_FTP_TOGGLE, // was built-in FTP toggle; keep released ordinal
  BREW_UTIL_UNUSED_KLOG, // was BREW_UTIL_TOGGLE_KLOG (service removed)
  BREW_UTIL_TOGGLE_PKGNET, // stable ordinal retained from the former DPI toggle
  BREW_UTIL_LAUNCH_PAYLOAD,
  BREW_UTIL_UNUSED_SHELLUI_ON_STANDBY, // was SHELLUI_ON_STANDBY; toolbox follows SceSysCore EXEC
  BREW_UTIL_GET_GAME_VER,
  BREW_UTIL_GET_GAME_CHEAT,
  BREW_UTIL_TOGGLE_CHEAT,
  BREW_UTIL_DOWNLOAD_CHEATS, // git catalog sync (was UNUSED after zip download removal)
  BREW_UTIL_UNUSED_RELOAD_CHEATS, // was RELOAD_CHEATS (index cache removed; file-sig hot-reload)
  BREW_UTIL_UNUSED_DOWNLOAD_KSTUFF, // was DOWNLOAD_KSTUFF (online kstuff download removed)
  BREW_UTIL_UNUSED_LEGACY_CMD_SERVER, // was TOGGLE_LEGACY_CMD_SERVER (TCP 9028 removed)
  BREW_UTIL_CHEAT_SYNC_STATUS, // last git cheat-sync snapshot
  BREW_UTIL_CANCEL_CHEAT_SYNC, // cooperatively stop the active catalog sync
  // Retain the two released ordinals; the former built-in service is gone.
  BREW_UTIL_UNUSED_LEGACY_SERVICE_SCAN,
  BREW_UTIL_UNUSED_LEGACY_SERVICE_TOGGLE,

  // Explicit values preserve the released ordinals after removing built-in FTP.
  BREW_UTIL_UNUSED_FTP_STATUS = 0x8000014u,
  BREW_UTIL_UNUSED_FTP_RECOVER = 0x8000015u,
  BREW_UTIL_UNUSED_SHADOWMOUNT_TOGGLE = 0x8000016u,
  BREW_UTIL_UNUSED_SHADOWMOUNT_STATUS = 0x8000017u,
  BREW_UTIL_PKGNET_STATUS = 0x8000018u,
  // Daemon pushes a live console system language so util re-applies it without
  // needing a SystemService query (util is under PTRACE_AUTHID after launch).
  BREW_UTIL_SET_SYSTEM_LANG = 0x8000019u,

  // Crit daemon plugin management. Payloads contain only validated plugin IDs;
  // filesystem paths never cross the IPC boundary.
  BREW_PLUGIN_LIST = 0x9000100u,
  BREW_PLUGIN_START = 0x9000101u,
  BREW_PLUGIN_STOP = 0x9000102u,
  BREW_PLUGIN_RELOAD = 0x9000103u,
  BREW_PLUGIN_DELETE = 0x9000104u,

  // Legacy: manual elfldr launch removed (embedded 9020 is bootstrapper-managed)
  BREW_UTIL_LAUNCH_ELFLDR = 0xE1F1D8u,
  BREW_RELOAD_SETTINGS = 0xC0FFEEu,

  // BREW_TOGGLE_PS5DEBUG removed (ps5debug no longer embedded)

  //KILL MAIN DAEMOM
  BREW_KILL_DAEMON = 0xDEAD0001u,
  /** Crit: stop owned util/loader → restart SceShellUI → exit daemon. */
  BREW_SHUTDOWN_STACK = 0xDEAD0002u,
  BREW_FORCE_KILL_PID = 0xDEADCAFEu,
};

/** TCP control port on crit daemon (LAN). Same wire as scripts/shutdown_onion.py. */
#define ONION_CTRL_TCP_PORT 9048
/** TCP control frame magic: ASCII 'ONIO' as BE u32. */
#define ONION_CTRL_TCP_MAGIC 0x4F4E494Fu
/** TCP cmd: full stack shutdown (util + ShellUI + daemon). */
#define ONION_CTRL_TCP_CMD_SHUTDOWN 1u

struct IPCMessage {
  int magic = 0xDEADBABE;
  enum DaemonCommands cmd;
  int error = 0;
  char msg[DAEMON_BUFF_MAX];
};
