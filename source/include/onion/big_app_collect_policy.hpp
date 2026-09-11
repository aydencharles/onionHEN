/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Decide when a SysCore process event should publish BigAppStarted.
 *
 * NOTE_TRACK does deliver the game child's NOTE_EXEC. GetAppInfo is often
 * already filled from SysCore appParam at that instant, but
 * sceSystemServiceGetAppIdOfRunningBigApp is not: ResArbitrator registers
 * the Big App after EXEC. Prefer a usable running-Big-App query when it is
 * ready (Cdlg EXEC, SysCore FORK). Otherwise publish from a non-system EXEC
 * identity instead of waiting for the reconcile timer.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <sys/types.h>

namespace onion::lifecycle {

enum class BigAppCollectAction {
  Ignore,
  PublishStarted,
};

enum class BigAppCollectReason {
  AlreadyTracked,
  ExecNotReady,
  ExecNotRunningBigApp,
  RunningQueryNotReady,
  RunningPidInvalid,
  PublishFromExec,
  PublishFromRunning,
};

struct ExecSnapshot {
  pid_t pid = -1;
  int app_info_rc = -1;
  uint32_t app_id = 0;
  std::string_view title_id;
  bool already_tracked = false;
};

struct RunningBigAppSnapshot {
  bool query_ok = false;
  pid_t pid = -1;
  int app_id = -1;
  std::string_view title_id;
};

struct BigAppCollectDecision {
  BigAppCollectAction action = BigAppCollectAction::Ignore;
  BigAppCollectReason reason = BigAppCollectReason::RunningQueryNotReady;
  pid_t pid = -1;
  uint32_t app_id = 0;
  std::string_view title_id;
};

inline constexpr int kBigAppReconcileBurstMs[] = {100, 300, 1000};
inline constexpr int kBigAppReconcileBurstCount =
    static_cast<int>(sizeof(kBigAppReconcileBurstMs) /
                     sizeof(kBigAppReconcileBurstMs[0]));
inline constexpr int kBigAppReconcileFallbackMs = 2000;

inline bool running_snapshot_usable(const RunningBigAppSnapshot &running) {
  return running.query_ok && running.pid > 1 && running.app_id >= 0 &&
         !running.title_id.empty();
}

inline bool exec_snapshot_usable(const ExecSnapshot &exec) {
  return exec.pid > 1 && exec.app_info_rc == 0 && !exec.title_id.empty() &&
         !exec.already_tracked;
}

inline bool is_system_app_title(std::string_view title_id) {
  return title_id.size() >= 4 && title_id.compare(0, 4, "NPXS") == 0;
}

inline const char *big_app_collect_reason_name(BigAppCollectReason reason) {
  switch (reason) {
  case BigAppCollectReason::AlreadyTracked:
    return "already-tracked";
  case BigAppCollectReason::ExecNotReady:
    return "exec-not-ready";
  case BigAppCollectReason::ExecNotRunningBigApp:
    return "exec-not-running-big-app";
  case BigAppCollectReason::RunningQueryNotReady:
    return "running-query-not-ready";
  case BigAppCollectReason::RunningPidInvalid:
    return "running-pid-invalid";
  case BigAppCollectReason::PublishFromExec:
    return "exec";
  case BigAppCollectReason::PublishFromRunning:
    return "running";
  }
  return "unknown";
}

inline BigAppCollectDecision
decide_big_app_started(const ExecSnapshot &exec,
                       const RunningBigAppSnapshot &running,
                       bool running_pid_already_tracked) {
  BigAppCollectDecision decision;

  if (running_snapshot_usable(running)) {
    decision.pid = running.pid;
    decision.app_id = static_cast<uint32_t>(running.app_id);
    decision.title_id = running.title_id;
    if (running_pid_already_tracked) {
      decision.action = BigAppCollectAction::Ignore;
      decision.reason = BigAppCollectReason::AlreadyTracked;
      return decision;
    }
    decision.action = BigAppCollectAction::PublishStarted;
    const bool exec_matches =
        exec.pid == running.pid && exec.app_info_rc == 0 &&
        !exec.title_id.empty() && exec.title_id == running.title_id &&
        exec.app_id == static_cast<uint32_t>(running.app_id);
    decision.reason = exec_matches ? BigAppCollectReason::PublishFromExec
                                   : BigAppCollectReason::PublishFromRunning;
    return decision;
  }

  if (exec_snapshot_usable(exec) && !is_system_app_title(exec.title_id)) {
    decision.action = BigAppCollectAction::PublishStarted;
    decision.reason = BigAppCollectReason::PublishFromExec;
    decision.pid = exec.pid;
    decision.app_id = exec.app_id;
    decision.title_id = exec.title_id;
    return decision;
  }

  decision.pid = exec.pid;
  decision.app_id = exec.app_id;
  decision.title_id = exec.title_id;
  decision.action = BigAppCollectAction::Ignore;
  if (exec.already_tracked) {
    decision.reason = BigAppCollectReason::AlreadyTracked;
  } else if (exec_snapshot_usable(exec) &&
             is_system_app_title(exec.title_id)) {
    decision.reason = BigAppCollectReason::ExecNotRunningBigApp;
  } else if (!running.query_ok) {
    decision.reason = BigAppCollectReason::RunningQueryNotReady;
  } else if (running.pid <= 1) {
    decision.reason = BigAppCollectReason::RunningPidInvalid;
  } else {
    decision.reason = BigAppCollectReason::ExecNotReady;
  }
  return decision;
}

} // namespace onion::lifecycle
