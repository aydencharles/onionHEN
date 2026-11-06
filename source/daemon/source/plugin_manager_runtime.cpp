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
  /**
   * Recovers a process only from a complete, newline-terminated PID marker.
   */
  pid_t recover(std::string_view plugin_id) override {
    const std::string path = pid_path(plugin_id);
    if (path.empty()) return -1;
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
      if (errno != ENOENT)
        LOG_WARN("[plugins] failed to open PID marker for %s: %s",
                 std::string(plugin_id).c_str(), std::strerror(errno));
      return -1;
    }
    char value[32]{};
    size_t offset = 0;
    while (offset < sizeof(value) - 1) {
      const ssize_t count = read(descriptor, value + offset,
                                 sizeof(value) - 1 - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count < 0) {
        const int error = errno;
        close(descriptor);
        LOG_WARN("[plugins] failed to read PID marker for %s: %s",
                 std::string(plugin_id).c_str(), std::strerror(error));
        return -1;
      }
      if (count == 0) break;
      offset += static_cast<size_t>(count);
    }
    close(descriptor);
    if (offset < 2 || value[offset - 1] != '\n') {
      LOG_WARN("[plugins] invalid PID marker for %s; process recovery may relaunch it",
               std::string(plugin_id).c_str());
      return -1;
    }
    value[offset - 1] = '\0';
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end != value + offset - 1 || parsed <= 1) {
      LOG_WARN("[plugins] invalid PID marker for %s; process recovery may relaunch it",
               std::string(plugin_id).c_str());
      return -1;
    }
    return static_cast<pid_t>(parsed);
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

  /**
   * Maintains the PID marker used to recover managed plugins after restart.
   * Reports failures because an unpersisted PID can cause recovery to relaunch
   * a plugin that is already running.
   */
  void persist(std::string_view plugin_id, pid_t pid) override {
    const std::string path = pid_path(plugin_id);
    const int id_length = static_cast<int>(plugin_id.size());
    const char *plugin_name = plugin_id.empty() ? "" : plugin_id.data();
    if (path.empty()) {
      LOG_WARN("[plugins] failed to resolve PID marker path for %.*s",
               id_length, plugin_name);
      return;
    }
    if (pid <= 1) {
      if (unlink(path.c_str()) != 0 && errno != ENOENT)
        LOG_WARN("[plugins] failed to remove PID marker for %.*s: %s",
                 id_length, plugin_name, std::strerror(errno));
      return;
    }
    mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
    mkdir(ONION_SYSTEM_TMP_PID_ROOT, 0777);
    std::string temp_path = path + ".tmp.XXXXXX";
    const int descriptor = mkstemp(temp_path.data());
    if (descriptor < 0) {
      LOG_WARN("[plugins] failed to create temporary PID marker for %.*s: %s; "
               "process recovery may relaunch it",
               id_length, plugin_name, std::strerror(errno));
      return;
    }
    char value[32]{};
    const int length = std::snprintf(value, sizeof(value), "%d\n", pid);
    if (length <= 0) {
      close(descriptor);
      unlink(temp_path.c_str());
      LOG_WARN("[plugins] failed to format PID marker for %.*s; "
               "process recovery may relaunch it",
               id_length, plugin_name);
      return;
    }
    size_t offset = 0;
    while (offset < static_cast<size_t>(length)) {
      const ssize_t written = write(descriptor, value + offset,
                                    static_cast<size_t>(length) - offset);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) {
        const int error = written < 0 ? errno : EIO;
        close(descriptor);
        unlink(temp_path.c_str());
        LOG_WARN("[plugins] failed to write temporary PID marker for %.*s: %s; "
                 "process recovery may relaunch it",
                 id_length, plugin_name, std::strerror(error));
        return;
      }
      offset += static_cast<size_t>(written);
    }
    if (close(descriptor) != 0) {
      const int error = errno;
      unlink(temp_path.c_str());
      LOG_WARN("[plugins] failed to close temporary PID marker for %.*s: %s; "
               "process recovery may relaunch it",
               id_length, plugin_name, std::strerror(error));
      return;
    }
    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
      const int error = errno;
      unlink(temp_path.c_str());
      LOG_WARN("[plugins] failed to publish PID marker for %.*s: %s; "
               "process recovery may relaunch it",
               id_length, plugin_name, std::strerror(error));
    }
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
  log_report("startup", manager().reconcile(true));
  if (!watcher().start())
    LOG_ERROR("[plugins] directory watcher failed to start");
}

void reconcile() {
  log_report("reconcile", manager().reconcile());
}

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

plugin::OperationResult set_auto_start(std::string_view plugin_id, bool enabled) {
  return manager().set_auto_start(plugin_id, enabled);
}

} // namespace onion::daemon::plugins
