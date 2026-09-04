/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "daemon_ops.hpp"
#include "plugin_ipc_server.hpp"
#include "plugin_manager_runtime.hpp"
#include "app_lifecycle_runtime.hpp"
#include "sprx_plugin_manager_runtime.hpp"
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/ipc_server.hpp>
#include <onion/system_tmp.h>
#include <msg.hpp>
#include <atomic>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mount.h>

extern "C" {
int nmount(struct iovec *iov, unsigned int niov, int flags);
int sceKernelGetProcessName(int pid, char *name);
int sceSystemServiceGetAppIdOfRunningBigApp();
int _sceApplicationGetAppId(int pid, int *appid);
int sceKernelTerminateProcess(int pid, int *ret);
}

// set_proc_authid from libonion_proc — freestanding kernel types, include carefully
extern "C" uintptr_t set_proc_authid(pid_t pid, uintptr_t new_authid);

struct NonStupidIovec {
  const void *iov_base;
  size_t iov_length;

  constexpr NonStupidIovec(const char *str)
      : iov_base(str), iov_length(__builtin_strlen(str) + 1) {}
  constexpr NonStupidIovec(const char *str, size_t length)
      : iov_base(str), iov_length(length) {}
};

constexpr NonStupidIovec operator""_iov(const char *str, unsigned long len) {
  return {str, len + 1};
}
bool remount(const char *dev, const char *path, int mnt_flag) {
  NonStupidIovec iov[]{
      "fstype"_iov, "nullfs"_iov, "fspath"_iov, {path},
      "target"_iov, {dev},        "rw"_iov,     {nullptr, 0},
  };
  constexpr size_t iovlen = sizeof(iov) / sizeof(iov[0]);
  return nmount(reinterpret_cast<struct iovec *>(iov), iovlen, mnt_flag) == 0;
}

int change_permissions_recursive(const char* path) {
    struct stat statbuf;
    struct dirent* entry;
    DIR* dir;
    int result = 0;

    if (!path || strlen(path) == 0) {
        LOG_ERROR( "Invalid path provided");
        return -1;
    }

    if (lstat(path, &statbuf) != 0) {
        LOG_ERROR( "Failed to stat '%s': %s", path, strerror(errno));
        return -1;
    }

    if (S_ISLNK(statbuf.st_mode)) {
        LOG_WARN("Skipping symbolic link: %s", path);
        return 0;
    }

    // Skip special files (devices, pipes, sockets, etc.)
    if (!S_ISREG(statbuf.st_mode) && !S_ISDIR(statbuf.st_mode)) {
        LOG_WARN("Skipping special file: %s", path);
        return 0;
    }

    if (!S_ISDIR(statbuf.st_mode)) {
        if (chmod(path, 0777) != 0) {
            LOG_ERROR( "Failed to chmod '%s': %s", path, strerror(errno));
            return -1;
        }
        return 0;
    }

    dir = opendir(path);
    if (!dir) {
        LOG_ERROR( "Failed to open directory '%s': %s", path, strerror(errno));
        return -1;
    }

    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        size_t path_len = strlen(path);
        size_t name_len = strlen(entry->d_name);

        if (path_len + name_len + 2 > PATH_MAX) {
            LOG_ERROR("Path too long: %s/%s", path, entry->d_name);
            result = -1;
            continue;
        }

        char newpath[PATH_MAX];
        int ret = snprintf(newpath, sizeof(newpath), "%s/%s", path, entry->d_name);
        if (ret >= sizeof(newpath)) {
            LOG_ERROR("Path truncated: %s/%s", path, entry->d_name);
            result = -1;
            continue;
        }

        if (change_permissions_recursive(newpath) != 0) {
            result = -1;
        }
    }

    if (errno != 0) {
        LOG_ERROR( "Error reading directory '%s': %s", path, strerror(errno));
        result = -1;
    }

    closedir(dir);

    if (chmod(path, 0777) != 0) {
        LOG_ERROR( "Failed to chmod directory '%s': %s", path, strerror(errno));
        return -1;
    }

    return result;
}

#define READ_SIZE 0x1024

bool test_sb_file(const char *filename) {
  if (!filename) {
    LOG_ERROR("test_sb_file: filename is null");
    return false;
  }

  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("test_sb_file: Failed to open %s", filename);
    return false;
  }

  // Determine the size of the file
  struct stat fileInfo;
  if (fstat(fd, &fileInfo) < 0) {
    LOG_ERROR("test_sb_file: Failed to get file size for %s", filename);
    close(fd);
    return false;
  }

  off_t fileSize = fileInfo.st_size;
  char buffer[READ_SIZE];

  // Read start
  if (read(fd, buffer, READ_SIZE) < 0) {
    LOG_ERROR("test_sb_file: Failed to read start of %s", filename);
    close(fd);
    return false;
  }

  // Calculate middle, ensuring we don't try to seek beyond the file size
  off_t middlePosition =
      fileSize / 2 > READ_SIZE ? fileSize / 2 - READ_SIZE / 2 : 0;
  if (lseek(fd, middlePosition, SEEK_SET) < 0 ||
      read(fd, buffer, READ_SIZE) < 0) {
    LOG_ERROR("test_sb_file: Failed to read middle of %s", filename);
    close(fd);
    return false;
  }

  // Read end
  off_t endPosition = fileSize > READ_SIZE ? fileSize - READ_SIZE : 0;
  if (lseek(fd, endPosition, SEEK_SET) < 0 || read(fd, buffer, READ_SIZE) < 0) {
    LOG_ERROR("test_sb_file: Failed to read end of %s", filename);
    close(fd);
    return false;
  }

  close(fd);
  LOG_DEBUG("test_sb_file: successfully sampled %s", filename);
  return true;
}


namespace {
std::atomic<int> g_last_ipc_error{0};
} // namespace

int daemon_last_ipc_error() {
  return g_last_ipc_error.load(std::memory_order_relaxed);
}

void reply(int sender_socket, bool error, std::string out_var) {
  g_last_ipc_error.store(error ? -1 : 0, std::memory_order_relaxed);
  onion::ipc_reply(sender_socket, BREW_RETURN_VALUE, error, out_var);
}

int get_shellui_pid() {
  /* sysctl allproc via libonion_proc — no 0..9999 PID scan. */
  return static_cast<int>(onion_find_pid("SceShellUI"));
}


int get_game_pid() {
  /*
   * Running BigApp process: onion_find_pid_ex with for_bigapp requires SCE
   * hooks registered in daemon main (process name + app info + bigapp id).
   * Returns process pid (not appid).
   */
  pid_t pid = onion_find_pid_ex(/*name=*/"", /*needle=*/false,
                                /*for_bigapp=*/true, /*need_eboot=*/false);
  if (pid > 0) {
    return static_cast<int>(pid);
  }

  /* Fallback: scan via application API when hooks unavailable. */
  char proc_name[255] = {0};
  int app_pid = -1;
  int appid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (appid < 0) {
    return -1;
  }

  for (int j = 1; j <= 9999; j++) {
    int bappid = 0;
    if (_sceApplicationGetAppId(j, &bappid) < 0)
      continue;
    if (appid != bappid)
      continue;
    app_pid = j;
    if (sceKernelGetProcessName(app_pid, proc_name) < 0) {
      LOG_ERROR("sceKernelGetProcessName failed for (%d)", app_pid);
    }
    break;
  }
  return app_pid;
}

void ForceKillProc(int pid) {
  /* PID 0/1 are kernel/init-class; never target them (payload path once
   * stored pid=1 as "unknown" and ShellUI hung on TerminateProcess(1)). */
  if (pid <= 1) {
    LOG_ERROR("ForceKillProc: refusing invalid/system pid=%d", pid);
    return;
  }
  if (pid == getpid()) {
    LOG_WARN("ForceKillProc: refusing self pid=%d", pid);
    return;
  }

  #define DECID_AUTH_ID 0x4800000000000022 // required for killing with sceKernelTerminateProcess / sys_proc_term  syscall
  uintptr_t authid = set_proc_authid(getpid(), DECID_AUTH_ID);

  LOG_INFO("Terminating pid=%d", pid);
  int ret = 0;
  if (sceKernelTerminateProcess(pid, &ret) != 0) {
    LOG_ERROR("sceKernelTerminateProcess(%d) failed ret=%d — SIGKILL fallback",
                 pid, ret);
    if (kill(pid, SIGKILL) != 0) {
      LOG_ERROR("kill(%d, SIGKILL) failed: %s", pid, strerror(errno));
    } else {
      LOG_INFO("SIGKILL sent to pid=%d", pid);
    }
  } else {
    LOG_INFO("Successfully terminated process with PID: %d", pid);
  }

  set_proc_authid(getpid(), authid); // Restore original authid
}

static void shutdown_owned_process(const char *label, pid_t pid) {
  if (pid <= 1 || pid == getpid())
    return;
  LOG_INFO("shutdown: stopping owned %s pid=%d", label, static_cast<int>(pid));
  if (kill(pid, SIGKILL) != 0 && errno != ESRCH)
    ForceKillProc(static_cast<int>(pid));
  usleep(200 * 1000);
}

static void shutdown_restart_shellui(void) {
  int shellui_pid = get_shellui_pid();
  if (shellui_pid <= 0 || shellui_pid == getpid()) {
    LOG_ERROR("shutdown: SceShellUI not found (cannot restart)");
    return;
  }
  LOG_INFO("shutdown: restarting SceShellUI pid=%d", shellui_pid);
  ForceKillProc(shellui_pid);
  /* Home menu is respawned by the system after process death. */
  if (onion_proc_is_alive(shellui_pid)) {
    (void)kill(shellui_pid, SIGKILL);
    usleep(200 * 1000);
  }
}

/**
 * Tear down userland OnionHEN only.
 *
 * Never SIGKILL kstuff: unloading HV/kernel patches from userland leaves
 * half-torn fd/budget state (fdescfree BUDGET_FD_FILE) and panics. kstuff
 * stays until reboot.
 *
 * Order: util → private elfldr (:9020) → SceShellUI (allowed) → this daemon
 * exits.
 */
[[noreturn]] void cmd_shutdown_onion_stack(void) {
  LOG_INFO(
      "cmd_shutdown_onion_stack: util → elfldr(:9020) → restart ShellUI → self "
      "(leave kstuff)");

  /*
   * Order matters: arm stack-shutdown first so the runtime supervisor will not
   * relaunch :9020 or util after teardown begins. Then stop IPC and kill util.
   */
  g_stack_shutting_down.store(true, std::memory_order_release);
  is_handler_enabled = false;
  onion::daemon::plugins::stop();
  onion::daemon::app_lifecycle::stop();
  app_lifecycle_listener_stop();
  onion::daemon::sprx_plugins::stop();
  onion::daemon::plugin_ipc::stop();
  app_jailbreak_set_enabled(false);
  /* Let fifo_and_dumper_thread observe the flag before util vanishes. */
  usleep(100 * 1000);

  LOG_INFO("shutdown[1/4]: stop util");
  shutdown_owned_process("util", runtime_owned_util_pid());
  onion_ready_clear(ONION_READY_UTIL);

  LOG_INFO("shutdown[2/4]: stop private elfldr (:9020)");
  shutdown_owned_process("private elfldr", runtime_owned_private_loader_pid());
  unlink(ONION_SYSTEM_TMP_ELFLDR_STATE);
  unlink(ONION_SYSTEM_TMP_ELFLDR_BUSY);

  LOG_INFO("shutdown[3/4]: restart SceShellUI");
  shutdown_restart_shellui();

  LOG_INFO("shutdown[4/4]: exit daemon (kstuff intentionally left running)");
  onion_notify(true, "notify.stack.shutdown");
  usleep(200 * 1000);
  exit(0);
}
