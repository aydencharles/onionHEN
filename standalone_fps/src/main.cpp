/* Standalone FPS counter + on-screen overlay payload.
 *
 * Samples the scanout (flip) counter from /dev/dce (safe mode: no game or
 * kernel memory access), publishes the result to
 * /system_tmp/onionhen/fps_sample (seqlock mmap, see fps_sample.h) and injects
 * a small ShellUI module (the embedded fps_overlay ELF) into SceShellUI that
 * draws FPS / CPU / GPU as stacked rows on screen.
 *
 * Load on a jailbroken console with an elfldr (e.g. port 9021).
 */
#include <onion/fps_dce.hpp>
#include <onion/fps_formula.hpp>
#include <onion/fps_publish.hpp>
#include <onion/fps_sample.h>
#include <onion/log.h>
#include <onion/ready.h>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include <ctime>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "daemon_ops.hpp"
#include "globalconf.hpp"

extern "C" {
int sceKernelGetProcessName(int pid, char *name);
int sceKernelGetAppInfo(pid_t pid, void *info);
int sceSystemServiceGetAppIdOfRunningBigApp();
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);

/* libNineS: ptrace-injects an ELF into the process `pid`. */
bool Inject_Toolbox(int pid, uint8_t *elf);

/* Embedded fps_overlay ELF (see embedded_overlay.c). */
extern uint8_t fps_overlay_elf_start[];
}

/* Keeps the overlay injected into SceShellUI. Re-injects after a ShellUI
 * restart (new pid, e.g. Rest Mode resume). */
static void *injector_thread(void *) {
  pid_t injected_pid = -1;
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const pid_t pid = onion_find_pid("SceShellUI");
    if (pid > 0 && pid != injected_pid &&
        !onion_ready_matches_pid(ONION_READY_TOOLBOX, pid)) {
      LOG_INFO("injecting fps overlay into SceShellUI pid=%d",
               static_cast<int>(pid));
      if (Inject_Toolbox(static_cast<int>(pid), fps_overlay_elf_start) &&
          onion_ready_wait_pid(ONION_READY_TOOLBOX, pid, 45000, 250)) {
        injected_pid = pid;
        LOG_INFO("fps overlay ready (pid=%d)", static_cast<int>(pid));
      } else {
        LOG_ERROR("fps overlay injection failed (pid=%d), retrying",
                  static_cast<int>(pid));
        sleep(10);
      }
    } else if (pid > 0 && onion_ready_matches_pid(ONION_READY_TOOLBOX, pid)) {
      injected_pid = pid;
    }
    sleep(3);
  }
  return nullptr;
}

std::atomic_bool g_stack_shutting_down{false};
onion::SettingsStore g_settings;

bool Get_Running_App_TID(std::string &title_id, int &BigAppid) {
  char tid[255];
  BigAppid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (BigAppid < 0)
    return false;
  std::memset(tid, 0, sizeof tid);
  if (sceSystemServiceGetAppTitleId(BigAppid, tid) != 0)
    return false;
  title_id = tid;
  return true;
}

/* ---- Safe mode only --------------------------------------------------------
 * The full render counter / PS4 BC hook path is intentionally removed. The
 * standalone FPS payload must stay in the scanout-only path so it cannot touch
 * game memory, kernel memory, or PS4 compatibility hooks. */
static constexpr unsigned kWarmupSec = 0;
static constexpr useconds_t kSafeSampleUs = 100000; /* 10 Hz */

static double mono_sec() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) +
         static_cast<double>(ts.tv_nsec) * 1e-9;
}

static void publish_sample(bool valid, const char *tid, float fps) {
  OnionFpsSample s{};
  s.pid = valid ? 1 : -1;
  s.valid = valid ? 1 : 0;
  s.source = ONION_FPS_SRC_SCANOUT;
  s.fps = valid ? fps : 0.f;
  s.unix_ns = onion_fps_realtime_ns();
  if (tid)
    std::strncpy(s.title_id, tid, sizeof(s.title_id) - 1);
  onion::fps::publish(s);
}

static void *safe_sampler_thread(void *) {
  onion::fps::DceSource dce;
  std::string cached_tid;
  double started = 0.0;
  bool have = false;
  uint64_t last_count = 0;
  double last_t = 0.0;
  float window[onion::fps::kWindow]{};
  int wn = 0, wi = 0;
  (void)onion::fps::publish_open();
  LOG_INFO("safe fps sampler started (scanout only, warmup %us)", kWarmupSec);

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    std::string tid;
    int app_id = 0;
    if (!Get_Running_App_TID(tid, app_id)) {
      cached_tid.clear();
      have = false;
      wn = wi = 0;
      publish_sample(false, nullptr, 0.f);
      sleep(1);
      continue;
    }
    if (tid != cached_tid) {
      cached_tid = tid;
      started = mono_sec();
      have = false;
      wn = wi = 0;
      LOG_INFO("safe fps: game %s detected; sampling immediately", tid.c_str());
    }
    if (mono_sec() - started < kWarmupSec) {
      publish_sample(false, tid.c_str(), 0.f);
      sleep(1);
      continue;
    }

    uint64_t count = 0;
    if (dce.sample(&count)) {
      const double now = mono_sec();
      if (have && count > last_count && now > last_t) {
        const double hz =
            onion::fps::hz_from_delta(count - last_count, now - last_t);
        if (hz > 0.0) {
          window[wi] = static_cast<float>(hz);
          wi = (wi + 1) % onion::fps::kWindow;
          if (wn < onion::fps::kWindow)
            ++wn;
          publish_sample(true, tid.c_str(),
                         onion::fps::rolling_mean(window, wn));
        }
      }
      last_count = count;
      last_t = now;
      have = true;
    } else if (dce.unavailable()) {
      LOG_ERROR("safe fps: /dev/dce unavailable, giving up");
      publish_sample(false, tid.c_str(), 0.f);
      sleep(5);
    }
    usleep(kSafeSampleUs);
  }
  return nullptr;
}

static void on_term(int) {
  g_stack_shutting_down.store(true, std::memory_order_release);
}

int main() {
  mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  onion_log_configure("FPS", "/data/onion_fps.log");
  onion_log_set_level(ONION_LOG_INFO);
  signal(SIGTERM, on_term);
  signal(SIGINT, on_term);

  /* Needed by onion_find_pid_ex(..., for_bigapp=true). */
  onion_proc_set_sce_hooks(
      [](int pid, char *name) -> int {
        return sceKernelGetProcessName(pid, name);
      },
      [](pid_t pid, void *info) -> int {
        return sceKernelGetAppInfo(pid, info);
      },
      []() -> int { return sceSystemServiceGetAppIdOfRunningBigApp(); });

  LOG_INFO("standalone fps counter started (safe scanout-only mode + overlay)");

  pthread_t render_thr{}, inject_thr{};
  pthread_create(&render_thr, nullptr, safe_sampler_thread, nullptr);
  pthread_create(&inject_thr, nullptr, injector_thread, nullptr);

  pthread_join(render_thr, nullptr);
  pthread_join(inject_thr, nullptr);
  return 0;
}
