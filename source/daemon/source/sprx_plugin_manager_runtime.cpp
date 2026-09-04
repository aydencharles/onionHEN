#include "sprx_plugin_manager_runtime.hpp"

#include "daemon_ops.hpp"

#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/sprx_plugin_manager.hpp>

#include <ps5/kernel.h>

#include <mutex>
#include <string>
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

class PreparedTargetAccess final : public onion::sprx::ITargetAccessPolicy {
public:
  onion::sprx::AccessResult prepare(pid_t pid,
                                     std::string_view) noexcept override {
    if (pid <= 1 || !onion_proc_is_alive(pid) || kernel_get_proc(pid) == 0)
      return {onion::sprx::AccessStatus::PrepareFailed};
    return kernel_get_ucred_uid(pid) == 0
               ? onion::sprx::AccessResult{onion::sprx::AccessStatus::Allowed}
               : onion::sprx::AccessResult{onion::sprx::AccessStatus::Denied};
  }

  onion::sprx::AccessResult restore(pid_t pid,
                                     std::string_view) noexcept override {
    return pid > 1 && onion_proc_is_alive(pid)
               ? onion::sprx::AccessResult{onion::sprx::AccessStatus::Allowed}
               : onion::sprx::AccessResult{onion::sprx::AccessStatus::RestoreFailed};
  }
};

struct Runtime {
  std::mutex mutex;
  onion::sprx::PtraceSprxRuntime transport;
  PreparedTargetAccess access;
  CurrentGameTarget target;
  onion::sprx::SprxPluginManager manager;

  Runtime() : manager(transport, access, target) {}
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
    else
      ++failed;
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

} // namespace

void start() { run("startup", true); }
void reconcile() { run("reconcile", false); }
void stop() {
  Runtime &current = runtime();
  std::lock_guard<std::mutex> lock(current.mutex);
  current.manager.stop();
  LOG_INFO("[sprx-manager] stopped target-bound modules");
}

} // namespace onion::daemon::sprx_plugins
