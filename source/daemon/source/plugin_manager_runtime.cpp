#include "plugin_manager_runtime.hpp"
#include "plugin_directory_watcher.hpp"

#include <elfldr_remote.h>
#include <onion/log.h>
#include <onion/plugin_manager.hpp>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::daemon::plugins {
namespace {

std::string pid_key(std::string_view plugin_id) {
  return "plugin_" + std::string(plugin_id);
}

std::string pid_path(std::string_view plugin_id) {
  char path[256]{};
  const std::string key = pid_key(plugin_id);
  if (!onion_system_tmp_pid_path(path, sizeof(path), key.c_str())) return {};
  return path;
}

class DaemonRuntime final : public plugin::ProcessRuntime {
public:
  pid_t recover(std::string_view plugin_id) override {
    const std::string path = pid_path(plugin_id);
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0) return -1;
    char value[32]{};
    const ssize_t count = read(descriptor, value, sizeof(value) - 1);
    close(descriptor);
    if (count <= 0) return -1;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    return errno == 0 && end != value && (*end == '\0' || *end == '\n') &&
                   parsed > 1
               ? static_cast<pid_t>(parsed)
               : -1;
  }

  pid_t launch(const plugin::PluginFile &plugin_file) override {
    if (!elfldr_remote_onion_available()) {
      LOG_ERROR("[plugins] private elfldr :%u unavailable for %s",
                ONION_ELFLDR_PORT, plugin_file.descriptor.plugin_id.c_str());
      return -1;
    }
    const pid_t pid = elfldr_remote_onion_launch_file_get_pid(
        plugin_file.path.c_str(), nullptr);
    if (pid > 1) {
      LOG_INFO("[plugins] started %s %s (%s) pid=%d",
               plugin_file.descriptor.plugin_id.c_str(),
               plugin_file.descriptor.version.c_str(),
               plugin_file.descriptor.name.c_str(), static_cast<int>(pid));
    }
    return pid;
  }

  bool alive(pid_t pid) override { return onion_proc_is_alive(pid); }

  void persist(std::string_view plugin_id, pid_t pid) override {
    const std::string path = pid_path(plugin_id);
    if (path.empty()) return;
    if (pid <= 1) {
      unlink(path.c_str());
      return;
    }
    mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
    mkdir(ONION_SYSTEM_TMP_PID_ROOT, 0777);
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0) return;
    char value[32]{};
    const int length = std::snprintf(value, sizeof(value), "%d\n", pid);
    if (length > 0) (void)write(descriptor, value, static_cast<size_t>(length));
    close(descriptor);
  }

  void stop(pid_t pid, uint32_t flags) override {
    if (pid <= 1 || pid == getpid()) return;
    if (flags & plugin::kFlagStopSupported) {
      (void)kill(pid, SIGTERM);
      for (int attempt = 0; attempt < 10 && onion_proc_is_alive(pid); ++attempt)
        usleep(50 * 1000);
    }
    if (onion_proc_is_alive(pid)) (void)kill(pid, SIGKILL);
  }
};

plugin::Manager &manager() {
  static DaemonRuntime runtime;
  static plugin::Manager instance(plugin::Repository{}, runtime);
  return instance;
}

void watcher_reconcile(void *) { reconcile(); }

PluginDirectoryWatcher &watcher() {
  static PluginDirectoryWatcher instance(plugin::kInstallRoot,
                                         watcher_reconcile, nullptr);
  return instance;
}

void log_report(const char *operation, const plugin::ReconcileReport &report) {
  for (const plugin::DiscoveryIssue &issue : report.issues)
    LOG_WARN("[plugins] %s: %s", issue.path.c_str(), issue.message.c_str());
  LOG_INFO("[plugins] %s: discovered=%zu running=%zu started=%zu failed=%zu",
           operation, report.discovered, report.running, report.started,
           report.failed);
}

} // namespace

void start() {
  log_report("startup", manager().reconcile());
  if (!watcher().start())
    LOG_ERROR("[plugins] directory watcher failed to start");
}

void reconcile() { log_report("reconcile", manager().reconcile()); }

void stop() {
  watcher().stop();
  manager().stop_all();
  LOG_INFO("[plugins] stopped all managed plugin processes");
}

std::vector<plugin::InventoryEntry> inventory() {
  return manager().inventory();
}

plugin::OperationResult start_plugin(std::string_view plugin_id) {
  return manager().start(plugin_id);
}

plugin::OperationResult stop_plugin(std::string_view plugin_id) {
  return manager().stop(plugin_id);
}

plugin::OperationResult reload_plugin(std::string_view plugin_id) {
  return manager().reload(plugin_id);
}

plugin::OperationResult remove_plugin(std::string_view plugin_id) {
  return manager().remove(plugin_id);
}

} // namespace onion::daemon::plugins
