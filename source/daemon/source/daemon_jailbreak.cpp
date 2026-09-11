/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Event-driven app jailbreak listener.
 *
 * Protocol (homebrew -> daemon):
 *   1. A process with a whitelisted Title ID starts.
 *   2. The app writes JSON {"PID":"<pid>"} (numeric PID also accepted) to
 *        /mnt/sandbox/<TID>_<000-050>/download0/<name>
 *      Accepted names:
 *        - etahen_jailbreak
 *        - onionhen_jailbreak
 *   3. SceSysCore NOTE_EXEC/NOTE_EXIT events identify app lifetime. Vnode
 *      events then follow sandbox -> slot -> download0 -> request file.
 *
 * There is no periodic foreground-app query or sandbox scan. The only scan of
 * the 51 possible slots happens when an allowed app starts, or when the
 * sandbox root reports that one of its direct children changed.
 */

#include "daemon_ops.hpp"
#include "app_lifecycle_runtime.hpp"
#include "globalconf.hpp"

#include <onion/app_jailbreak_policy.hpp>
#include <onion/app_lifecycle.hpp>
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/settings.hpp>
#include "onion_cjson.hpp"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  char title_id[14];
  char unknown2[0x3c];
} app_info_t;

extern "C" {
#include <ps5/kernel.h>

int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceKernelGetProcessName(int pid, char *name);
}

namespace {

pthread_mutex_t jb_control_lock = PTHREAD_MUTEX_INITIALIZER;
std::atomic_bool jb_listener_enabled{false};
int jb_control_write_fd = -1;

pthread_mutex_t lifecycle_control_lock = PTHREAD_MUTEX_INITIALIZER;
int lifecycle_control_write_fd = -1;

enum class LifecycleListenerControlMessage : char {
  Stop = 'S',
};

enum class ControlMessage : char {
  Rebuild = 'R',
  Lifecycle = 'L',
};

enum class AppJailbreakCommandType {
  BigAppStarted,
  BigAppExited,
};

struct AppJailbreakCommand {
  AppJailbreakCommandType type;
  pid_t pid;
  uint32_t app_id;
  std::string title_id;
  std::mutex completion_mutex;
  std::condition_variable completion_ready;
  bool completed = false;
  bool success = false;
};

struct ControlMessages {
  bool rebuild_requested = false;
  bool lifecycle_requested = false;
};

std::mutex app_jailbreak_commands_mutex;
std::deque<std::shared_ptr<AppJailbreakCommand>> app_jailbreak_commands;

constexpr auto kAppJailbreakCommandTimeout = std::chrono::seconds(2);
constexpr auto kAppJailbreakStartupTimeout = std::chrono::seconds(2);

onion::lifecycle::RuntimeStartupGate app_jailbreak_startup;
std::atomic_bool app_jailbreak_start_cancelled{false};

bool has_pending_app_jailbreak_commands() {
  std::lock_guard<std::mutex> lock(app_jailbreak_commands_mutex);
  return !app_jailbreak_commands.empty();
}

/** Same authid etaHEN / Hijacker::jailbreak historically used. */
constexpr uint64_t kJbAuthId = 0x4801000000000013ull;
constexpr size_t kMaxRequestBytes = 4096;
constexpr int kProcResolveAttempts = 3;
constexpr useconds_t kProcResolveUsleep = 30 * 1000;
constexpr const char *kMountRoot = "/mnt";
constexpr const char *kSandboxRoot = "/mnt/sandbox";
constexpr const char *kJailbreakReqNames[] = {
    "etahen_jailbreak",
    "onionhen_jailbreak",
};
constexpr const char *kShellUiTitleId = "NPXS40087";
constexpr const char *kShellUiProcessName = "SceShellUI";

struct ExecIdentity {
  char process_name[64] {};
  int name_rc = -1;
  app_info_t info {};
  int app_info_rc = -1;
  std::string tid;
};

ExecIdentity read_exec_identity(pid_t pid) {
  ExecIdentity id;
  id.name_rc = sceKernelGetProcessName(pid, id.process_name);
  id.app_info_rc = sceKernelGetAppInfo(pid, &id.info);
  if (id.app_info_rc == 0) {
    char title_id[sizeof(id.info.title_id) + 1] = {};
    memcpy(title_id, id.info.title_id, sizeof(id.info.title_id));
    id.tid = title_id;
  }
  return id;
}

bool identity_is_shellui(const ExecIdentity &id) {
  if (id.name_rc == 0 &&
      std::strcmp(id.process_name, kShellUiProcessName) == 0) {
    return true;
  }
  return id.app_info_rc == 0 && id.tid == kShellUiTitleId;
}

bool is_whitelisted_app(const std::string &tid,
                        const onion::AppJailbreakAllowlist &allowlist) {
  return onion::app_jailbreak::is_whitelisted(tid, allowlist);
}

int jb_read_uid(pid_t pid) {
  if (kernel_get_proc(pid) == 0) {
    return -1;
  }
  return static_cast<int>(kernel_get_ucred_uid(pid));
}

uint64_t jb_read_authid(pid_t pid) {
  if (kernel_get_proc(pid) == 0) {
    return 0;
  }
  return kernel_get_ucred_authid(pid);
}

/**
 * Full app jailbreak (uid0 + authid + caps + optional sandbox escape).
 * Uses ps5-payload-sdk kernel helpers so it tracks the FW symbols the HEN
 * already resolved into KERNEL_ADDRESS_*.
 */
bool jb_apply_privileges(pid_t pid, bool escape_sandbox) {
  static const uint8_t kFullCaps[16] = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  };

  const intptr_t kproc = kernel_get_proc(pid);
  if (kproc == 0) {
    LOG_DEBUG("[JB] kernel_get_proc(%d)=0 (ALLPROC=0x%lx rootvnode=0x%lx)",
             static_cast<int>(pid),
             static_cast<unsigned long>(KERNEL_ADDRESS_ALLPROC),
             static_cast<unsigned long>(KERNEL_ADDRESS_ROOTVNODE));
    return false;
  }

  if (kernel_set_ucred_uid(pid, 0) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_uid failed pid=%d",
              static_cast<int>(pid));
    return false;
  }
  (void)kernel_set_ucred_ruid(pid, 0);
  (void)kernel_set_ucred_svuid(pid, 0);
  (void)kernel_set_ucred_rgid(pid, 0);

  /* cr_ngroups is not exposed by SDK helpers; its ucred offset is 0x10. */
  const intptr_t ucred = kernel_get_proc_ucred(pid);
  if (ucred) {
    const uint32_t ngroups = 0;
    (void)kernel_copyin(&ngroups, ucred + 0x10, sizeof(ngroups));
  }

  if (kernel_set_ucred_authid(pid, kJbAuthId) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_authid failed pid=%d",
              static_cast<int>(pid));
    return false;
  }
  if (kernel_set_ucred_caps(pid, kFullCaps) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_caps failed pid=%d",
              static_cast<int>(pid));
    return false;
  }

  /* sceAttr byte at ucred+0x83 = 0x80 enables ptrace. */
  uint8_t attrs[32] = {};
  if (kernel_get_ucred_attrs(pid, attrs) != 0) {
    LOG_ERROR("[JB] kernel_get_ucred_attrs failed pid=%d",
              static_cast<int>(pid));
    return false;
  }
  attrs[3] = 0x80;
  if (kernel_set_ucred_attrs(pid, attrs) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_attrs failed pid=%d",
              static_cast<int>(pid));
    return false;
  }

  if (escape_sandbox) {
    const intptr_t root = kernel_get_root_vnode();
    if (root == 0) {
      LOG_ERROR("[JB] kernel_get_root_vnode()=0 - cannot escape sandbox");
      return false;
    }
    if (kernel_set_proc_rootdir(pid, root) != 0) {
      LOG_ERROR("[JB] kernel_set_proc_rootdir failed pid=%d",
                static_cast<int>(pid));
      return false;
    }
    if (kernel_set_proc_jaildir(pid, root) != 0) {
      LOG_ERROR("[JB] kernel_set_proc_jaildir failed pid=%d",
                static_cast<int>(pid));
      return false;
    }
  }

  return kernel_get_ucred_uid(pid) == 0;
}

bool path_is_directory(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool path_is_within(const std::string &path, const std::string &root) {
  return path == root ||
         (path.size() > root.size() &&
          path.compare(0, root.size(), root) == 0 &&
          path[root.size()] == '/');
}

enum class WatchKind {
  MountRoot,
  SandboxRoot,
  SlotDirectory,
  DownloadDirectory,
  RequestFile,
};

const char *watch_kind_name(WatchKind kind) {
  switch (kind) {
  case WatchKind::MountRoot:
    return "mount-root";
  case WatchKind::SandboxRoot:
    return "sandbox-root";
  case WatchKind::SlotDirectory:
    return "slot";
  case WatchKind::DownloadDirectory:
    return "download0";
  case WatchKind::RequestFile:
    return "request";
  }
  return "unknown";
}

struct VnodeWatch {
  int fd = -1;
  WatchKind kind = WatchKind::MountRoot;
  std::string path;
  std::string tid;
  std::string slot_path;
};

struct TrackedApp {
  uint32_t app_id = 0;
  std::set<pid_t> pids;
};

struct TrackedBigApp {
  uint32_t app_id = 0;
  std::string title_id;
};

/** Identifies foreground Big Apps and only publishes lifecycle events. */
class SceSysCoreAppLifecycleCollector {
public:
  bool on_exec(pid_t pid, const ExecIdentity &id) {
    if (id.app_info_rc != 0 || id.tid.empty() || big_apps_.count(pid) != 0)
      return false;

    std::string current_title_id;
    int current_app_id = -1;
    if (!Get_Running_App_TID(current_title_id, current_app_id) ||
        current_app_id < 0 ||
        id.info.app_id != static_cast<uint32_t>(current_app_id) ||
        id.tid != current_title_id || get_game_pid() != pid)
      return false;

    if (!onion::daemon::app_lifecycle::publish_big_app_started(
            pid, id.info.app_id, id.tid))
      return false;

    big_apps_[pid] = {id.info.app_id, id.tid};
    LOG_INFO("[lifecycle] Big App started pid=%d appid=%u tid=%s",
             static_cast<int>(pid), id.info.app_id, id.tid.c_str());
    return true;
  }

  bool on_exit(pid_t pid) {
    const auto found = big_apps_.find(pid);
    if (found == big_apps_.end())
      return false;

    const TrackedBigApp app = found->second;
    big_apps_.erase(found);
    if (!onion::daemon::app_lifecycle::publish_big_app_exited(
            pid, app.app_id, app.title_id))
      return false;

    LOG_INFO("[lifecycle] Big App exited pid=%d appid=%u tid=%s",
             static_cast<int>(pid), app.app_id, app.title_id.c_str());
    return true;
  }

private:
  std::map<pid_t, TrackedBigApp> big_apps_;
};

/** The sole owner of SceSysCore NOTE_EXEC/NOTE_EXIT subscription. */
class SceSysCoreAppLifecycleListener {
public:
  explicit SceSysCoreAppLifecycleListener(int control_read_fd)
      : control_read_fd_(control_read_fd) {}

  ~SceSysCoreAppLifecycleListener() {
    if (kq_ >= 0)
      close(kq_);
  }

  void run() {
    if (!start())
      return;

    struct kevent events[16];
    while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
      const int count = kevent(kq_, nullptr, 0, events,
                               sizeof(events) / sizeof(events[0]), nullptr);
      if (count < 0) {
        if (errno == EINTR)
          continue;
        LOG_ERROR("[lifecycle] SceSysCore kevent wait failed: %s",
                  strerror(errno));
        return;
      }

      for (int index = 0; index < count; ++index) {
        const struct kevent &event = events[index];
        if (event.filter == EVFILT_READ &&
            event.ident == static_cast<uintptr_t>(control_read_fd_)) {
          drain_control_pipe();
          return;
        }
        if (event.flags & EV_ERROR) {
          LOG_ERROR("[lifecycle] SceSysCore event error filter=%d ident=%lu "
                    "error=%lld",
                    static_cast<int>(event.filter),
                    static_cast<unsigned long>(event.ident),
                    static_cast<long long>(event.data));
          continue;
        }
        if (event.filter == EVFILT_PROC && !handle_process_event(event))
          return;
      }
    }
  }

private:
  bool start() {
    kq_ = kqueue();
    if (kq_ < 0) {
      LOG_ERROR("[lifecycle] kqueue create failed: %s", strerror(errno));
      return false;
    }

    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(control_read_fd_), EVFILT_READ,
           EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) != 0) {
      LOG_ERROR("[lifecycle] control-pipe watch failed: %s", strerror(errno));
      return false;
    }

    syscore_pid_ = onion_find_pid("SceSysCore.elf");
    if (syscore_pid_ <= 0) {
      LOG_ERROR("[lifecycle] cannot find SceSysCore.elf; listener inactive");
      return false;
    }

    EV_SET(&event, static_cast<uintptr_t>(syscore_pid_), EVFILT_PROC,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_FORK | NOTE_EXEC | NOTE_TRACK, 0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) != 0) {
      LOG_ERROR("[lifecycle] cannot watch SceSysCore pid=%d: %s",
                static_cast<int>(syscore_pid_), strerror(errno));
      return false;
    }

    LOG_INFO("[lifecycle] SceSysCore listener active pid=%d",
             static_cast<int>(syscore_pid_));
    return true;
  }

  void drain_control_pipe() const {
    char buffer[64];
    while (read(control_read_fd_, buffer, sizeof(buffer)) > 0) {
    }
  }

  bool handle_process_event(const struct kevent &event) {
    const pid_t pid = static_cast<pid_t>(event.ident);
    if (event.fflags & NOTE_EXEC) {
      const ExecIdentity id = read_exec_identity(pid);
      if (identity_is_shellui(id)) {
        LOG_DEBUG("[lifecycle] SysCore EXEC SceShellUI pid=%d",
                  static_cast<int>(pid));
        toolbox_on_new_shellui(pid);
      }
      (void)collector_.on_exec(pid, id);
    }
    if (event.fflags & NOTE_EXIT) {
      if (onion::lifecycle::ProcessExitPolicy::should_stop_listener(
              syscore_pid_, pid)) {
        LOG_ERROR("[lifecycle] SceSysCore exited; listener stopped");
        return false;
      }
      (void)collector_.on_exit(pid);
    }
    return true;
  }

  int control_read_fd_ = -1;
  int kq_ = -1;
  pid_t syscore_pid_ = -1;
  SceSysCoreAppLifecycleCollector collector_;
};

/** Owns AppJailbreak's PID and sandbox vnode monitoring state. */
class AppJailbreakRuntime {
public:
  explicit AppJailbreakRuntime(int control_read_fd)
      : control_read_fd_(control_read_fd) {}

  ~AppJailbreakRuntime() {
    close_vnode_watches();
    if (kq_ >= 0) {
      close(kq_);
    }
  }

  void run() {
    struct kevent events[16];
    while (!g_stack_shutting_down.load(std::memory_order_acquire) &&
           !app_jailbreak_start_cancelled.load(std::memory_order_acquire)) {
      const int count = kevent(kq_, nullptr, 0, events,
                               sizeof(events) / sizeof(events[0]), nullptr);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        LOG_ERROR("[JB] kevent wait failed: %s", strerror(errno));
        break;
      }

      for (int i = 0; i < count; ++i) {
        const struct kevent &event = events[i];
        if (event.filter == EVFILT_READ &&
            event.ident == static_cast<uintptr_t>(control_read_fd_)) {
          const ControlMessages messages = drain_control_pipe();
          // EAGAIN on the writer means a prior wake is pending, which may be
          // a rebuild byte rather than a lifecycle byte. Check the queue too.
          if (messages.lifecycle_requested ||
              has_pending_app_jailbreak_commands()) {
            process_app_jailbreak_commands();
          }
          if (g_stack_shutting_down.load(std::memory_order_acquire) ||
              app_jailbreak_start_cancelled.load(std::memory_order_acquire)) {
            return;
          }
          if (messages.rebuild_requested) {
            /* Replacing kqueue invalidates every remaining event in this batch. */
            if (!rebuild()) {
              return;
            }
            break;
          }
          continue;
        }

        if (event.flags & EV_ERROR) {
          LOG_ERROR("[JB] kqueue event error filter=%d ident=%lu error=%lld",
                    static_cast<int>(event.filter),
                    static_cast<unsigned long>(event.ident),
                    static_cast<long long>(event.data));
          continue;
        }

        if (event.filter == EVFILT_PROC) {
          handle_tracked_process_event(event);
        } else if (event.filter == EVFILT_VNODE) {
          handle_vnode_event(event);
        }
      }
    }
  }

  bool initialize() { return rebuild(); }

private:
  bool install_control_watch(int kq) const {
    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(control_read_fd_), EVFILT_READ,
           EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
    return kevent(kq, &event, 1, nullptr, 0, nullptr) == 0;
  }

  bool rebuild() {
    const int new_kq = kqueue();
    if (new_kq < 0) {
      LOG_ERROR("[JB] kqueue create failed: %s", strerror(errno));
      return false;
    }
    if (!install_control_watch(new_kq)) {
      LOG_ERROR("[JB] control-pipe watch failed: %s", strerror(errno));
      close(new_kq);
      return false;
    }

    close_vnode_watches();
    if (kq_ >= 0) {
      close(kq_);
    }
    kq_ = new_kq;
    pid_to_tid_.clear();
    active_apps_.clear();
    settings_ = g_settings.snapshot();

    const bool jb_enabled =
        jb_listener_enabled.load(std::memory_order_acquire) &&
        settings_.app_jailbreak_enabled;

    LOG_INFO("[JB] runtime active: PID/vnode monitoring, jailbreak=%s",
             jb_enabled ? "on" : "off");
    return true;
  }

  ControlMessages drain_control_pipe() const {
    ControlMessages messages;
    char buffer[64];
    for (;;) {
      const ssize_t count = read(control_read_fd_, buffer, sizeof(buffer));
      if (count <= 0) {
        break;
      }
      for (ssize_t index = 0; index < count; ++index) {
        switch (static_cast<ControlMessage>(buffer[index])) {
        case ControlMessage::Rebuild:
          messages.rebuild_requested = true;
          break;
        case ControlMessage::Lifecycle:
          messages.lifecycle_requested = true;
          break;
        }
      }
    }
    return messages;
  }

  void handle_tracked_process_event(const struct kevent &event) {
    const pid_t pid = static_cast<pid_t>(event.ident);
    if (event.fflags & NOTE_EXIT) {
      remove_app_process(pid);
    }
  }

  void process_app_jailbreak_commands() {
    for (;;) {
      std::shared_ptr<AppJailbreakCommand> command;
      {
        std::lock_guard<std::mutex> lock(app_jailbreak_commands_mutex);
        if (app_jailbreak_commands.empty())
          return;
        command = std::move(app_jailbreak_commands.front());
        app_jailbreak_commands.pop_front();
      }

      bool success = false;
      switch (command->type) {
      case AppJailbreakCommandType::BigAppStarted:
        success = inspect_big_app_process(command->pid, command->app_id,
                                          command->title_id);
        break;
      case AppJailbreakCommandType::BigAppExited:
        remove_app_process(command->pid);
        success = true;
        break;
      }

      {
        std::lock_guard<std::mutex> lock(command->completion_mutex);
        command->success = success;
        command->completed = true;
      }
      command->completion_ready.notify_one();
    }
  }

  bool inspect_big_app_process(pid_t pid, uint32_t app_id,
                               const std::string &title_id) {
    if (!jb_listener_enabled.load(std::memory_order_acquire) ||
        !settings_.app_jailbreak_enabled) {
      LOG_DEBUG("[JB] Big App start ignored while AppJailbreak is disabled "
                "pid=%d tid=%s",
                static_cast<int>(pid), title_id.c_str());
      return true;
    }

    ExecIdentity id;
    id.app_info_rc = 0;
    id.info.app_id = app_id;
    id.tid = title_id;
    return inspect_app_process(pid, "big-app", id);
  }

  bool inspect_app_process(pid_t pid, const char *source,
                           const ExecIdentity &id) {
    if (pid <= 1 || pid_to_tid_.find(pid) != pid_to_tid_.end()) {
      return true;
    }

    if (id.app_info_rc != 0) {
      LOG_TRACE("[JB][diag] %s pid=%d name_rc=%d name='%s' "
                "GetAppInfo rc=%d errno=%d",
                source, static_cast<int>(pid), id.name_rc, id.process_name,
                id.app_info_rc, errno);
      return false;
    }
    const std::string &tid = id.tid;
    const bool whitelisted =
        !tid.empty() &&
        is_whitelisted_app(tid, settings_.app_jailbreak_allowlist);
    LOG_TRACE("[JB][diag] %s pid=%d name_rc=%d name='%s' appid=%u "
              "tid='%s' whitelist=%s",
              source, static_cast<int>(pid), id.name_rc, id.process_name,
              id.info.app_id, tid.c_str(),
              onion::app_jailbreak::whitelist_reason(
                  tid, settings_.app_jailbreak_allowlist));
    if (!whitelisted) {
      return true;
    }

    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(pid), EVFILT_PROC,
           EV_ADD | EV_ENABLE | EV_CLEAR, NOTE_EXIT, 0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) != 0) {
      LOG_ERROR("[JB] cannot watch exit pid=%d tid=%s: %s",
                static_cast<int>(pid), tid.c_str(), strerror(errno));
      return false;
    }

    TrackedApp &app = active_apps_[tid];
    const bool first_process = app.pids.empty();
    app.app_id = id.info.app_id;
    app.pids.insert(pid);
    pid_to_tid_[pid] = tid;

    LOG_INFO("[JB] allowed App %s pid=%d tid=%s appid=%u whitelist=%s",
             source, static_cast<int>(pid), tid.c_str(), id.info.app_id,
             onion::app_jailbreak::whitelist_reason(
                 tid, settings_.app_jailbreak_allowlist));

    if (first_process) {
      ensure_sandbox_root_watch();
      discover_slots(tid);
    }
    return true;
  }

  void remove_app_process(pid_t pid) {
    const auto pid_it = pid_to_tid_.find(pid);
    if (pid_it == pid_to_tid_.end()) {
      return;
    }
    const std::string tid = pid_it->second;
    pid_to_tid_.erase(pid_it);

    const auto app_it = active_apps_.find(tid);
    if (app_it == active_apps_.end()) {
      return;
    }
    app_it->second.pids.erase(pid);
    if (!app_it->second.pids.empty()) {
      return;
    }

    active_apps_.erase(app_it);
    remove_tid_watches(tid);
    LOG_INFO("[JB] allowed App exited tid=%s; sandbox watches removed",
             tid.c_str());
    if (active_apps_.empty()) {
      close_vnode_watches();
    }
  }

  bool add_vnode_watch(WatchKind kind, const std::string &path,
                       const std::string &tid = {},
                       const std::string &slot_path = {}) {
    if (path_to_fd_.find(path) != path_to_fd_.end()) {
      return true;
    }

    int flags = O_RDONLY | O_NONBLOCK;
    const bool is_directory = kind != WatchKind::RequestFile;
    if (is_directory) {
      flags |= O_DIRECTORY;
    }
    const int fd = open(path.c_str(), flags);
    if (fd < 0) {
      LOG_ERROR("[JB][diag] open watcher failed kind=%s path=%s errno=%d (%s)",
                watch_kind_name(kind), path.c_str(), errno, strerror(errno));
      return false;
    }

    const uint32_t vnode_flags =
        is_directory
            ? NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE |
                  NOTE_RENAME | NOTE_REVOKE
            : NOTE_WRITE | NOTE_EXTEND | NOTE_CLOSE_WRITE | NOTE_DELETE |
                  NOTE_RENAME | NOTE_REVOKE;
    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(fd), EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR, vnode_flags, 0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) != 0) {
      LOG_ERROR("[JB] cannot watch path=%s: %s", path.c_str(),
                strerror(errno));
      close(fd);
      return false;
    }

    vnode_watches_.emplace(
        fd, VnodeWatch{fd, kind, path, tid, slot_path});
    path_to_fd_.emplace(path, fd);
    LOG_DEBUG("[JB][diag] vnode watch added kind=%s fd=%d path=%s tid=%s",
              watch_kind_name(kind), fd, path.c_str(),
              tid.empty() ? "-" : tid.c_str());
    return true;
  }

  void remove_vnode_watch(int fd) {
    const auto it = vnode_watches_.find(fd);
    if (it == vnode_watches_.end()) {
      return;
    }
    path_to_fd_.erase(it->second.path);
    close(it->second.fd);
    vnode_watches_.erase(it);
  }

  void remove_watch_tree(const std::string &root) {
    std::vector<int> remove;
    for (const auto &entry : vnode_watches_) {
      if (path_is_within(entry.second.path, root)) {
        remove.push_back(entry.first);
      }
    }
    for (const int fd : remove) {
      remove_vnode_watch(fd);
    }
  }

  void remove_tid_watches(const std::string &tid) {
    std::vector<int> remove;
    for (const auto &entry : vnode_watches_) {
      if (entry.second.tid == tid) {
        remove.push_back(entry.first);
      }
    }
    for (const int fd : remove) {
      remove_vnode_watch(fd);
    }
  }

  void close_vnode_watches() {
    for (const auto &entry : vnode_watches_) {
      close(entry.second.fd);
    }
    vnode_watches_.clear();
    path_to_fd_.clear();
  }

  void ensure_sandbox_root_watch() {
    if (active_apps_.empty()) {
      return;
    }
    if (path_is_directory(kSandboxRoot)) {
      const auto mount_it = path_to_fd_.find(kMountRoot);
      if (mount_it != path_to_fd_.end()) {
        remove_vnode_watch(mount_it->second);
      }
      (void)add_vnode_watch(WatchKind::SandboxRoot, kSandboxRoot);
      return;
    }

    LOG_DEBUG("[JB][diag] sandbox root absent; watching %s", kMountRoot);
    remove_watch_tree(kSandboxRoot);
    if (!add_vnode_watch(WatchKind::MountRoot, kMountRoot)) {
      LOG_ERROR("[JB] cannot watch %s while waiting for sandbox root: %s",
                kMountRoot, strerror(errno));
    }
  }

  void discover_all_slots() {
    ensure_sandbox_root_watch();
    if (path_to_fd_.find(kSandboxRoot) == path_to_fd_.end()) {
      return;
    }
    for (const auto &app : active_apps_) {
      discover_slots(app.first);
    }
  }

  void discover_slots(const std::string &tid) {
    if (active_apps_.find(tid) == active_apps_.end() ||
        !path_is_directory(kSandboxRoot)) {
      LOG_DEBUG("[JB][diag] slot discovery deferred tid=%s sandbox_root=%d",
                tid.c_str(), path_is_directory(kSandboxRoot) ? 1 : 0);
      return;
    }

    int found = 0;
    for (int slot = 0; slot <= 50; ++slot) {
      char suffix[5];
      snprintf(suffix, sizeof(suffix), "_%03d", slot);
      const std::string slot_path =
          std::string(kSandboxRoot) + "/" + tid + suffix;
      if (!path_is_directory(slot_path)) {
        continue;
      }
      ++found;
      LOG_TRACE("[JB][diag] sandbox slot found tid=%s slot=%03d path=%s",
                tid.c_str(), slot, slot_path.c_str());
      (void)add_vnode_watch(WatchKind::SlotDirectory, slot_path, tid,
                            slot_path);
      ensure_download_watch(tid, slot_path);
    }
    LOG_DEBUG("[JB][diag] slot discovery complete tid=%s checked=51 found=%d",
              tid.c_str(), found);
  }

  void ensure_download_watch(const std::string &tid,
                             const std::string &slot_path) {
    if (active_apps_.find(tid) == active_apps_.end()) {
      return;
    }
    const std::string download_path = slot_path + "/download0";
    if (!path_is_directory(download_path)) {
      remove_watch_tree(download_path);
      return;
    }
    (void)add_vnode_watch(WatchKind::DownloadDirectory, download_path, tid,
                          slot_path);
    ensure_request_watches(tid, slot_path);
  }

  void ensure_request_watches(const std::string &tid,
                              const std::string &slot_path) {
    const std::string download_path = slot_path + "/download0";
    for (const char *name : kJailbreakReqNames) {
      const std::string request_path = download_path + "/" + name;
      if (path_to_fd_.find(request_path) != path_to_fd_.end()) {
        continue;
      }
      struct stat st {};
      if (stat(request_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        continue;
      }
      if (!add_vnode_watch(WatchKind::RequestFile, request_path, tid,
                           slot_path)) {
        continue;
      }
      const int request_fd = path_to_fd_[request_path];
      LOG_DEBUG("[JB] request file discovered: %s", request_path.c_str());
      if (process_request(request_fd, 0)) {
        remove_vnode_watch(request_fd);
      }
    }
  }

  void handle_vnode_event(const struct kevent &event) {
    const int fd = static_cast<int>(event.ident);
    const auto it = vnode_watches_.find(fd);
    if (it == vnode_watches_.end()) {
      return;
    }
    const VnodeWatch watch = it->second;
    const bool invalidated =
        (event.fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) != 0;
    LOG_TRACE("[JB][diag] vnode event kind=%s fd=%d fflags=0x%x path=%s",
              watch_kind_name(watch.kind), fd, event.fflags,
              watch.path.c_str());

    switch (watch.kind) {
    case WatchKind::MountRoot:
      ensure_sandbox_root_watch();
      if (path_is_directory(kSandboxRoot)) {
        discover_all_slots();
      }
      break;
    case WatchKind::SandboxRoot:
      if (invalidated) {
        remove_watch_tree(kSandboxRoot);
        ensure_sandbox_root_watch();
      } else {
        discover_all_slots();
      }
      break;
    case WatchKind::SlotDirectory:
      if (invalidated) {
        remove_watch_tree(watch.path);
        discover_slots(watch.tid);
      } else {
        ensure_download_watch(watch.tid, watch.slot_path);
      }
      break;
    case WatchKind::DownloadDirectory:
      if (invalidated) {
        remove_watch_tree(watch.path);
        ensure_download_watch(watch.tid, watch.slot_path);
      } else {
        ensure_request_watches(watch.tid, watch.slot_path);
      }
      break;
    case WatchKind::RequestFile:
      if (invalidated) {
        remove_vnode_watch(fd);
        ensure_request_watches(watch.tid, watch.slot_path);
      } else if (event.fflags &
                 (NOTE_WRITE | NOTE_EXTEND | NOTE_CLOSE_WRITE)) {
        if (process_request(fd, event.fflags)) {
          remove_vnode_watch(fd);
        }
      }
      break;
    }
  }

  bool read_request(int fd, std::string *body, bool *too_large) const {
    *too_large = false;
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
      return false;
    }
    if (static_cast<uint64_t>(st.st_size) > kMaxRequestBytes) {
      *too_large = true;
      return false;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
      return false;
    }

    body->assign(static_cast<size_t>(st.st_size), '\0');
    size_t offset = 0;
    while (offset < body->size()) {
      const ssize_t count =
          read(fd, body->data() + offset, body->size() - offset);
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0) {
        return false;
      }
      offset += static_cast<size_t>(count);
    }
    return true;
  }

  pid_t parse_request_pid(const cJSON *root) const {
    const cJSON *value = onion_cjson::item(root, "PID");
    if (cJSON_IsNumber(value)) {
      if (value->valuedouble != static_cast<double>(value->valueint) ||
          value->valueint <= 1) {
        return -1;
      }
      return static_cast<pid_t>(value->valueint);
    }
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
      return -1;
    }

    errno = 0;
    char *end = nullptr;
    const long parsed = strtol(value->valuestring, &end, 10);
    if (errno != 0 || end == value->valuestring || *end != '\0' ||
        parsed <= 1 || parsed > INT_MAX) {
      return -1;
    }
    return static_cast<pid_t>(parsed);
  }

  bool clear_request(const std::string &path) const {
    if (unlink(path.c_str()) != 0 && errno != ENOENT) {
      LOG_ERROR("[JB] unlink request failed path=%s: %s", path.c_str(),
                strerror(errno));
      return false;
    } else {
      LOG_DEBUG("[JB] cleared request file %s", path.c_str());
    }
    return true;
  }

  bool process_request(int fd, uint32_t event_flags) {
    const auto watch_it = vnode_watches_.find(fd);
    if (watch_it == vnode_watches_.end()) {
      return false;
    }
    const std::string path = watch_it->second.path;
    const std::string tid = watch_it->second.tid;

    std::string body;
    bool too_large = false;
    if (!read_request(fd, &body, &too_large)) {
      if (too_large) {
        LOG_ERROR("[JB] request exceeds %lu bytes: %s",
                  static_cast<unsigned long>(kMaxRequestBytes), path.c_str());
        return clear_request(path);
      }
      return false;
    }

    onion_cjson::Root json(body);
    if (!json) {
      if (event_flags & NOTE_CLOSE_WRITE) {
        LOG_ERROR("[JB] incomplete/invalid request JSON retained for retry: %s",
                  path.c_str());
      }
      return false;
    }

    const pid_t target_pid = parse_request_pid(json.get());
    if (target_pid <= 1) {
      LOG_ERROR("[JB] invalid or missing PID in request: %s", path.c_str());
      return clear_request(path);
    }

    const onion::Settings current = g_settings.snapshot();
    if (!jb_listener_enabled.load(std::memory_order_acquire) ||
        !current.app_jailbreak_enabled ||
        active_apps_.find(tid) == active_apps_.end() ||
        !is_whitelisted_app(tid, current.app_jailbreak_allowlist)) {
      LOG_DEBUG("[JB] request deferred because App jailbreak is inactive: %s",
                path.c_str());
      return false;
    }

    if (!isProcessAlive(target_pid)) {
      LOG_ERROR("[JB] pid=%d is dead; clearing stale request %s",
                static_cast<int>(target_pid), path.c_str());
      return clear_request(path);
    }
    if (KERNEL_ADDRESS_ALLPROC == 0) {
      LOG_ERROR("[JB] KERNEL_ADDRESS_ALLPROC=0; clearing request %s",
                path.c_str());
      return clear_request(path);
    }

    const int uid_before = jb_read_uid(target_pid);
    const uint64_t auth_before = jb_read_authid(target_pid);
    LOG_DEBUG("[JB] request pid=%d tid=%s pre-jb uid=%d authid=0x%llx",
              static_cast<int>(target_pid), tid.c_str(), uid_before,
              static_cast<unsigned long long>(auth_before));

    bool ok = false;
    for (int attempt = 1; attempt <= kProcResolveAttempts && !ok; ++attempt) {
      if (kernel_get_proc(target_pid) == 0) {
        LOG_DEBUG("[JB] kernel_get_proc(%d)=0 attempt=%d/%d",
                  static_cast<int>(target_pid), attempt, kProcResolveAttempts);
        if (!isProcessAlive(target_pid)) {
          break;
        }
        if (attempt < kProcResolveAttempts) {
          usleep(kProcResolveUsleep);
        }
        continue;
      }
      if (!jb_listener_enabled.load(std::memory_order_acquire)) {
        LOG_DEBUG("[JB] listener disabled before privilege apply; deferring %s",
                  path.c_str());
        return false;
      }
      ok = jb_apply_privileges(target_pid, /*escape_sandbox=*/true);
      break;
    }

    const int uid_after = jb_read_uid(target_pid);
    const uint64_t auth_after = jb_read_authid(target_pid);
    LOG_DEBUG("[JB] post-jb pid=%d uid=%d (was %d) authid=0x%llx "
              "(was 0x%llx) ok=%d",
              static_cast<int>(target_pid), uid_after, uid_before,
              static_cast<unsigned long long>(auth_after),
              static_cast<unsigned long long>(auth_before), ok ? 1 : 0);

    if (ok && uid_after == 0) {
      if (current.debug_app_jb_msg) {
        onion_notify(true, "notify.jailbreak.granted",
                     static_cast<int>(target_pid));
      }
      LOG_INFO("[JB] OK: pid=%d tid=%s fully jailbroken",
               static_cast<int>(target_pid), tid.c_str());
    } else {
      LOG_ERROR("[JB] FAIL: privilege apply did not stick for pid=%d uid=%d",
                static_cast<int>(target_pid), uid_after);
    }

    return clear_request(path);
  }

  int control_read_fd_ = -1;
  int kq_ = -1;
  onion::Settings settings_ {};
  std::map<int, VnodeWatch> vnode_watches_;
  std::map<std::string, int> path_to_fd_;
  std::map<pid_t, std::string> pid_to_tid_;
  std::map<std::string, TrackedApp> active_apps_;
};

bool enqueue_app_jailbreak_command(AppJailbreakCommandType type, pid_t pid,
                                   uint32_t app_id, const char *title_id) {
  if (pid <= 1 || title_id == nullptr || title_id[0] == '\0' ||
      g_stack_shutting_down.load(std::memory_order_acquire))
    return false;

  auto command = std::make_shared<AppJailbreakCommand>();
  command->type = type;
  command->pid = pid;
  command->app_id = app_id;
  command->title_id = title_id;
  {
    std::lock_guard<std::mutex> lock(app_jailbreak_commands_mutex);
    app_jailbreak_commands.push_back(command);
  }

  bool queued_wake = false;
  pthread_mutex_lock(&jb_control_lock);
  if (jb_control_write_fd >= 0) {
    const char wake = static_cast<char>(ControlMessage::Lifecycle);
    const ssize_t count = write(jb_control_write_fd, &wake, sizeof(wake));
    queued_wake = count == static_cast<ssize_t>(sizeof(wake)) ||
                  (count < 0 && errno == EAGAIN);
  }
  pthread_mutex_unlock(&jb_control_lock);

  if (!queued_wake) {
    std::lock_guard<std::mutex> lock(app_jailbreak_commands_mutex);
    for (auto it = app_jailbreak_commands.begin();
         it != app_jailbreak_commands.end();
         ++it) {
      if (*it == command) {
        app_jailbreak_commands.erase(it);
        break;
      }
    }
    LOG_WARN("[lifecycle] cannot wake AppJailbreak listener for pid=%d",
             static_cast<int>(pid));
    return false;
  }

  std::unique_lock<std::mutex> lock(command->completion_mutex);
  if (!command->completion_ready.wait_for(lock, kAppJailbreakCommandTimeout,
                                          [&command] {
                                            return command->completed;
                                          })) {
    LOG_WARN("[lifecycle] AppJailbreak command timed out type=%u pid=%d",
             static_cast<unsigned>(type), static_cast<int>(pid));
    return false;
  }
  return command->success;
}

void cancel_pending_app_jailbreak_commands() {
  std::deque<std::shared_ptr<AppJailbreakCommand>> pending;
  {
    std::lock_guard<std::mutex> lock(app_jailbreak_commands_mutex);
    pending.swap(app_jailbreak_commands);
  }
  for (const auto &command : pending) {
    {
      std::lock_guard<std::mutex> lock(command->completion_mutex);
      command->success = false;
      command->completed = true;
    }
    command->completion_ready.notify_one();
  }
}

bool set_nonblocking_close_on_exec(int fd) {
  const int status_flags = fcntl(fd, F_GETFL, 0);
  const int descriptor_flags = fcntl(fd, F_GETFD, 0);
  return status_flags >= 0 && descriptor_flags >= 0 &&
         fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) == 0 &&
         fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) == 0;
}

} // namespace

void app_lifecycle_listener_stop() {
  pthread_mutex_lock(&lifecycle_control_lock);
  if (lifecycle_control_write_fd >= 0) {
    const char wake = static_cast<char>(LifecycleListenerControlMessage::Stop);
    const ssize_t ignored =
        write(lifecycle_control_write_fd, &wake, sizeof(wake));
    (void)ignored;
  }
  pthread_mutex_unlock(&lifecycle_control_lock);
}

void *app_lifecycle_listener_thread(void *args) noexcept {
  (void)args;
  int control_pipe[2] = {-1, -1};
  if (pipe(control_pipe) != 0 ||
      !set_nonblocking_close_on_exec(control_pipe[0]) ||
      !set_nonblocking_close_on_exec(control_pipe[1])) {
    LOG_ERROR("[lifecycle] control pipe setup failed: %s", strerror(errno));
    if (control_pipe[0] >= 0)
      close(control_pipe[0]);
    if (control_pipe[1] >= 0)
      close(control_pipe[1]);
    return nullptr;
  }

  pthread_mutex_lock(&lifecycle_control_lock);
  lifecycle_control_write_fd = control_pipe[1];
  pthread_mutex_unlock(&lifecycle_control_lock);

  {
    SceSysCoreAppLifecycleListener listener(control_pipe[0]);
    listener.run();
  }

  pthread_mutex_lock(&lifecycle_control_lock);
  if (lifecycle_control_write_fd == control_pipe[1])
    lifecycle_control_write_fd = -1;
  close(control_pipe[1]);
  pthread_mutex_unlock(&lifecycle_control_lock);
  close(control_pipe[0]);
  LOG_INFO("[lifecycle] SceSysCore listener stopped");
  return nullptr;
}

void app_jailbreak_set_enabled(bool enabled) {
  const bool previous =
      jb_listener_enabled.exchange(enabled, std::memory_order_acq_rel);

  pthread_mutex_lock(&jb_control_lock);
  if (jb_control_write_fd >= 0) {
    const char wake = static_cast<char>(ControlMessage::Rebuild);
    const ssize_t ignored = write(jb_control_write_fd, &wake, sizeof(wake));
    (void)ignored;
  }
  pthread_mutex_unlock(&jb_control_lock);

  if (previous != enabled) {
    LOG_INFO("[JB] app jailbreak listener %s",
             enabled ? "enabled" : "disabled");
  }
}

bool app_jailbreak_on_big_app_started(pid_t pid, uint32_t app_id,
                                      const char *title_id) {
  return enqueue_app_jailbreak_command(
      AppJailbreakCommandType::BigAppStarted, pid, app_id, title_id);
}

bool app_jailbreak_on_big_app_exited(pid_t pid, uint32_t app_id,
                                     const char *title_id) {
  return enqueue_app_jailbreak_command(
      AppJailbreakCommandType::BigAppExited, pid, app_id, title_id);
}

void *fifo_and_dumper_thread(void *args) noexcept {
  (void)args;
  int control_pipe[2] = {-1, -1};
  if (pipe(control_pipe) != 0 ||
      !set_nonblocking_close_on_exec(control_pipe[0]) ||
      !set_nonblocking_close_on_exec(control_pipe[1])) {
    LOG_ERROR("[JB] control pipe setup failed: %s", strerror(errno));
    if (control_pipe[0] >= 0) {
      close(control_pipe[0]);
    }
    if (control_pipe[1] >= 0) {
      close(control_pipe[1]);
    }
    app_jailbreak_startup.signal(
        onion::lifecycle::RuntimeStartupStatus::Failed);
    return nullptr;
  }

  pthread_mutex_lock(&jb_control_lock);
  jb_control_write_fd = control_pipe[1];
  pthread_mutex_unlock(&jb_control_lock);

  if (app_jailbreak_start_cancelled.load(std::memory_order_acquire)) {
    app_jailbreak_startup.signal(
        onion::lifecycle::RuntimeStartupStatus::Failed);
  }

  LOG_INFO("[JB] runtime started (PID + sandbox vnode monitoring)");
  LOG_DEBUG("[JB] kernel symbols: ALLPROC=0x%lx ROOTVNODE=0x%lx",
            static_cast<unsigned long>(KERNEL_ADDRESS_ALLPROC),
            static_cast<unsigned long>(KERNEL_ADDRESS_ROOTVNODE));

  {
    AppJailbreakRuntime runtime(control_pipe[0]);
    const bool initialized = runtime.initialize();
    if (initialized &&
        !app_jailbreak_start_cancelled.load(std::memory_order_acquire)) {
      app_jailbreak_startup.signal(
          onion::lifecycle::RuntimeStartupStatus::Ready);
      runtime.run();
    } else {
      app_jailbreak_startup.signal(
          onion::lifecycle::RuntimeStartupStatus::Failed);
    }
  }
  cancel_pending_app_jailbreak_commands();

  pthread_mutex_lock(&jb_control_lock);
  if (jb_control_write_fd == control_pipe[1]) {
    jb_control_write_fd = -1;
  }
  close(control_pipe[1]);
  pthread_mutex_unlock(&jb_control_lock);
  close(control_pipe[0]);

  LOG_INFO("[JB] runtime stopped");
  return nullptr;
}

bool app_jailbreak_runtime_start(pthread_t *thread) {
  if (thread == nullptr)
    return false;
  app_jailbreak_start_cancelled.store(false, std::memory_order_release);
  app_jailbreak_startup.reset();
  if (pthread_create(thread, nullptr, fifo_and_dumper_thread, nullptr) != 0) {
    LOG_ERROR("[JB] runtime thread creation failed");
    return false;
  }
  const auto startup_status = app_jailbreak_startup.wait_for(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          kAppJailbreakStartupTimeout));
  if (startup_status != onion::lifecycle::RuntimeStartupStatus::Ready) {
    if (startup_status == onion::lifecycle::RuntimeStartupStatus::Timeout)
      LOG_ERROR("[JB] runtime startup timed out");
    app_jailbreak_start_cancelled.store(true, std::memory_order_release);
    pthread_mutex_lock(&jb_control_lock);
    if (jb_control_write_fd >= 0) {
      const char wake = static_cast<char>(ControlMessage::Rebuild);
      const ssize_t ignored = write(jb_control_write_fd, &wake, sizeof(wake));
      (void)ignored;
    }
    pthread_mutex_unlock(&jb_control_lock);
    (void)pthread_join(*thread, nullptr);
    return false;
  }
  return true;
}
