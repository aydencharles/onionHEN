#include "plugin_directory_watcher.hpp"

#include <onion/log.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::daemon {
namespace {

constexpr long kDebounceNanoseconds = 150 * 1000 * 1000;
constexpr long kFallbackSeconds = 30;

void ensure_directory(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  if (slash != std::string::npos && slash != 0)
    (void)mkdir(path.substr(0, slash).c_str(), 0777);
  (void)mkdir(path.c_str(), 0777);
}

bool register_event(int kq, int fd, int16_t filter, uint16_t flags,
                    uint32_t fflags = 0) {
  struct kevent event {};
  EV_SET(&event, static_cast<uintptr_t>(fd), filter, flags, fflags, 0,
         nullptr);
  return kevent(kq, &event, 1, nullptr, 0, nullptr) == 0;
}

} // namespace

PluginDirectoryWatcher::PluginDirectoryWatcher(std::string path,
                                               Callback callback,
                                               void *context)
    : path_(std::move(path)), callback_(callback), context_(context) {}

PluginDirectoryWatcher::~PluginDirectoryWatcher() { stop(); }

bool PluginDirectoryWatcher::start() {
  std::lock_guard<std::mutex> lock(control_mutex_);
  if (running_.load(std::memory_order_acquire)) return true;
  if (thread_started_) {
    (void)pthread_join(thread_, nullptr);
    close(wake_read_);
    close(wake_write_);
    wake_read_ = -1;
    wake_write_ = -1;
    thread_started_ = false;
  }

  int wake_pipe[2] = {-1, -1};
  if (pipe(wake_pipe) != 0) {
    LOG_ERROR("[plugins] watcher pipe failed: %s", strerror(errno));
    return false;
  }
  wake_read_ = wake_pipe[0];
  wake_write_ = wake_pipe[1];
  running_.store(true, std::memory_order_release);
  if (pthread_create(&thread_, nullptr, thread_entry, this) != 0) {
    running_.store(false, std::memory_order_release);
    close(wake_read_);
    close(wake_write_);
    wake_read_ = -1;
    wake_write_ = -1;
    LOG_ERROR("[plugins] watcher thread creation failed");
    return false;
  }
  thread_started_ = true;
  return true;
}

void PluginDirectoryWatcher::stop() {
  std::lock_guard<std::mutex> lock(control_mutex_);
  if (!thread_started_) return;
  const bool was_running =
      running_.exchange(false, std::memory_order_acq_rel);
  if (was_running && wake_write_ >= 0) {
    const uint8_t wake = 1;
    (void)write(wake_write_, &wake, sizeof(wake));
  }
  (void)pthread_join(thread_, nullptr);
  close(wake_read_);
  close(wake_write_);
  wake_read_ = -1;
  wake_write_ = -1;
  thread_started_ = false;
}

void *PluginDirectoryWatcher::thread_entry(void *context) {
  static_cast<PluginDirectoryWatcher *>(context)->run();
  return nullptr;
}

void PluginDirectoryWatcher::run() {
  const int kq = kqueue();
  if (kq < 0) {
    LOG_ERROR("[plugins] watcher kqueue failed: %s", strerror(errno));
    running_.store(false, std::memory_order_release);
    return;
  }
  if (!register_event(kq, wake_read_, EVFILT_READ, EV_ADD | EV_ENABLE)) {
    LOG_ERROR("[plugins] cannot register watcher wake event: %s",
              strerror(errno));
    close(kq);
    running_.store(false, std::memory_order_release);
    return;
  }

  int directory_fd = -1;
  while (running_.load(std::memory_order_acquire)) {
    if (directory_fd < 0) {
      ensure_directory(path_);
      directory_fd = open(path_.c_str(), O_RDONLY | O_NONBLOCK | O_DIRECTORY);
      if (directory_fd >= 0 &&
          !register_event(kq, directory_fd, EVFILT_VNODE,
                          EV_ADD | EV_ENABLE | EV_CLEAR,
                          NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE |
                              NOTE_RENAME | NOTE_REVOKE)) {
        LOG_ERROR("[plugins] cannot watch %s: %s", path_.c_str(),
                  strerror(errno));
        close(directory_fd);
        directory_fd = -1;
      } else if (directory_fd >= 0) {
        LOG_INFO("[plugins] watching %s", path_.c_str());
      }
    }

    const struct timespec timeout = {
        directory_fd >= 0 ? kFallbackSeconds : 1, 0};
    struct kevent event {};
    const int count = kevent(kq, nullptr, 0, &event, 1, &timeout);
    if (count < 0) {
      if (errno == EINTR) continue;
      LOG_ERROR("[plugins] watcher wait failed: %s", strerror(errno));
      break;
    }
    if (!running_.load(std::memory_order_acquire)) break;
    if (count == 0) {
      if (directory_fd >= 0 && callback_) callback_(context_);
      continue;
    }
    if (event.filter == EVFILT_READ &&
        static_cast<int>(event.ident) == wake_read_)
      break;
    if (event.filter != EVFILT_VNODE ||
        static_cast<int>(event.ident) != directory_fd)
      continue;

    const bool invalidated =
        (event.fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) != 0;
    const struct timespec debounce = {0, kDebounceNanoseconds};
    (void)nanosleep(&debounce, nullptr);
    if (callback_) callback_(context_);
    if (invalidated) {
      close(directory_fd);
      directory_fd = -1;
    }
  }

  if (directory_fd >= 0) close(directory_fd);
  close(kq);
  running_.store(false, std::memory_order_release);
}

} // namespace onion::daemon
