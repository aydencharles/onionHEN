#include "sprx_plugin_manager_runtime.hpp"

#include "daemon_ops.hpp"

#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/pt.h>
#include <onion/sprx_plugin_manager.hpp>

#include <ps5/kernel.h>

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace onion::daemon::sprx_plugins {
namespace {

class CurrentGameTarget final : public onion::sprx::ISprxTargetProvider {
public:
  bool current(onion::sprx::SprxTarget *out) noexcept override {
    if (!out)
      return false;
    std::string title_id;
    int app_id = -1;
    const int pid = get_game_pid();
    if (pid <= 1 || !Get_Running_App_TID(title_id, app_id))
      return false;
    out->pid = static_cast<pid_t>(pid);
    out->title_id = std::move(title_id);
    return true;
  }
};

constexpr uint64_t kJbAuthId = 0x4801000000000013ull;
constexpr char kLoadStartModuleNid[] = "wzvqT4UqKX8";
constexpr char kPthreadCreateNid[] = "OxhIB8LB-PQ";
constexpr uint32_t kBigAppReadyTimeoutMs = 5000;
constexpr uint32_t kBigAppReadyPollMs = 100;
constexpr uint32_t kBigAppSettleMs = 3000;
constexpr const char *kSandboxRoot = "/mnt/sandbox";
constexpr const char *kStageSubdir = ".onion_sprx";

struct TargetCredSnapshot {
  pid_t pid = -1;
  bool active = false;
  bool modified = false;
  uid_t uid = 0;
  uid_t ruid = 0;
  uid_t svuid = 0;
  gid_t rgid = 0;
  gid_t svgid = 0;
  uint64_t authid = 0;
  uint8_t caps[16] = {};
  uint8_t attrs[32] = {};
};

bool is_directory(const std::string &path) noexcept {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_regular_file(const std::string &path) noexcept {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool find_sandbox_slot(std::string_view title_id, std::string *slot_out) {
  if (!slot_out || title_id.empty())
    return false;

  std::string best_slot;
  time_t best_mtime = 0;
  for (int slot = 0; slot <= 50; ++slot) {
    char buf[128] = {};
    std::snprintf(buf, sizeof(buf), "%s/%.*s_%03d", kSandboxRoot,
                  static_cast<int>(title_id.size()), title_id.data(), slot);
    struct stat st {};
    if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) {
      if (best_slot.empty() || st.st_mtime >= best_mtime) {
        best_slot = buf;
        best_mtime = st.st_mtime;
      }
    }
  }
  if (best_slot.empty())
    return false;
  *slot_out = std::move(best_slot);
  return true;
}

bool find_sandbox_stage_dir(const std::string &slot_path,
                            std::string *host_dir_out,
                            std::string *guest_dir_out) {
  if (!host_dir_out || !guest_dir_out || slot_path.empty())
    return false;

  /* Prefer exec-capable mounts inside the sandbox; avoid download0 (noexec). */
  constexpr const char *kSubdirs[] = {"data", "system_tmp", "temp0"};
  for (const char *sub : kSubdirs) {
    const std::string candidate = slot_path + "/" + sub;
    if (is_directory(candidate)) {
      *host_dir_out = candidate + "/" + kStageSubdir;
      *guest_dir_out = std::string("/") + sub + "/" + kStageSubdir;
      return true;
    }
  }

  *host_dir_out = slot_path + "/" + kStageSubdir;
  *guest_dir_out = std::string("/") + kStageSubdir;
  return true;
}

bool copy_file_for_sandbox(const std::string &src_path,
                           const std::string &dst_dir,
                           const std::string &dst_path) noexcept {
  if (mkdir(dst_dir.c_str(), 0755) != 0 && errno != EEXIST)
    return false;
  (void)chmod(dst_dir.c_str(), 0755);

  const int src_fd = open(src_path.c_str(), O_RDONLY);
  if (src_fd < 0)
    return false;

  const std::string tmp_path = dst_path + ".tmp";
  const int dst_fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (dst_fd < 0) {
    close(src_fd);
    return false;
  }

  uint8_t buf[16384];
  bool ok = true;
  while (true) {
    const ssize_t n = read(src_fd, buf, sizeof(buf));
    if (n == 0)
      break;
    if (n < 0) {
      if (errno == EINTR)
        continue;
      ok = false;
      break;
    }
    ssize_t written = 0;
    while (written < n) {
      const ssize_t w =
          write(dst_fd, buf + written, static_cast<size_t>(n - written));
      if (w < 0) {
        if (errno == EINTR)
          continue;
        ok = false;
        break;
      }
      written += w;
    }
    if (!ok)
      break;
  }

  if (close(dst_fd) != 0)
    ok = false;
  close(src_fd);

  if (!ok) {
    (void)unlink(tmp_path.c_str());
    return false;
  }
  (void)chmod(tmp_path.c_str(), 0755);
  if (rename(tmp_path.c_str(), dst_path.c_str()) != 0) {
    (void)unlink(tmp_path.c_str());
    return false;
  }
  (void)chmod(dst_path.c_str(), 0755);
  return true;
}

bool restore_snapshot(const TargetCredSnapshot &snap) noexcept {
  bool ok = true;
  if (kernel_set_ucred_attrs(snap.pid, snap.attrs) != 0) {
    LOG_ERROR("[sprx-manager] restore attrs failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  if (kernel_set_ucred_caps(snap.pid, snap.caps) != 0) {
    LOG_ERROR("[sprx-manager] restore caps failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  if (kernel_set_ucred_authid(snap.pid, snap.authid) != 0) {
    LOG_ERROR("[sprx-manager] restore authid failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  if (kernel_set_ucred_svgid(snap.pid, snap.svgid) != 0) {
    LOG_ERROR("[sprx-manager] restore svgid failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  if (kernel_set_ucred_rgid(snap.pid, snap.rgid) != 0) {
    LOG_ERROR("[sprx-manager] restore rgid failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  if (kernel_set_ucred_svuid(snap.pid, snap.svuid) != 0) {
    LOG_ERROR("[sprx-manager] restore svuid failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  if (kernel_set_ucred_ruid(snap.pid, snap.ruid) != 0) {
    LOG_ERROR("[sprx-manager] restore ruid failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  if (kernel_set_ucred_uid(snap.pid, snap.uid) != 0) {
    LOG_ERROR("[sprx-manager] restore uid failed pid=%d",
              static_cast<int>(snap.pid));
    ok = false;
  }
  return ok;
}

class PreparedTargetAccess final : public onion::sprx::ITargetAccessPolicy {
public:
  onion::sprx::AccessResult prepare(pid_t pid,
                                     std::string_view title_id) noexcept override {
    return prepare_for_module(pid, title_id, {});
  }

  onion::sprx::AccessResult
  prepare_for_module(pid_t pid, std::string_view title_id,
                     std::string_view host_path) noexcept override {
    static constexpr uint8_t kFullCaps[16] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };

    if (pid <= 1 || !onion_proc_is_alive(pid) || kernel_get_proc(pid) == 0)
      return {onion::sprx::AccessStatus::PrepareFailed, {}};
    if (snapshot_.active) {
      LOG_ERROR("[sprx-manager] active credential snapshot still pending pid=%d",
                static_cast<int>(snapshot_.pid));
      return {onion::sprx::AccessStatus::PrepareFailed, {}};
    }

    const intptr_t root_vnode = kernel_get_root_vnode();
    if (root_vnode == 0) {
      LOG_ERROR("[sprx-manager] kernel_get_root_vnode()=0 pid=%d",
                static_cast<int>(pid));
      return {onion::sprx::AccessStatus::PrepareFailed, {}};
    }

    const uid_t uid = kernel_get_ucred_uid(pid);
    const intptr_t rootdir = kernel_get_proc_rootdir(pid);
    if (rootdir == 0) {
      LOG_ERROR("[sprx-manager] kernel_get_proc_rootdir()=0 pid=%d",
                static_cast<int>(pid));
      return {onion::sprx::AccessStatus::PrepareFailed, {}};
    }

    const bool sandboxed = (rootdir != root_vnode);
    std::string target_path;
    if (sandboxed && !host_path.empty()) {
      /*
       * NEVER swap fd_rdir/fd_jdir on a live sandboxed game: while detached for
       * the loader thread, concurrent game threads opening /app0/... fail with
       * ENOENT and crash the game.
       *
       * If the exact host_path is already mounted inside the game's sandbox
       * (e.g. /mnt/sandbox/<TID>_000/data/... -> /data/...), pass it directly
       * with zero copy and no noexec mount issues.  Otherwise stage into an
       * exec-capable directory inside the sandbox (never download0, which is
       * mounted MNT_NOEXEC and causes EACCES / 0x8002000d).
       */
      std::string slot_path;
      if (!find_sandbox_slot(title_id, &slot_path)) {
        LOG_ERROR("[sprx-manager] sandbox directory not found pid=%d tid=%.*s",
                  static_cast<int>(pid), static_cast<int>(title_id.size()),
                  title_id.data());
        return {onion::sprx::AccessStatus::PrepareFailed, {}};
      }

      const std::string direct_in_sandbox = slot_path + std::string(host_path);
      if (is_regular_file(direct_in_sandbox)) {
        target_path.assign(host_path.data(), host_path.size());
        LOG_DEBUG("[sprx-manager] module directly visible in sandbox pid=%d path=%s",
                  static_cast<int>(pid), target_path.c_str());
      } else {
        std::string host_stage_dir;
        std::string guest_stage_dir;
        if (!find_sandbox_stage_dir(slot_path, &host_stage_dir, &guest_stage_dir)) {
          LOG_ERROR("[sprx-manager] no stage dir in sandbox pid=%d slot=%s",
                    static_cast<int>(pid), slot_path.c_str());
          return {onion::sprx::AccessStatus::PrepareFailed, {}};
        }
        const size_t slash = host_path.find_last_of('/');
        const std::string_view base =
            slash == std::string_view::npos ? host_path
                                            : host_path.substr(slash + 1);
        const std::string host_stage_path =
            host_stage_dir + "/" + std::string(base);
        if (!copy_file_for_sandbox(std::string(host_path), host_stage_dir,
                                   host_stage_path)) {
          LOG_ERROR("[sprx-manager] failed to stage %.*s into %s errno=%d",
                    static_cast<int>(host_path.size()), host_path.data(),
                    host_stage_path.c_str(), errno);
          return {onion::sprx::AccessStatus::PrepareFailed, {}};
        }
        target_path = guest_stage_dir + "/" + std::string(base);
        LOG_DEBUG("[sprx-manager] staged module for pid=%d host=%s guest=%s",
                  static_cast<int>(pid), host_stage_path.c_str(),
                  target_path.c_str());
      }
    }

    if (uid == 0) {
      snapshot_ = {};
      snapshot_.pid = pid;
      snapshot_.active = true;
      snapshot_.modified = false;
      return {onion::sprx::AccessStatus::Allowed, std::move(target_path)};
    }

    TargetCredSnapshot snap{};
    snap.pid = pid;
    snap.uid = uid;
    snap.ruid = kernel_get_ucred_ruid(pid);
    snap.svuid = kernel_get_ucred_svuid(pid);
    snap.rgid = kernel_get_ucred_rgid(pid);
    snap.svgid = kernel_get_ucred_svgid(pid);
    snap.authid = kernel_get_ucred_authid(pid);
    if (snap.authid == 0 ||
        kernel_get_ucred_caps(pid, snap.caps) != 0 ||
        kernel_get_ucred_attrs(pid, snap.attrs) != 0) {
      LOG_ERROR("[sprx-manager] failed to snapshot ucred pid=%d tid=%.*s",
                static_cast<int>(pid), static_cast<int>(title_id.size()),
                title_id.data());
      return {onion::sprx::AccessStatus::PrepareFailed, {}};
    }

    uint8_t elevated_attrs[32] = {};
    std::memcpy(elevated_attrs, snap.attrs, sizeof(elevated_attrs));
    elevated_attrs[3] = 0x80;

    bool ok = true;
    ok = ok && kernel_set_ucred_uid(pid, 0) == 0;
    ok = ok && kernel_set_ucred_ruid(pid, 0) == 0;
    ok = ok && kernel_set_ucred_svuid(pid, 0) == 0;
    ok = ok && kernel_set_ucred_rgid(pid, 0) == 0;
    ok = ok && kernel_set_ucred_svgid(pid, 0) == 0;
    ok = ok && kernel_set_ucred_authid(pid, kJbAuthId) == 0;
    ok = ok && kernel_set_ucred_caps(pid, kFullCaps) == 0;
    ok = ok && kernel_set_ucred_attrs(pid, elevated_attrs) == 0;
    if (!ok) {
      LOG_ERROR("[sprx-manager] failed to elevate target pid=%d tid=%.*s; rolling back",
                static_cast<int>(pid), static_cast<int>(title_id.size()),
                title_id.data());
      if (onion_proc_is_alive(pid) && kernel_get_proc(pid) != 0)
        (void)restore_snapshot(snap);
      return {onion::sprx::AccessStatus::PrepareFailed, {}};
    }

    snap.active = true;
    snap.modified = true;
    snapshot_ = snap;
    LOG_DEBUG("[sprx-manager] temporarily elevated ucred pid=%d tid=%.*s saved_uid=%u",
              static_cast<int>(pid), static_cast<int>(title_id.size()),
              title_id.data(), static_cast<unsigned int>(snap.uid));
    return {onion::sprx::AccessStatus::Allowed, std::move(target_path)};
  }

  onion::sprx::AccessResult restore(pid_t pid,
                                     std::string_view title_id) noexcept override {
    if (!snapshot_.active || snapshot_.pid != pid) {
      LOG_WARN("[sprx-manager] restore called without matching snapshot pid=%d",
               static_cast<int>(pid));
      return {onion::sprx::AccessStatus::RestoreFailed, {}};
    }

    const TargetCredSnapshot snap = snapshot_;
    snapshot_ = {};
    if (!snap.modified)
      return {onion::sprx::AccessStatus::Allowed, {}};

    if (!onion_proc_is_alive(pid) || kernel_get_proc(pid) == 0) {
      LOG_DEBUG("[sprx-manager] target exited before credential restore pid=%d tid=%.*s",
                static_cast<int>(pid), static_cast<int>(title_id.size()),
                title_id.data());
      return {onion::sprx::AccessStatus::Allowed, {}};
    }

    if (!restore_snapshot(snap)) {
      LOG_ERROR("[sprx-manager] credential restore failed pid=%d tid=%.*s",
                static_cast<int>(pid), static_cast<int>(title_id.size()),
                title_id.data());
      return {onion::sprx::AccessStatus::RestoreFailed, {}};
    }

    LOG_DEBUG("[sprx-manager] restored ucred pid=%d tid=%.*s uid=%u",
              static_cast<int>(pid), static_cast<int>(title_id.size()),
              title_id.data(), static_cast<unsigned int>(snap.uid));
    return {onion::sprx::AccessStatus::Allowed, {}};
  }

private:
  TargetCredSnapshot snapshot_{};
};

struct Runtime {
  std::mutex mutex;
  onion::sprx::PtraceSprxRuntime transport;
  PreparedTargetAccess access;
  CurrentGameTarget target;
  onion::sprx::SprxPluginManager manager;

  Runtime() : manager(transport, access, target) {
    manager.set_scan_directory(kSprxDir);
  }
};

Runtime &runtime() {
  static Runtime value;
  return value;
}

void log_report(const char *operation,
                const onion::sprx::SprxStartupReport &report) {
  for (const auto &issue : report.issues)
    LOG_WARN("[sprx-manager] line=%zu plugin=%s: %s", issue.line,
             issue.plugin_id.empty() ? "-" : issue.plugin_id.c_str(),
             issue.message.c_str());
  size_t loaded = 0;
  size_t failed = 0;
  size_t skipped = 0;
  for (const auto &item : report.results) {
    if (item.skipped_dependency)
      ++skipped;
    else if (item.load.succeeded())
      ++loaded;
    else {
      ++failed;
      const std::string detail = onion::sprx::format_load_error(item.load);
      LOG_WARN("[sprx-manager] %s plugin=%s failed: %s (pid=%d remote=0x%08x)",
               operation, item.id.c_str(), detail.c_str(),
               static_cast<int>(item.load.pid),
               static_cast<unsigned int>(
                   static_cast<uint32_t>(item.load.remote_result)));
    }
  }
  LOG_INFO("[sprx-manager] %s catalog=%d entries=%zu loaded=%zu failed=%zu skipped=%zu",
           operation, report.catalog_loaded ? 1 : 0, report.results.size(),
           loaded, failed, skipped);
}

void run(const char *operation, bool reload) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  if (reload) {
    std::vector<onion::sprx::SprxCatalogIssue> issues;
    if (!current.manager.load_catalog(kCatalogPath, &issues)) {
      for (const auto &issue : issues)
        LOG_WARN("[sprx-manager] line=%zu: %s", issue.line, issue.message.c_str());
      return;
    }
  }
  const auto report = reload ? current.manager.start() : current.manager.reconcile();
  log_report(operation, report);
}

bool is_current_target(Runtime &current, pid_t pid, uint32_t app_id,
                       std::string_view title_id) {
  onion::sprx::SprxTarget target;
  if (!current.target.current(&target)) {
    LOG_DEBUG("[sprx-manager] lifecycle target unavailable pid=%d",
              static_cast<int>(pid));
    return false;
  }
  if (target.pid != pid || target.title_id != title_id) {
    LOG_DEBUG("[sprx-manager] stale Big App start pid=%d tid=%.*s current_pid=%d current_tid=%s",
              static_cast<int>(pid), static_cast<int>(title_id.size()),
              title_id.data(), static_cast<int>(target.pid),
              target.title_id.c_str());
    return false;
  }
  (void)app_id;
  return true;
}

} // namespace

void start() { run("startup", true); }
void reconcile() { run("reconcile", false); }

std::vector<onion::sprx::SprxInventoryEntry> inventory() {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  return current.manager.inventory();
}

onion::sprx::SprxOperationResult reload_catalog() {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  const auto result = current.manager.reload_catalog(&issues);
  for (const auto &issue : issues)
    LOG_WARN("[sprx-manager] line=%zu plugin=%s: %s", issue.line,
             issue.plugin_id.empty() ? "-" : issue.plugin_id.c_str(),
             issue.message.c_str());
  return result;
}

onion::sprx::SprxOperationResult load_now(std::string_view id) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  return current.manager.load_now(id);
}

onion::sprx::SprxOperationResult set_enabled(std::string_view id,
                                             bool enabled) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  return current.manager.set_enabled(id, enabled);
}

onion::sprx::SprxOperationResult set_auto_start(std::string_view id,
                                                bool auto_start) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  return current.manager.set_auto_start(id, auto_start);
}

onion::sprx::SprxOperationResult bind_target(
    std::string_view id, onion::sprx::SprxTargetScope scope) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  return current.manager.bind_target(id, scope);
}

onion::sprx::SprxOperationResult remove(std::string_view id) {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  return current.manager.remove(id);
}

bool wait_for_target_ready(pid_t pid, std::string_view title_id) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kBigAppReadyTimeoutMs);
  bool resolved = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!onion_proc_is_alive(pid))
      return false;
    if (pt_resolve(pid, kLoadStartModuleNid) != 0 &&
        pt_resolve(pid, kPthreadCreateNid) != 0) {
      resolved = true;
      break;
    }
    usleep(static_cast<useconds_t>(kBigAppReadyPollMs) * 1000U);
  }
  if (!resolved) {
    LOG_WARN("[sprx-manager] timed out waiting for libkernel NIDs pid=%d tid=%.*s",
             static_cast<int>(pid), static_cast<int>(title_id.size()),
             title_id.data());
    return onion_proc_is_alive(pid);
  }

  for (uint32_t elapsed = 0; elapsed < kBigAppSettleMs;
       elapsed += kBigAppReadyPollMs) {
    if (!onion_proc_is_alive(pid))
      return false;
    usleep(static_cast<useconds_t>(kBigAppReadyPollMs) * 1000U);
  }
  return onion_proc_is_alive(pid);
}

void on_big_app_started(pid_t pid, uint32_t app_id,
                        std::string_view title_id) {
  Runtime &current = runtime();
  bool has_auto_start = false;
  {
    std::lock_guard<std::mutex> lock(current.mutex);
    if (!is_current_target(current, pid, app_id, title_id))
      return;
    (void)current.manager.reload_catalog();
    const auto catalog = current.manager.catalog();
    has_auto_start = !catalog.startup_order(title_id).empty();
  }
  if (!has_auto_start)
    return;

  if (!wait_for_target_ready(pid, title_id))
    return;

  std::lock_guard<std::mutex> lock(current.mutex);
  if (!is_current_target(current, pid, app_id, title_id))
    return;
  log_report("big-app-start", current.manager.reconcile());
}

void on_big_app_exited(pid_t pid, uint32_t app_id,
                       std::string_view title_id) {
  LOG_INFO("[sprx-manager] target exited pid=%d appid=%u tid=%.*s; no daemon-owned SPRX runtime or UI session to release",
           static_cast<int>(pid), app_id, static_cast<int>(title_id.size()),
           title_id.data());
}

void stop() {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  current.manager.stop();
  LOG_INFO("[sprx-manager] stopped target-bound modules");
}

} // namespace onion::daemon::sprx_plugins
