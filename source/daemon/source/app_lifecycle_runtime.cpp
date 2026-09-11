/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "app_lifecycle_runtime.hpp"
#include "daemon_ops.hpp"
#include "sprx_plugin_manager_runtime.hpp"

#include <onion/app_lifecycle.hpp>
#include <onion/log.h>

namespace onion::daemon::app_lifecycle {
namespace {

class AppJailbreakLifecycleSubscriber final
    : public onion::lifecycle::ISubscriber {
public:
  int priority() const noexcept override { return 10; }

  bool on_event(const onion::lifecycle::Event &event) noexcept override {
    const char *title_id = event.app.title_id.c_str();
    bool completed = false;
    switch (event.type) {
    case onion::lifecycle::EventType::BigAppStarted:
      completed = app_jailbreak_on_big_app_started(
          event.app.pid, event.app.app_id, title_id);
      break;
    case onion::lifecycle::EventType::BigAppExited:
      completed = app_jailbreak_on_big_app_exited(
          event.app.pid, event.app.app_id, title_id);
      break;
    }
    if (!completed) {
      LOG_WARN("[lifecycle] AppJailbreak did not acknowledge event type=%u "
               "pid=%d tid=%s",
               static_cast<unsigned>(event.type), static_cast<int>(event.app.pid),
               title_id);
    }
    return completed;
  }
};

class SprxLifecycleSubscriber final : public onion::lifecycle::ISubscriber {
public:
  int priority() const noexcept override { return 100; }

  bool on_event(const onion::lifecycle::Event &event) noexcept override {
    switch (event.type) {
    case onion::lifecycle::EventType::BigAppStarted:
      sprx_plugins::on_big_app_started(event.app.pid, event.app.app_id,
                                       event.app.title_id);
      break;
    case onion::lifecycle::EventType::BigAppExited:
      sprx_plugins::on_big_app_exited(event.app.pid, event.app.app_id,
                                      event.app.title_id);
      break;
    }
    return true;
  }
};

struct Runtime {
  AppJailbreakLifecycleSubscriber app_jailbreak;
  SprxLifecycleSubscriber sprx;
  onion::lifecycle::Dispatcher dispatcher;

  Runtime() : dispatcher(32) {
    (void)dispatcher.subscribe(app_jailbreak);
    (void)dispatcher.subscribe(sprx);
  }
};

Runtime &runtime() {
  static Runtime value;
  return value;
}

bool publish(onion::lifecycle::EventType type, pid_t pid, uint32_t app_id,
             std::string_view title_id) {
  onion::lifecycle::Event event;
  event.type = type;
  event.app.pid = pid;
  event.app.app_id = app_id;
  event.app.title_id = title_id;
  if (!runtime().dispatcher.publish(std::move(event))) {
    LOG_WARN("[lifecycle] dropped event type=%u pid=%d tid=%.*s",
             static_cast<unsigned>(type), static_cast<int>(pid),
             static_cast<int>(title_id.size()), title_id.data());
    return false;
  }
  return true;
}

} // namespace

bool start() {
  const bool started = runtime().dispatcher.start();
  if (!started)
    LOG_ERROR("[lifecycle] dispatcher start failed");
  return started;
}

void stop() { runtime().dispatcher.stop(); }

bool publish_big_app_started(pid_t pid, uint32_t app_id,
                             std::string_view title_id) {
  return publish(onion::lifecycle::EventType::BigAppStarted, pid, app_id,
                 title_id);
}

bool publish_big_app_exited(pid_t pid, uint32_t app_id,
                            std::string_view title_id) {
  return publish(onion::lifecycle::EventType::BigAppExited, pid, app_id,
                 title_id);
}

} // namespace onion::daemon::app_lifecycle
