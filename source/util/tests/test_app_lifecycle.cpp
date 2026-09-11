#include "test_harness.h"

#include <onion/app_lifecycle.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct FakeRuntime {
  onion::lifecycle::RuntimeStartupGate startup;
};

void *fake_runtime_ready(void *context) {
  auto *runtime = static_cast<FakeRuntime *>(context);
  runtime->startup.signal(onion::lifecycle::RuntimeStartupStatus::Ready);
  return nullptr;
}

void *fake_runtime_slow_ready(void *context) {
  auto *runtime = static_cast<FakeRuntime *>(context);
  usleep(20 * 1000);
  runtime->startup.signal(onion::lifecycle::RuntimeStartupStatus::Ready);
  return nullptr;
}

int test_fake_runtime_startup_handshake() {
  FakeRuntime runtime;
  pthread_t worker {};
  TEST_ASSERT_EQ_INT(0, pthread_create(&worker, nullptr, fake_runtime_ready,
                                       &runtime));
  TEST_ASSERT_TRUE(runtime.startup.wait_for(std::chrono::milliseconds(100)) ==
                   onion::lifecycle::RuntimeStartupStatus::Ready);
  TEST_ASSERT_EQ_INT(0, pthread_join(worker, nullptr));
  return 0;
}

int test_fake_runtime_timeout_cannot_be_revived() {
  FakeRuntime runtime;
  pthread_t worker {};
  TEST_ASSERT_EQ_INT(0, pthread_create(&worker, nullptr,
                                       fake_runtime_slow_ready, &runtime));
  TEST_ASSERT_TRUE(runtime.startup.wait_for(std::chrono::milliseconds(1)) ==
                   onion::lifecycle::RuntimeStartupStatus::Timeout);
  TEST_ASSERT_EQ_INT(0, pthread_join(worker, nullptr));
  TEST_ASSERT_TRUE(runtime.startup.status() ==
                   onion::lifecycle::RuntimeStartupStatus::Timeout);
  return 0;
}

int test_runtime_startup_gate_ready_failed_and_timeout() {
  onion::lifecycle::RuntimeStartupGate gate;
  TEST_ASSERT_TRUE(gate.status() ==
                   onion::lifecycle::RuntimeStartupStatus::Pending);
  gate.signal(onion::lifecycle::RuntimeStartupStatus::Ready);
  TEST_ASSERT_TRUE(gate.wait_for(std::chrono::milliseconds(1)) ==
                   onion::lifecycle::RuntimeStartupStatus::Ready);

  gate.reset();
  gate.signal(onion::lifecycle::RuntimeStartupStatus::Failed);
  TEST_ASSERT_TRUE(gate.wait_for(std::chrono::milliseconds(1)) ==
                   onion::lifecycle::RuntimeStartupStatus::Failed);

  gate.reset();
  TEST_ASSERT_TRUE(gate.wait_for(std::chrono::milliseconds(1)) ==
                   onion::lifecycle::RuntimeStartupStatus::Timeout);
  gate.signal(onion::lifecycle::RuntimeStartupStatus::Ready);
  TEST_ASSERT_TRUE(gate.status() ==
                   onion::lifecycle::RuntimeStartupStatus::Timeout);
  return 0;
}

int test_fake_syscore_listener_stops_on_owner_exit_only() {
  struct FakeListener {
    pid_t owner_pid = 100;
    bool running = true;

    void on_exit(pid_t event_pid) {
      if (onion::lifecycle::ProcessExitPolicy::should_stop_listener(
              owner_pid, event_pid))
        running = false;
    }
  } listener;

  listener.on_exit(101);
  TEST_ASSERT_TRUE(listener.running);
  listener.on_exit(100);
  TEST_ASSERT_TRUE(!listener.running);
  TEST_ASSERT_TRUE(!onion::lifecycle::ProcessExitPolicy::should_stop_listener(
      -1, 100));
  return 0;
}

struct Invocation {
  int subscriber_priority = 0;
  onion::lifecycle::EventType type;
  uint64_t sequence = 0;
};

class Recorder final : public onion::lifecycle::ISubscriber {
public:
  explicit Recorder(int value, std::vector<Invocation> &out,
                    std::mutex &out_mutex, bool should_succeed = true)
      : value_(value), out_(out), out_mutex_(out_mutex),
        should_succeed_(should_succeed) {}

  int priority() const noexcept override { return value_; }

  bool on_event(const onion::lifecycle::Event &event) noexcept override {
    std::lock_guard<std::mutex> lock(out_mutex_);
    out_.push_back({value_, event.type, event.sequence});
    return should_succeed_;
  }

private:
  int value_;
  std::vector<Invocation> &out_;
  std::mutex &out_mutex_;
  bool should_succeed_;
};

bool wait_for_count(const std::vector<Invocation> &events, std::mutex &mutex,
                    size_t count) {
  for (int attempt = 0; attempt < 1000; ++attempt) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (events.size() == count)
        return true;
    }
    usleep(1000);
  }
  return false;
}

int test_dispatches_fifo_by_priority() {
  std::vector<Invocation> events;
  std::mutex events_mutex;
  Recorder later(20, events, events_mutex);
  Recorder earlier(10, events, events_mutex);
  onion::lifecycle::Dispatcher dispatcher;
  TEST_ASSERT_TRUE(dispatcher.subscribe(later));
  TEST_ASSERT_TRUE(dispatcher.subscribe(earlier));
  TEST_ASSERT_TRUE(!dispatcher.subscribe(earlier));
  TEST_ASSERT_TRUE(dispatcher.start());
  TEST_ASSERT_TRUE(!dispatcher.subscribe(earlier));

  onion::lifecycle::Event started;
  started.type = onion::lifecycle::EventType::BigAppStarted;
  started.app = {123, 7, "CUSA12345"};
  onion::lifecycle::Event exited;
  exited.type = onion::lifecycle::EventType::BigAppExited;
  exited.app = {123, 7, "CUSA12345"};
  TEST_ASSERT_TRUE(dispatcher.publish(std::move(started)));
  TEST_ASSERT_TRUE(dispatcher.publish(std::move(exited)));
  TEST_ASSERT_TRUE(wait_for_count(events, events_mutex, 4));
  dispatcher.stop();

  std::lock_guard<std::mutex> lock(events_mutex);
  TEST_ASSERT_EQ_INT(4, static_cast<int>(events.size()));
  TEST_ASSERT_EQ_INT(10, events[0].subscriber_priority);
  TEST_ASSERT_EQ_INT(20, events[1].subscriber_priority);
  TEST_ASSERT_EQ_INT(10, events[2].subscriber_priority);
  TEST_ASSERT_EQ_INT(20, events[3].subscriber_priority);
  TEST_ASSERT_TRUE(events[0].type == onion::lifecycle::EventType::BigAppStarted);
  TEST_ASSERT_TRUE(events[2].type == onion::lifecycle::EventType::BigAppExited);
  TEST_ASSERT_EQ_U64(1, events[0].sequence);
  TEST_ASSERT_EQ_U64(1, events[1].sequence);
  TEST_ASSERT_EQ_U64(2, events[2].sequence);
  TEST_ASSERT_EQ_U64(2, events[3].sequence);
  return 0;
}

int test_stops_lower_priority_subscribers_after_failure() {
  std::vector<Invocation> events;
  std::mutex events_mutex;
  Recorder failing(10, events, events_mutex, false);
  Recorder blocked(100, events, events_mutex);
  onion::lifecycle::Dispatcher dispatcher;
  TEST_ASSERT_TRUE(dispatcher.subscribe(blocked));
  TEST_ASSERT_TRUE(dispatcher.subscribe(failing));
  TEST_ASSERT_TRUE(dispatcher.start());

  onion::lifecycle::Event event;
  event.type = onion::lifecycle::EventType::BigAppStarted;
  event.app = {123, 7, "CUSA12345"};
  TEST_ASSERT_TRUE(dispatcher.publish(std::move(event)));
  TEST_ASSERT_TRUE(wait_for_count(events, events_mutex, 1));
  dispatcher.stop();

  std::lock_guard<std::mutex> lock(events_mutex);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(events.size()));
  TEST_ASSERT_EQ_INT(10, events[0].subscriber_priority);
  return 0;
}

} // namespace

extern "C" int test_app_lifecycle_suite(void) {
  int failures = 0;
  failures += onion_test_run("app_lifecycle.fake_runtime_handshake",
                             test_fake_runtime_startup_handshake);
  failures += onion_test_run("app_lifecycle.fake_runtime_timeout_cleanup",
                             test_fake_runtime_timeout_cannot_be_revived);
  failures += onion_test_run("app_lifecycle.runtime_startup_gate",
                             test_runtime_startup_gate_ready_failed_and_timeout);
  failures += onion_test_run("app_lifecycle.fake_syscore_owner_exit",
                             test_fake_syscore_listener_stops_on_owner_exit_only);
  failures += onion_test_run("app_lifecycle.serial_priority",
                             test_dispatches_fifo_by_priority);
  failures += onion_test_run("app_lifecycle.failure_stops_chain",
                             test_stops_lower_priority_subscribers_after_failure);
  return failures;
}
