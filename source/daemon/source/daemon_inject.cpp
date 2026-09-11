/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Toolbox injection for explicit commands and SceShellUI lifecycle recovery.
 */

#include "daemon_ops.hpp"
#include "daemon_power_state.hpp"
#include "globalconf.hpp"
#include "toolbox_injection.hpp"
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/settings.hpp>
#include <ps5/kernel.h>
#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>

extern "C" {
bool Inject_Toolbox(int pid, uint8_t *elf);
extern uint8_t shellui_elf_start[];
}

namespace {

onion::ToolboxInject g_toolbox_inject;
std::atomic<pid_t> g_last_shellui{0};
std::atomic<uint32_t> g_rest_gen{0};

struct RestInjectJob {
  pid_t exec_pid;
  pid_t previous_pid;
  uint32_t gen;
};

pid_t toolbox_live_pid() {
  return static_cast<pid_t>(get_shellui_pid());
}

bool toolbox_already_ready(pid_t pid) {
  return pid > 0 && g_toolbox_inject.is_ready_for(pid);
}

void toolbox_remember_pid(pid_t pid) {
  if (pid > 1) {
    g_last_shellui.store(pid, std::memory_order_release);
  }
}

/*
 * Same gate kstuff uses before touching a just-exec'd NPXS40087: userland
 * libs are mapped.  Each name is 60 x 500ms (ps5-kstuff-ldr/src/shellui_patch.c).
 * Poll is on a known pid inside the rest worker, not a process scanner.
 */
constexpr const char *kShellUiReadySprx[] = {
    "libSceNpTrophy.sprx",
    "libSceNpTrophy2.sprx",
};
constexpr useconds_t kSprxPollUs = 500 * 1000;
constexpr int kSprxPollMax = 60;
constexpr int kRestRetryMax = 4;
constexpr useconds_t kRestRetryDelayUs[] = {
    500 * 1000,
    1000 * 1000,
    2000 * 1000,
};

bool toolbox_sprx_loaded(pid_t pid, const char *name) {
  uint32_t handle = static_cast<uint32_t>(-1);
  return kernel_dynlib_handle(pid, name, &handle) == 0 &&
         handle != static_cast<uint32_t>(-1);
}

bool toolbox_wait_one_sprx(pid_t pid, uint32_t gen, const char *name) {
  for (int i = 0; i < kSprxPollMax; ++i) {
    if (g_rest_gen.load(std::memory_order_acquire) != gen) {
      LOG_DEBUG("rest: sprx wait superseded gen=%u pid=%d name=%s", gen,
                static_cast<int>(pid), name);
      return false;
    }
    if (!isProcessAlive(pid)) {
      LOG_DEBUG("rest: sprx wait pid=%d died at try=%d name=%s",
                static_cast<int>(pid), i, name);
      return false;
    }
    if (toolbox_sprx_loaded(pid, name)) {
      LOG_DEBUG("rest: %s loaded pid=%d after %d ms", name,
                static_cast<int>(pid), i * 500);
      return true;
    }
    usleep(kSprxPollUs);
  }
  LOG_DEBUG("rest: %s still missing pid=%d after %d ms", name,
            static_cast<int>(pid), kSprxPollMax * 500);
  return false;
}

bool toolbox_wait_shellui_sprx(pid_t pid, uint32_t gen) {
  for (const char *name : kShellUiReadySprx) {
    if (!toolbox_wait_one_sprx(pid, gen, name)) {
      return false;
    }
  }
  return true;
}

bool toolbox_wait_resume_delay(pid_t pid, uint32_t gen) {
  uint64_t delay = g_settings.snapshot().rest_mode_delay_seconds;
  if (delay > static_cast<uint64_t>(onion::kRestModeDelayMaxSeconds))
    delay = onion::kRestModeDelayMaxSeconds;
  if (delay == 0)
    return true;

  LOG_DEBUG("rest: resume delay %llu s after trophy sprx pid=%d",
            static_cast<unsigned long long>(delay), static_cast<int>(pid));
  for (uint64_t i = 0; i < delay; ++i) {
    if (g_rest_gen.load(std::memory_order_acquire) != gen) {
      LOG_DEBUG("rest: resume delay superseded gen=%u pid=%d", gen,
                static_cast<int>(pid));
      return false;
    }
    if (!isProcessAlive(pid)) {
      LOG_DEBUG("rest: resume delay pid=%d died at %llu/%llu s",
                static_cast<int>(pid), static_cast<unsigned long long>(i),
                static_cast<unsigned long long>(delay));
      return false;
    }
    sleep(1);
  }
  return g_rest_gen.load(std::memory_order_acquire) == gen &&
         isProcessAlive(pid);
}

void toolbox_wait_kstuff() {
  if (!onion_ready_wait(ONION_READY_KSTUFF, /*timeout_ms=*/5000,
                        /*poll_ms=*/200))
    sleep(1);
}

void toolbox_notify_outcome(const onion::ToolboxInjectionOutcome &outcome) {
  switch (outcome.result) {
  case onion::ToolboxInjectionResult::AlreadyReady:
    LOG_WARN("Toolbox already active in SceShellUI pid=%d",
             static_cast<int>(outcome.pid));
    LOG_DEBUG("rest: skip inject, ready marker matches pid=%d",
              static_cast<int>(outcome.pid));
    break;
  case onion::ToolboxInjectionResult::Injected:
    LOG_INFO("Toolbox online for SceShellUI pid=%d (ready protocol)",
             static_cast<int>(outcome.pid));
    LOG_DEBUG("rest: ensure Injected pid=%d", static_cast<int>(outcome.pid));
    break;
  case onion::ToolboxInjectionResult::TargetNotFound:
    LOG_DEBUG("rest: ensure TargetNotFound");
    onion_notify(true, "notify.toolbox.no_shellui_pid");
    break;
  case onion::ToolboxInjectionResult::InjectFailed:
    LOG_DEBUG("rest: ensure InjectFailed pid=%d",
              static_cast<int>(outcome.pid));
    onion_notify(true, "notify.toolbox.inject_failed");
    break;
  case onion::ToolboxInjectionResult::ReadyTimeout:
    LOG_DEBUG("rest: ensure ReadyTimeout pid=%d",
              static_cast<int>(outcome.pid));
    onion_notify(true, "notify.toolbox.load_timeout");
    break;
  case onion::ToolboxInjectionResult::TargetChanged:
    LOG_DEBUG("rest: ensure TargetChanged pid=%d",
              static_cast<int>(outcome.pid));
    onion_notify(true, "notify.toolbox.shellui_restarted");
    break;
  }
}

bool toolbox_inject_immediate(pid_t expected_pid = 0) {
  const pid_t observed_pid = expected_pid > 1 ? expected_pid : toolbox_live_pid();
  LOG_DEBUG("rest: cmd_enable_toolbox shellui_pid=%d",
            static_cast<int>(observed_pid));
  if (toolbox_already_ready(observed_pid)) {
    toolbox_remember_pid(observed_pid);
    toolbox_notify_outcome({onion::ToolboxInjectionResult::AlreadyReady,
                            observed_pid});
    return true;
  }

  toolbox_wait_kstuff();
  LOG_INFO("Activating toolbox...");

  const onion::ToolboxInjectionOutcome outcome = g_toolbox_inject.inject(
      []() -> pid_t { return toolbox_live_pid(); },
      [](pid_t pid) -> bool {
        LOG_DEBUG("Injecting toolbox into SceShellUI pid=%d",
                  static_cast<int>(pid));
        return Inject_Toolbox(static_cast<int>(pid), shellui_elf_start);
      },
      /*timeout_ms=*/45 * 1000, /*poll_ms=*/250, expected_pid);

  toolbox_remember_pid(outcome.pid);
  toolbox_notify_outcome(outcome);
  return outcome.ready();
}

bool toolbox_inject_rest(pid_t exec_pid, pid_t previous_pid, uint32_t gen,
                         bool apply_user_delay) {
  LOG_DEBUG("rest: SysCore EXEC shellui pid=%d previous=%d gen=%u",
            static_cast<int>(exec_pid), static_cast<int>(previous_pid), gen);
  const bool replacement = previous_pid > 1 && previous_pid != exec_pid;
  LOG_DEBUG("rest: shellui pid %d -> %d, wait trophy sprx",
            static_cast<int>(previous_pid), static_cast<int>(exec_pid));
  if (!toolbox_wait_shellui_sprx(exec_pid, gen)) {
    LOG_DEBUG("rest: skip inject, pid=%d not ready for ELF load",
              static_cast<int>(exec_pid));
    return false;
  }
  if (apply_user_delay && !toolbox_wait_resume_delay(exec_pid, gen)) {
    LOG_DEBUG("rest: skip inject after resume delay pid=%d",
              static_cast<int>(exec_pid));
    return false;
  }
  if (g_rest_gen.load(std::memory_order_acquire) != gen) {
    LOG_DEBUG("rest: inject superseded after sprx wait gen=%u pid=%d", gen,
              static_cast<int>(exec_pid));
    return false;
  }
  if (replacement) {
    onion_notify(true, "notify.rest.reactivating");
  }
  return toolbox_inject_immediate(exec_pid);
}

void *toolbox_rest_worker(void *arg) {
  RestInjectJob job = *static_cast<RestInjectJob *>(arg);
  delete static_cast<RestInjectJob *>(arg);
  for (int attempt = 0; attempt < kRestRetryMax; ++attempt) {
    if (g_rest_gen.load(std::memory_order_acquire) != job.gen) {
      LOG_DEBUG("rest: inject retry superseded gen=%u", job.gen);
      break;
    }
    pid_t target = job.exec_pid;
    if (target <= 1) {
      for (int resolve_try = 0; resolve_try < 30 && target <= 1;
           ++resolve_try) {
        target = toolbox_live_pid();
        if (target <= 1) {
          usleep(1000 * 1000);
        }
      }
    }
    if (target > 1 &&
        toolbox_inject_rest(target, job.previous_pid, job.gen,
                            /*apply_user_delay=*/attempt == 0)) {
      toolbox_remember_pid(target);
      return nullptr;
    }
    if (attempt + 1 < kRestRetryMax) {
      LOG_DEBUG("rest: inject retry=%d/%d target=%d", attempt + 1,
                kRestRetryMax - 1, static_cast<int>(target));
      usleep(kRestRetryDelayUs[attempt]);
    }
  }
  LOG_WARN("rest: toolbox recovery exhausted gen=%u requested_pid=%d", job.gen,
           static_cast<int>(job.exec_pid));
  return nullptr;
}

void start_rest_inject(pid_t exec_pid) {
  if (exec_pid > 1 && daemon_power_state_is_sleeping()) {
    toolbox_remember_pid(exec_pid);
    LOG_INFO("rest: defer ShellUI injection during standby pid=%d",
             static_cast<int>(exec_pid));
    return;
  }
  if (exec_pid <= 1 && toolbox_live_pid() <= 1) {
    LOG_DEBUG("rest: no SceShellUI pid yet; compensation worker will retry");
  }
  const pid_t previous = exec_pid > 1
                             ? g_last_shellui.exchange(exec_pid,
                                                       std::memory_order_acq_rel)
                             : g_last_shellui.load(std::memory_order_acquire);
  const uint32_t gen =
      g_rest_gen.fetch_add(1, std::memory_order_acq_rel) + 1;
  auto *job = new RestInjectJob{exec_pid, previous, gen};
  pthread_t thread = nullptr;
  if (pthread_create(&thread, nullptr, toolbox_rest_worker, job) != 0) {
    LOG_ERROR("rest: pthread_create for shellui exec pid=%d failed",
              static_cast<int>(exec_pid));
    delete job;
    (void)toolbox_inject_rest(exec_pid, previous, gen,
                              /*apply_user_delay=*/true);
    return;
  }
  pthread_detach(thread);
}

} // namespace

void toolbox_on_new_shellui(pid_t pid) {
  /* NOTE_EXEC denotes a new image, even when the kernel reuses the PID. */
  onion_ready_clear(ONION_READY_TOOLBOX);
  if (daemon_power_state_is_sleeping()) {
    toolbox_remember_pid(pid);
    LOG_INFO("rest: NOTE_EXEC during standby; defer ShellUI injection pid=%d",
             static_cast<int>(pid));
    return;
  }
  start_rest_inject(pid);
}

void toolbox_on_resume() {
  if (daemon_power_state_is_sleeping()) {
    LOG_DEBUG("rest: resume compensation still in standby; defer");
    return;
  }
  const pid_t pid = toolbox_live_pid();
  if (pid <= 1) {
    start_rest_inject(0);
    return;
  }
  if (toolbox_already_ready(pid)) {
    toolbox_remember_pid(pid);
    LOG_DEBUG("rest: resume compensation sees ready toolbox pid=%d",
              static_cast<int>(pid));
    return;
  }
  LOG_INFO("rest: resume compensation scheduling ShellUI pid=%d",
           static_cast<int>(pid));
  start_rest_inject(pid);
}

bool cmd_enable_toolbox() {
  return toolbox_inject_immediate();
}
