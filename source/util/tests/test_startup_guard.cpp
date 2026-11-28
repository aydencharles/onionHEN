/* Host tests for ShellUI startup input guard, main-thread handoff, and staggered RNPS reload. */
#include "test_harness.h"
#include "startup_guard.hpp"

using namespace onion::shellui;

static int test_staggered_reload_with_bundle_events() {
  StartupTimingConfig cfg{};
  cfg.pre_reload_settle_frames = 2;
  cfg.post_bundle_settle_frames = 3;
  cfg.reload_phase_timeout_ms = 1200;
  cfg.hard_input_suppress_cap_ms = 6000;

  StartupCoordinator coord(cfg);
  TEST_ASSERT_TRUE(!coord.should_suppress_input(100));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.phase()),
                     static_cast<int>(StartupPhase::Idle));

  coord.begin(1000);
  TEST_ASSERT_TRUE(coord.should_suppress_input(1000));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.phase()),
                     static_cast<int>(StartupPhase::WaitingForMainThread));

  /* Main thread claims init; worker fallback is mutually excluded. */
  TEST_ASSERT_TRUE(coord.try_claim_main_thread_init());
  TEST_ASSERT_TRUE(!coord.try_claim_worker_fallback_init());
  TEST_ASSERT_TRUE(!coord.try_claim_main_thread_init());
  TEST_ASSERT_TRUE(coord.should_suppress_input(1010));

  coord.mark_main_thread_init_done(/*need_home=*/true, /*need_cc=*/true, 1020);
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.phase()),
                     static_cast<int>(StartupPhase::SettlingBeforeHomeReload));
  TEST_ASSERT_TRUE(coord.should_suppress_input(1020));

  /* 2 settle frames before Home reload fires on the 3rd tick. */
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1036)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1052)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1068)),
                     static_cast<int>(StartupTickAction::ReloadHome));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.phase()),
                     static_cast<int>(StartupPhase::ReloadingHome));
  TEST_ASSERT_TRUE(coord.should_suppress_input(1068));

  /* Before ioctl decrypts NPXS40002, frames do not advance early. */
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1100)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.phase()),
                     static_cast<int>(StartupPhase::ReloadingHome));

  /* ioctl_hook observes HomeUI bundle decrypt; 3 settle frames then CC reload. */
  coord.on_rnps_bundle_decrypted();
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1116)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1132)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1148)),
                     static_cast<int>(StartupTickAction::ReloadControlCenter));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.phase()),
                     static_cast<int>(StartupPhase::ReloadingControlCenter));
  TEST_ASSERT_TRUE(coord.should_suppress_input(1148));

  /* ioctl_hook observes Control Center bundle decrypt; 3 settle frames then Completed. */
  coord.on_rnps_bundle_decrypted();
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1164)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1180)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1196)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_TRUE(coord.is_completed());
  TEST_ASSERT_TRUE(!coord.should_suppress_input(1196));
  return 0;
}

static int test_reload_timeout_without_ioctl_and_cc_skip() {
  StartupTimingConfig cfg{};
  cfg.pre_reload_settle_frames = 0;
  cfg.post_bundle_settle_frames = 5;
  cfg.reload_phase_timeout_ms = 500;
  cfg.hard_input_suppress_cap_ms = 6000;

  StartupCoordinator coord(cfg);
  coord.begin(100);
  TEST_ASSERT_TRUE(coord.try_claim_main_thread_init());
  coord.mark_main_thread_init_done(/*need_home=*/true, /*need_cc=*/false, 110);

  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(120)),
                     static_cast<int>(StartupTickAction::ReloadHome));
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(619)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_TRUE(!coord.is_completed());

  /* At +500 ms timeout with no ioctl and no CC reload, completes directly. */
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(620)),
                     static_cast<int>(StartupTickAction::None));
  TEST_ASSERT_TRUE(coord.is_completed());
  TEST_ASSERT_TRUE(!coord.should_suppress_input(620));
  return 0;
}

static int test_worker_fallback_and_failure_release_input() {
  StartupCoordinator coord{};
  coord.begin(500);
  TEST_ASSERT_TRUE(coord.should_suppress_input(500));

  /* Worker claims fallback; main thread cannot claim afterwards. */
  TEST_ASSERT_TRUE(coord.try_claim_worker_fallback_init());
  TEST_ASSERT_TRUE(!coord.try_claim_main_thread_init());
  TEST_ASSERT_TRUE(coord.should_suppress_input(600));

  coord.mark_worker_fallback_done();
  TEST_ASSERT_TRUE(coord.is_completed());
  TEST_ASSERT_TRUE(!coord.should_suppress_input(600));

  /* Failure also releases input suppression immediately. */
  coord.begin(1000);
  TEST_ASSERT_TRUE(coord.try_claim_main_thread_init());
  TEST_ASSERT_TRUE(coord.should_suppress_input(1050));
  coord.mark_failed();
  TEST_ASSERT_TRUE(coord.is_failed());
  TEST_ASSERT_TRUE(!coord.should_suppress_input(1050));
  return 0;
}

static int test_hard_input_cap_and_force_complete_timeout() {
  StartupTimingConfig cfg{};
  cfg.pre_reload_settle_frames = 1;
  cfg.post_bundle_settle_frames = 10;
  cfg.reload_phase_timeout_ms = 2000;
  cfg.hard_input_suppress_cap_ms = 3000;

  StartupCoordinator coord(cfg);
  coord.begin(1000);
  TEST_ASSERT_TRUE(coord.should_suppress_input(3999));
  /* Hard monotonic cap guarantees controller input can never stay locked. */
  TEST_ASSERT_TRUE(!coord.should_suppress_input(4000));

  /* force_complete_timeout only completes reload/settle phases. */
  TEST_ASSERT_TRUE(!coord.force_complete_timeout());
  TEST_ASSERT_TRUE(coord.try_claim_main_thread_init());
  TEST_ASSERT_TRUE(!coord.force_complete_timeout());
  coord.mark_main_thread_init_done(/*need_home=*/true, /*need_cc=*/true, 1100);
  TEST_ASSERT_TRUE(coord.force_complete_timeout());
  TEST_ASSERT_TRUE(coord.is_completed());
  TEST_ASSERT_TRUE(!coord.force_complete_timeout());
  /* Subsequent UI tick is a no-op once force-completed. */
  TEST_ASSERT_EQ_INT(static_cast<int>(coord.tick_ui_frame(1200)),
                     static_cast<int>(StartupTickAction::None));
  return 0;
}

extern "C" int test_startup_guard_suite(void) {
  int failures = 0;
  failures += onion_test_run("startup_guard.staggered_reload",
                             test_staggered_reload_with_bundle_events);
  failures += onion_test_run("startup_guard.timeout_and_cc_skip",
                             test_reload_timeout_without_ioctl_and_cc_skip);
  failures += onion_test_run("startup_guard.fallback_and_failure",
                             test_worker_fallback_and_failure_release_input);
  failures += onion_test_run("startup_guard.hard_cap_and_force_complete",
                             test_hard_input_cap_and_force_complete_timeout);
  return failures;
}
