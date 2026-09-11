#include "test_harness.h"

#include <onion/big_app_collect_policy.hpp>

namespace {

using onion::lifecycle::BigAppCollectAction;
using onion::lifecycle::BigAppCollectReason;
using onion::lifecycle::decide_big_app_started;
using onion::lifecycle::ExecSnapshot;
using onion::lifecycle::RunningBigAppSnapshot;

int test_publish_when_exec_is_running_big_app(void) {
  ExecSnapshot exec;
  exec.pid = 179;
  exec.app_info_rc = 0;
  exec.app_id = 32792;
  exec.title_id = "LAPY20011";

  RunningBigAppSnapshot running;
  running.query_ok = true;
  running.pid = 179;
  running.app_id = 32792;
  running.title_id = "LAPY20011";

  const auto decision = decide_big_app_started(exec, running, false);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::PublishStarted);
  TEST_ASSERT_TRUE(decision.reason == BigAppCollectReason::PublishFromExec);
  TEST_ASSERT_EQ_INT(179, static_cast<int>(decision.pid));
  TEST_ASSERT_EQ_INT(32792, static_cast<int>(decision.app_id));
  TEST_ASSERT_TRUE(decision.title_id == "LAPY20011");
  return 0;
}

int test_reconcile_cdlg_exec_to_running_homebrew(void) {
  ExecSnapshot exec;
  exec.pid = 180;
  exec.app_info_rc = 0;
  exec.app_id = 32793;
  exec.title_id = "NPXS40093";

  RunningBigAppSnapshot running;
  running.query_ok = true;
  running.pid = 179;
  running.app_id = 32792;
  running.title_id = "LAPY20011";

  const auto decision = decide_big_app_started(exec, running, false);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::PublishStarted);
  TEST_ASSERT_TRUE(decision.reason == BigAppCollectReason::PublishFromRunning);
  TEST_ASSERT_EQ_INT(179, static_cast<int>(decision.pid));
  TEST_ASSERT_TRUE(decision.title_id == "LAPY20011");
  return 0;
}

int test_reconcile_when_exec_identity_not_ready(void) {
  ExecSnapshot exec;
  exec.pid = 179;
  exec.app_info_rc = -1;

  RunningBigAppSnapshot running;
  running.query_ok = true;
  running.pid = 179;
  running.app_id = 32792;
  running.title_id = "LAPY20011";

  const auto decision = decide_big_app_started(exec, running, false);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::PublishStarted);
  TEST_ASSERT_TRUE(decision.reason == BigAppCollectReason::PublishFromRunning);
  TEST_ASSERT_EQ_INT(179, static_cast<int>(decision.pid));
  return 0;
}

int test_ignore_already_tracked_running_pid(void) {
  ExecSnapshot exec;
  exec.pid = 179;
  exec.app_info_rc = 0;
  exec.app_id = 32792;
  exec.title_id = "LAPY20011";

  RunningBigAppSnapshot running;
  running.query_ok = true;
  running.pid = 179;
  running.app_id = 32792;
  running.title_id = "LAPY20011";

  const auto decision = decide_big_app_started(exec, running, true);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::Ignore);
  TEST_ASSERT_TRUE(decision.reason == BigAppCollectReason::AlreadyTracked);
  TEST_ASSERT_EQ_INT(179, static_cast<int>(decision.pid));
  return 0;
}

int test_publish_exec_when_running_query_not_ready(void) {
  ExecSnapshot exec;
  exec.pid = 99;
  exec.app_info_rc = 0;
  exec.app_id = 24;
  exec.title_id = "LAPY20011";

  RunningBigAppSnapshot running;
  const auto decision = decide_big_app_started(exec, running, false);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::PublishStarted);
  TEST_ASSERT_TRUE(decision.reason == BigAppCollectReason::PublishFromExec);
  TEST_ASSERT_EQ_INT(99, static_cast<int>(decision.pid));
  TEST_ASSERT_TRUE(decision.title_id == "LAPY20011");
  return 0;
}

int test_skip_fork_until_identity_or_running_query(void) {
  ExecSnapshot exec;
  exec.pid = 99;

  RunningBigAppSnapshot running;
  const auto decision = decide_big_app_started(exec, running, false);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::Ignore);
  TEST_ASSERT_TRUE(decision.reason ==
                   BigAppCollectReason::RunningQueryNotReady);
  return 0;
}

int test_skip_system_exec_until_running_query(void) {
  ExecSnapshot exec;
  exec.pid = 100;
  exec.app_info_rc = 0;
  exec.app_id = 25;
  exec.title_id = "NPXS40093";

  RunningBigAppSnapshot running;
  const auto decision = decide_big_app_started(exec, running, false);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::Ignore);
  TEST_ASSERT_TRUE(decision.reason ==
                   BigAppCollectReason::ExecNotRunningBigApp);
  return 0;
}

int test_skip_when_running_pid_invalid(void) {
  ExecSnapshot exec;
  RunningBigAppSnapshot running;
  running.query_ok = true;
  running.pid = -1;
  running.app_id = 32792;
  running.title_id = "LAPY20011";

  const auto decision = decide_big_app_started(exec, running, false);
  TEST_ASSERT_TRUE(decision.action == BigAppCollectAction::Ignore);
  TEST_ASSERT_TRUE(decision.reason == BigAppCollectReason::RunningPidInvalid);
  return 0;
}

int test_reconcile_retry_delays(void) {
  TEST_ASSERT_EQ_INT(3, onion::lifecycle::kBigAppReconcileBurstCount);
  TEST_ASSERT_EQ_INT(100, onion::lifecycle::kBigAppReconcileBurstMs[0]);
  TEST_ASSERT_EQ_INT(300, onion::lifecycle::kBigAppReconcileBurstMs[1]);
  TEST_ASSERT_EQ_INT(1000, onion::lifecycle::kBigAppReconcileBurstMs[2]);
  TEST_ASSERT_EQ_INT(2000, onion::lifecycle::kBigAppReconcileFallbackMs);
  TEST_ASSERT_STREQ("running", onion::lifecycle::big_app_collect_reason_name(
                                   BigAppCollectReason::PublishFromRunning));
  TEST_ASSERT_STREQ("exec", onion::lifecycle::big_app_collect_reason_name(
                                BigAppCollectReason::PublishFromExec));
  return 0;
}

} // namespace

extern "C" int test_big_app_collect_policy_suite(void) {
  int failures = 0;
  failures += onion_test_run("big_app_collect.exec_matches_running",
                             test_publish_when_exec_is_running_big_app);
  failures += onion_test_run("big_app_collect.cdlg_reconciles_homebrew",
                             test_reconcile_cdlg_exec_to_running_homebrew);
  failures += onion_test_run("big_app_collect.exec_identity_not_ready",
                             test_reconcile_when_exec_identity_not_ready);
  failures += onion_test_run("big_app_collect.already_tracked",
                             test_ignore_already_tracked_running_pid);
  failures += onion_test_run("big_app_collect.exec_before_running_query",
                             test_publish_exec_when_running_query_not_ready);
  failures += onion_test_run("big_app_collect.fork_without_identity",
                             test_skip_fork_until_identity_or_running_query);
  failures += onion_test_run("big_app_collect.system_exec_without_running",
                             test_skip_system_exec_until_running_query);
  failures += onion_test_run("big_app_collect.running_pid_invalid",
                             test_skip_when_running_pid_invalid);
  failures += onion_test_run("big_app_collect.retry_delays",
                             test_reconcile_retry_delays);
  return failures;
}
