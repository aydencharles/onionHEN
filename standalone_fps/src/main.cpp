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
#include <onion/fps_sample.h>
#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

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

/* Reads the published sample and shows it as a system toast. */
static void *display_thread(void *) {
  bool shown_any = false;
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    OnionFpsSample s;
    if (onion_fps_read(&s) == 0 && s.fps > 0.f) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "FPS: %.0f", static_cast<double>(s.fps));
      show_toast(buf);
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
  pthread_create(&render_thr, nullptr, fps_sampler_thread, nullptr);
  pthread_create(&vsync_thr, nullptr, vsync_fps_sampler_thread, nullptr);

  pthread_create(&display_thr, nullptr, display_thread, nullptr);

  pthread_join(render_thr, nullptr);
  pthread_join(vsync_thr, nullptr);
  pthread_join(display_thr, nullptr);
  return 0;
}
