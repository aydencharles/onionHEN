/* Standalone FPS counter payload.
 *
 * Runs the same sampler code as the OnionHEN daemon (daemon_fps.cpp, compiled
 * unmodified) but without the daemon, util, kstuff stack, settings or ShellUI.
 * Publishes the result to /system_tmp/onionhen/fps_sample (seqlock mmap, see
 * fps_sample.h) so any reader (overlay, plugin, your own tool) can use it.
 *
 * Load on a jailbroken console with an elfldr (e.g. port 9021).
 * It also shows the FPS as a system toast every few seconds (kToastEverySec).
 */
#include <onion/fps_dce.hpp>
#include <onion/fps_formula.hpp>
#include <onion/fps_publish.hpp>
#include <onion/fps_sample.h>
#include <onion/log.h>
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
#include <ps5/payload.h>
}

/* libhijacker reads this global (allproc/rootvnode lookups). The daemon sets
 * it the same way from the payload args. */
extern "C" uint64_t kernel_base;
uint64_t kernel_base = 0;

extern "C" {
int sceKernelGetProcessName(int pid, char *name);
int sceKernelGetAppInfo(pid_t pid, void *info);
int sceSystemServiceGetAppIdOfRunningBigApp();
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);
int sceKernelGetCpuTemperature(int *temp);
int sceKernelGetSocSensorTemperature(int sensor, int *temp);
int sceKernelSendNotificationRequest(int device, void *req, size_t size,
                                     int blocking);
}

/* How often the on-screen FPS toast is shown (seconds). Edit and rebuild. */
static constexpr unsigned kToastEverySec = 5;

struct NotifyRequest {
  char pad[45];
  char message[3075];
};

static void show_toast(const char *text) {
  NotifyRequest req{};
  std::snprintf(req.message, sizeof(req.message), "%s", text);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void debug_toast_for_sample(float fps) {
  char buf[128]{};
  int cpu_temp = 0;
  int gpu_temp = 0;

  if (sceKernelGetCpuTemperature && sceKernelGetCpuTemperature(&cpu_temp) == 0)
    ;
  else
    cpu_temp = -1;

  if (sceKernelGetSocSensorTemperature &&
      sceKernelGetSocSensorTemperature(0, &gpu_temp) == 0)
    ;
  else
    gpu_temp = -1;

  if (cpu_temp >= 0 && gpu_temp >= 0) {
    std::snprintf(buf, sizeof(buf), "FPS: %.0f CPU: %dC GPU: %dC",
                  static_cast<double>(fps), cpu_temp, gpu_temp);
  } else if (cpu_temp >= 0) {
    std::snprintf(buf, sizeof(buf), "FPS: %.0f CPU: %dC",
                  static_cast<double>(fps), cpu_temp);
  } else if (gpu_temp >= 0) {
    std::snprintf(buf, sizeof(buf), "FPS: %.0f GPU: %dC",
                  static_cast<double>(fps), gpu_temp);
  } else {
    std::snprintf(buf, sizeof(buf), "FPS: %.0f", static_cast<double>(fps));
  }

  show_toast(buf);
}

/* Reads the published sample and shows it as a system toast. */
static void *display_thread(void *) {
  bool shown_any = false;
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    OnionFpsSample s;
    if (onion_fps_read(&s) == 0 && s.fps > 0.f) {
      debug_toast_for_sample(s.fps);
      if (!shown_any) {
        LOG_INFO("fps toast active tid=%s fps=%.1f", s.title_id,
                 static_cast<double>(s.fps));
        shown_any = true;
      }
    } else {
      shown_any = false;
    }
    sleep(kToastEverySec);
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


/* ---- Safe mode (default) -------------------------------------------------
 * Only reads the /dev/dce scanout flip counter. No kernel memory access, no
 * DMAP page-table walks, no code patching in the game, and it waits
 * kWarmupSec after a game starts before touching anything. This is the
 * measured display/flip rate, which equals the game's FPS for most titles.
 * We intentionally keep it warmup-free so the payload is usable as soon as a
 * game starts. */
static constexpr unsigned kWarmupSec = 0;
static constexpr useconds_t kSafeSampleUs = 100000; /* 10 Hz */
static constexpr const char *kFullModeFlag = "/data/onion_fps_full";
static constexpr const char *kSafeModeFlag = "/data/onion_fps_safe";

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
  payload_args_t *args = payload_get_args();
  kernel_base = static_cast<uint64_t>(args->kdata_base_addr);

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

  LOG_INFO("standalone fps counter started");

  pthread_t render_thr{}, vsync_thr{}, display_thr{};

  const bool safe_override = access(kSafeModeFlag, F_OK) == 0;
  const bool full = !safe_override && access(kFullModeFlag, F_OK) == 0;

  if (safe_override) {
    LOG_WARN("SAFE mode forced: scanout-only mode enabled. Delete %s to return to safe mode.",
             kSafeModeFlag);
    pthread_create(&render_thr, nullptr, safe_sampler_thread, nullptr);
  } else if (full) {
    LOG_WARN("FULL mode enabled: %s exists. This is the unsafe path that can panic the kernel.",
             kFullModeFlag);
    pthread_create(&render_thr, nullptr, fps_sampler_thread, nullptr);
    pthread_create(&vsync_thr, nullptr, vsync_fps_sampler_thread, nullptr);
  } else {
    LOG_INFO("DEFAULT SAFE MODE: no %s present; using scanout-only sampler.",
             kFullModeFlag);
    pthread_create(&render_thr, nullptr, safe_sampler_thread, nullptr);
  }

  pthread_create(&display_thr, nullptr, display_thread, nullptr);

  pthread_join(render_thr, nullptr);
  if (full)
    pthread_join(vsync_thr, nullptr);
  pthread_join(display_thr, nullptr);
  return 0;
}
