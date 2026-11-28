/* Copyright (C) 2026 OnionHEN
 *
 * Startup coordination for ShellUI injection:
 *   1. Suppresses user controller input during hook installation and initial
 *      RNPS (HomeUI / Control Center) reloads, with a hard time-cap failsafe.
 *   2. Hands Mono UI initialization and remaining hook installation to the
 *      PUI UI main thread (OnRender_Hook), with an atomic worker fallback if
 *      PUI is unavailable or paused.
 *   3. Staggers NPXS40002 (HomeUI) and NPXS40003 (Control Center) ReloadApp
 *      calls across frames and waits for RNPS bundle decryption + settle
 *      before lifting input suppression and publishing ONION_READY_TOOLBOX.
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace onion::shellui {

enum class StartupPhase : uint8_t {
  Idle = 0,
  WaitingForMainThread,
  RunningMainThreadInit,
  RunningWorkerFallbackInit,
  SettlingBeforeHomeReload,
  ReloadingHome,
  ReloadingControlCenter,
  Completed,
  Failed,
};

enum class StartupTickAction : uint8_t {
  None = 0,
  ReloadHome,
  ReloadControlCenter,
};

struct StartupTimingConfig {
  uint32_t pre_reload_settle_frames = 2;
  uint32_t post_bundle_settle_frames = 18;
  uint64_t reload_phase_timeout_ms = 1200;
  uint64_t hard_input_suppress_cap_ms = 6000;
};

class StartupCoordinator final {
public:
  explicit StartupCoordinator(StartupTimingConfig config = {}) noexcept
      : config_(config) {}

  void reset() noexcept;
  void begin(uint64_t now_ms) noexcept;

  bool try_claim_main_thread_init() noexcept;
  bool try_claim_worker_fallback_init() noexcept;

  void mark_main_thread_init_done(bool need_home_reload, bool need_cc_reload,
                                  uint64_t now_ms) noexcept;
  void mark_worker_fallback_done() noexcept;
  void mark_failed() noexcept;

  /* Lock-free notification from ioctl_hook on any thread after DECRYPT_RNPS_BUNDLE. */
  void on_rnps_bundle_decrypted() noexcept;

  /* Called once per UI frame on the PUI main thread (OnRender_Hook). */
  StartupTickAction tick_ui_frame(uint64_t now_ms) noexcept;

  /* Worker-side escape hatch if UI stops ticking during staggered reloads. */
  bool force_complete_timeout() noexcept;

  /* Lock-free input gate check for GamePad.GetData and OnShareButton. */
  bool should_suppress_input(uint64_t now_ms) const noexcept;

  StartupPhase phase() const noexcept;
  bool is_completed() const noexcept;
  bool is_failed() const noexcept;

private:
  bool enter_reload_phase(StartupPhase expected, StartupPhase target,
                          uint64_t now_ms) noexcept;
  bool advance_from_reload(StartupPhase current, StartupPhase next,
                           uint64_t now_ms) noexcept;

  StartupTimingConfig config_{};
  std::atomic<uint8_t> phase_{static_cast<uint8_t>(StartupPhase::Idle)};
  std::atomic<uint64_t> guard_start_ms_{0};
  std::atomic<uint32_t> rnps_decrypt_seq_{0};

  /* Written/read exclusively on the UI main thread after init handoff. */
  bool need_home_reload_ = false;
  bool need_cc_reload_ = false;
  bool bundle_seen_ = false;
  uint32_t settle_frames_left_ = 0;
  uint32_t phase_baseline_seq_ = 0;
  uint64_t phase_start_ms_ = 0;
};

StartupCoordinator &global_startup_coordinator() noexcept;
uint64_t startup_monotonic_ms() noexcept;

bool shellui_startup_should_suppress_input() noexcept;
void shellui_startup_on_rnps_bundle_decrypted() noexcept;

} // namespace onion::shellui
