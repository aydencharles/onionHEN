#pragma once

#include <atomic>
#include <mutex>
#include <pthread.h>
#include <string>

namespace onion::daemon {

class PluginDirectoryWatcher {
public:
  using Callback = void (*)(void *context);

  PluginDirectoryWatcher(std::string path, Callback callback, void *context);
  ~PluginDirectoryWatcher();

  PluginDirectoryWatcher(const PluginDirectoryWatcher &) = delete;
  PluginDirectoryWatcher &operator=(const PluginDirectoryWatcher &) = delete;

  bool start();
  void stop();
  bool running() const { return running_.load(std::memory_order_acquire); }

private:
  static void *thread_entry(void *context);
  void run();

  std::string path_;
  Callback callback_ = nullptr;
  void *context_ = nullptr;
  std::atomic_bool running_{false};
  pthread_t thread_{};
  bool thread_started_ = false;
  int wake_read_ = -1;
  int wake_write_ = -1;
  mutable std::mutex control_mutex_;
};

} // namespace onion::daemon
