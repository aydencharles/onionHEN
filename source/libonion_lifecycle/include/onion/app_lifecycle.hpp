/* Copyright (C) 2026 OnionHEN / LightningMods */

#pragma once

#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

#include <sys/types.h>

namespace onion::lifecycle {

enum class RuntimeStartupStatus : uint8_t {
  Pending,
  Ready,
  Failed,
  Timeout,
};

/** Thread-safe startup handshake shared by a runtime owner and its starter. */
class RuntimeStartupGate final {
public:
  RuntimeStartupGate() = default;

  RuntimeStartupGate(const RuntimeStartupGate &) = delete;
  RuntimeStartupGate &operator=(const RuntimeStartupGate &) = delete;

  void reset() noexcept;
  void signal(RuntimeStartupStatus status) noexcept;
  RuntimeStartupStatus wait_for(std::chrono::milliseconds timeout) noexcept;
  RuntimeStartupStatus status() const noexcept;

private:
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  RuntimeStartupStatus status_ = RuntimeStartupStatus::Pending;
};

/** Keeps the owner process exit from being mistaken for an app exit. */
class ProcessExitPolicy final {
public:
  static bool should_stop_listener(pid_t owner_pid, pid_t event_pid) noexcept {
    return owner_pid > 1 && owner_pid == event_pid;
  }
};

enum class EventType : uint8_t {
  BigAppStarted,
  BigAppExited,
};

struct BigApp {
  pid_t pid = -1;
  uint32_t app_id = 0;
  std::string title_id;
};

struct Event {
  EventType type = EventType::BigAppStarted;
  BigApp app;
  uint64_t sequence = 0;
};

/**
 * A lifecycle callback executes on the dispatcher's one worker thread.
 * Lower priorities run first; equal priorities preserve subscription order.
 */
class ISubscriber {
public:
  virtual ~ISubscriber() = default;
  virtual int priority() const noexcept = 0;
  /** Return false to stop dispatching this event to lower-priority subscribers. */
  virtual bool on_event(const Event &event) noexcept = 0;
};

/**
 * Bounded, FIFO lifecycle dispatcher. Event collection may happen on a
 * kqueue thread, but subscribers are always invoked serially on this worker.
 * Subscriptions are immutable after start to keep ordering deterministic.
 */
class Dispatcher final {
public:
  explicit Dispatcher(size_t max_pending = 32) noexcept;
  ~Dispatcher();

  Dispatcher(const Dispatcher &) = delete;
  Dispatcher &operator=(const Dispatcher &) = delete;

  bool subscribe(ISubscriber &subscriber) noexcept;
  bool start() noexcept;
  void stop() noexcept;
  bool publish(Event event) noexcept;

private:
  static void *worker_entry(void *context) noexcept;
  void run() noexcept;

  size_t max_pending_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::vector<ISubscriber *> subscribers_;
  std::deque<Event> pending_;
  pthread_t worker_ {};
  uint64_t next_sequence_ = 1;
  bool started_ = false;
  bool stopping_ = false;
};

} // namespace onion::lifecycle
