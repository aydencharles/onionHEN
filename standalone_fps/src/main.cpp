/* Standalone FPS counter payload.
 *
 * Runs the same sampler code as the OnionHEN daemon (daemon_fps.cpp, compiled
 * unmodified) but without the daemon, util, kstuff stack, settings or ShellUI.
 * Publishes the result to /system_tmp/onionhen/fps_sample (seqlock mmap, see
 * fps_sample.h) so any reader (overlay, plugin, your own tool) can use it.
 *
 * Load on a jailbroken console with an elfldr (e.g. port 9021).
 * Optional: pass "--stdout" semantics are not needed; values go to klog.
 */
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
int sceKernelGetProcessName(int pid, char *name);
int sceKernelGetAppInfo(pid_t pid, void *info);
int sceSystemServiceGetAppIdOfRunningBigApp();
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);
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

  pthread_t render_thr{}, vsync_thr{};
  pthread_create(&render_thr, nullptr, fps_sampler_thread, nullptr);
  pthread_create(&vsync_thr, nullptr, vsync_fps_sampler_thread, nullptr);

  pthread_join(render_thr, nullptr);
  pthread_join(vsync_thr, nullptr);
  return 0;
}
