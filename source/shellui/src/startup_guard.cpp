/* Copyright (C) 2026 OnionHEN */

#include "startup_guard.hpp"

#include <chrono>

namespace onion::shellui {

void StartupCoordinator::reset() noexcept {
  phase_.store(static_cast<uint8_t>(StartupPhase::Idle),
               std::memory_order_release);
  guard_start_ms_.store(0, std::memory_order_release);
  rnps_decrypt_seq_.store(0, std::memory_order_release);
  need_home_reload_ = false;
  need_cc_reload_ = false;
  bundle_seen_ = false;
  settle_frames_left_ = 0;
  phase_baseline_seq_ = 0;
  phase_start_ms_ = 0;
}

void StartupCoordinator::begin(uint64_t now_ms) noexcept {
  need_home_reload_ = false;
  need_cc_reload_ = false;
  bundle_seen_ = false;
  settle_frames_left_ = 0;
  phase_baseline_seq_ = 0;
  phase_start_ms_ = now_ms;
  guard_start_ms_.store(now_ms, std::memory_order_release);
  phase_.store(static_cast<uint8_t>(StartupPhase::WaitingForMainThread),
               std::memory_order_release);
}

bool StartupCoordinator::try_claim_main_thread_init() noexcept {
  uint8_t expected = static_cast<uint8_t>(StartupPhase::WaitingForMainThread);
  return phase_.compare_exchange_strong(
      expected, static_cast<uint8_t>(StartupPhase::RunningMainThreadInit),
      std::memory_order_acq_rel, std::memory_order_acquire);
}

bool StartupCoordinator::try_claim_worker_fallback_init() noexcept {
  uint8_t expected = static_cast<uint8_t>(StartupPhase::WaitingForMainThread);
  return phase_.compare_exchange_strong(
      expected, static_cast<uint8_t>(StartupPhase::RunningWorkerFallbackInit),
      std::memory_order_acq_rel, std::memory_order_acquire);
}

void StartupCoordinator::mark_main_thread_init_done(bool need_home_reload,
                                                    bool need_cc_reload,
                                                    uint64_t now_ms) noexcept {
  need_home_reload_ = need_home_reload;
  need_cc_reload_ = need_cc_reload;
  bundle_seen_ = false;
  phase_start_ms_ = now_ms;
  phase_baseline_seq_ = rnps_decrypt_seq_.load(std::memory_order_acquire);

  if (!need_home_reload_ && !need_cc_reload_) {
    settle_frames_left_ = 0;
    phase_.store(static_cast<uint8_t>(StartupPhase::Completed),
                 std::memory_order_release);
    return;
  }

  settle_frames_left_ = config_.pre_reload_settle_frames;
  phase_.store(static_cast<uint8_t>(StartupPhase::SettlingBeforeHomeReload),
               std::memory_order_release);
}

void StartupCoordinator::mark_worker_fallback_done() noexcept {
  phase_.store(static_cast<uint8_t>(StartupPhase::Completed),
               std::memory_order_release);
}

void StartupCoordinator::mark_failed() noexcept {
  phase_.store(static_cast<uint8_t>(StartupPhase::Failed),
               std::memory_order_release);
}

void StartupCoordinator::on_rnps_bundle_decrypted() noexcept {
  rnps_decrypt_seq_.fetch_add(1, std::memory_order_release);
}

bool StartupCoordinator::enter_reload_phase(StartupPhase expected,
                                            StartupPhase target,
                                            uint64_t now_ms) noexcept {
  bundle_seen_ = false;
  settle_frames_left_ = config_.post_bundle_settle_frames;
  phase_start_ms_ = now_ms;
  phase_baseline_seq_ = rnps_decrypt_seq_.load(std::memory_order_acquire);

  uint8_t expected_raw = static_cast<uint8_t>(expected);
  return phase_.compare_exchange_strong(
      expected_raw, static_cast<uint8_t>(target), std::memory_order_acq_rel,
      std::memory_order_acquire);
}

bool StartupCoordinator::advance_from_reload(StartupPhase current,
                                             StartupPhase next,
                                             uint64_t now_ms) noexcept {
  if (next == StartupPhase::ReloadingControlCenter) {
    return enter_reload_phase(current, next, now_ms);
  }
  uint8_t expected_raw = static_cast<uint8_t>(current);
  return phase_.compare_exchange_strong(
      expected_raw, static_cast<uint8_t>(next), std::memory_order_acq_rel,
      std::memory_order_acquire);
}

StartupTickAction StartupCoordinator::tick_ui_frame(uint64_t now_ms) noexcept {
  const StartupPhase cur = phase();

  if (cur == StartupPhase::SettlingBeforeHomeReload) {
    if (settle_frames_left_ > 0) {
      --settle_frames_left_;
      return StartupTickAction::None;
    }
    if (need_home_reload_) {
      if (enter_reload_phase(StartupPhase::SettlingBeforeHomeReload,
                             StartupPhase::ReloadingHome, now_ms)) {
        return StartupTickAction::ReloadHome;
      }
      return StartupTickAction::None;
    }
    if (need_cc_reload_) {
      if (enter_reload_phase(StartupPhase::SettlingBeforeHomeReload,
                             StartupPhase::ReloadingControlCenter, now_ms)) {
        return StartupTickAction::ReloadControlCenter;
      }
      return StartupTickAction::None;
    }
    (void)advance_from_reload(StartupPhase::SettlingBeforeHomeReload,
                              StartupPhase::Completed, now_ms);
    return StartupTickAction::None;
  }

  if (cur == StartupPhase::ReloadingHome ||
      cur == StartupPhase::ReloadingControlCenter) {
    const uint32_t seq = rnps_decrypt_seq_.load(std::memory_order_acquire);
    if (!bundle_seen_ && seq != phase_baseline_seq_) {
      bundle_seen_ = true;
    }

    bool ready_to_advance = false;
    if (bundle_seen_) {
      if (settle_frames_left_ > 0) {
        --settle_frames_left_;
      }
      if (settle_frames_left_ == 0) {
        ready_to_advance = true;
      }
    }

    const uint64_t elapsed_ms =
        (now_ms >= phase_start_ms_) ? (now_ms - phase_start_ms_) : 0;
    if (elapsed_ms >= config_.reload_phase_timeout_ms) {
      ready_to_advance = true;
    }

    if (!ready_to_advance) {
      return StartupTickAction::None;
    }

    if (cur == StartupPhase::ReloadingHome && need_cc_reload_) {
      if (advance_from_reload(StartupPhase::ReloadingHome,
                              StartupPhase::ReloadingControlCenter, now_ms)) {
        return StartupTickAction::ReloadControlCenter;
      }
      return StartupTickAction::None;
    }

    (void)advance_from_reload(cur, StartupPhase::Completed, now_ms);
    return StartupTickAction::None;
  }

  return StartupTickAction::None;
}

bool StartupCoordinator::force_complete_timeout() noexcept {
  for (;;) {
    uint8_t cur_raw = phase_.load(std::memory_order_acquire);
    const StartupPhase cur = static_cast<StartupPhase>(cur_raw);
    if (cur != StartupPhase::SettlingBeforeHomeReload &&
        cur != StartupPhase::ReloadingHome &&
        cur != StartupPhase::ReloadingControlCenter) {
      return false;
    }
    if (phase_.compare_exchange_weak(
            cur_raw, static_cast<uint8_t>(StartupPhase::Completed),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      return true;
    }
  }
}

bool StartupCoordinator::should_suppress_input(uint64_t now_ms) const noexcept {
  const StartupPhase cur = phase();
  switch (cur) {
  case StartupPhase::WaitingForMainThread:
  case StartupPhase::RunningMainThreadInit:
  case StartupPhase::RunningWorkerFallbackInit:
  case StartupPhase::SettlingBeforeHomeReload:
  case StartupPhase::ReloadingHome:
  case StartupPhase::ReloadingControlCenter:
    break;
  case StartupPhase::Idle:
  case StartupPhase::Completed:
  case StartupPhase::Failed:
    return false;
  }

  const uint64_t start_ms = guard_start_ms_.load(std::memory_order_acquire);
  if (now_ms >= start_ms &&
      (now_ms - start_ms) >= config_.hard_input_suppress_cap_ms) {
    return false;
  }
  return true;
}

StartupPhase StartupCoordinator::phase() const noexcept {
  return static_cast<StartupPhase>(phase_.load(std::memory_order_acquire));
}

bool StartupCoordinator::is_completed() const noexcept {
  return phase() == StartupPhase::Completed;
}

bool StartupCoordinator::is_failed() const noexcept {
  return phase() == StartupPhase::Failed;
}

StartupCoordinator &global_startup_coordinator() noexcept {
  static StartupCoordinator coordinator{};
  return coordinator;
}

uint64_t startup_monotonic_ms() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool shellui_startup_should_suppress_input() noexcept {
  return global_startup_coordinator().should_suppress_input(
      startup_monotonic_ms());
}

void shellui_startup_on_rnps_bundle_decrypted() noexcept {
  global_startup_coordinator().on_rnps_bundle_decrypted();
}

} // namespace onion::shellui
