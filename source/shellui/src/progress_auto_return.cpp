/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Reusable progress auto-return controller.
 */

#include "progress_auto_return.hpp"

#include <ctime>

#ifndef ONION_HOST_TEST
#include "toolbox_navigation.hpp"
#include "shellui_state.hpp"
#include <onion/platform.h>
#endif

namespace toolbox {

uint64_t IPageStackNavigator::now_ms() const {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000 + (ts.tv_nsec / 1000000);
}

#ifndef ONION_HOST_TEST
bool DefaultPageStackNavigator::is_under_transition() const {
  return toolbox_is_stack_under_transition();
}

bool DefaultPageStackNavigator::pop() {
  return toolbox_pop_page();
}

Page DefaultPageStackNavigator::current_active_page() const {
  return g_ui.active_page;
}
#else
bool DefaultPageStackNavigator::is_under_transition() const {
  return false;
}

bool DefaultPageStackNavigator::pop() {
  return false;
}

Page DefaultPageStackNavigator::current_active_page() const {
  return Page::None;
}
#endif

ProgressAutoReturn::ProgressAutoReturn(
    AutoReturnConfig config,
    std::shared_ptr<IPageStackNavigator> navigator)
    : config_(config),
      navigator_(navigator ? std::move(navigator)
                            : std::make_shared<DefaultPageStackNavigator>()) {}

void ProgressAutoReturn::reset() {
  completed_time_ms_ = 0;
  triggered_ = false;
}

bool ProgressAutoReturn::poll(ProgressOutcome outcome, uint64_t now_monotonic_ms) {
  if (triggered_ || outcome == ProgressOutcome::Ongoing) {
    if (outcome == ProgressOutcome::Ongoing) {
      completed_time_ms_ = 0;
    }
    return false;
  }

  const uint64_t current_time =
      (now_monotonic_ms != 0) ? now_monotonic_ms : navigator_->now_ms();

  if (completed_time_ms_ == 0) {
    completed_time_ms_ = current_time;
#ifndef ONION_HOST_TEST
    LOG_DEBUG("progress_auto_return: outcome=%d reached, dwell timer started",
              static_cast<int>(outcome));
#endif
    return false;
  }

  const uint64_t elapsed = (current_time >= completed_time_ms_)
                               ? (current_time - completed_time_ms_)
                               : 0;
  const uint64_t dwell_limit = (outcome == ProgressOutcome::Success)
                                   ? config_.success_dwell_ms
                                   : config_.error_dwell_ms;

  if (elapsed < dwell_limit) {
    return false;
  }

  // Dwell timer expired: verify page affinity
  if (config_.target_page != Page::None &&
      navigator_->current_active_page() != config_.target_page) {
#ifndef ONION_HOST_TEST
    LOG_DEBUG("progress_auto_return: active_page=%u != target_page=%u, skip pop",
              static_cast<unsigned>(navigator_->current_active_page()),
              static_cast<unsigned>(config_.target_page));
#endif
    triggered_ = true;
    return false;
  }

  // Verify transition safety
  if (navigator_->is_under_transition()) {
#ifndef ONION_HOST_TEST
    LOG_DEBUG("progress_auto_return: stack under transition, deferring pop");
#endif
    return false;
  }

#ifndef ONION_HOST_TEST
  LOG_DEBUG("progress_auto_return: executing pop after %llu ms dwell",
            static_cast<unsigned long long>(elapsed));
#endif
  triggered_ = true;
  return navigator_->pop();
}

} // namespace toolbox
