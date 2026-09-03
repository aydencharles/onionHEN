/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// Include files
#include <cstdint>
#include <onion/ucred.h>
#include <onion/net.h>
#include <onion/proc_query.h>
#include <onion/platform.h>
#include "daemon_ops.hpp"
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

// System includes
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/_pthreadtypes.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <ps5/klog.h>

// Project includes
#include "globalconf.hpp"
#include "launcher.hpp"
#include "ipc.hpp"
#include "plugin_ipc_server.hpp"
#include "plugin_manager_runtime.hpp"
#include "startup_navigation.hpp"
#include "welcome_toast.hpp"
#include <onion/debug_settings_route_policy.hpp>
#include <onion/fault_frame.h>
#include <onion/ready.h>

#define MSG_NOSIGNAL 0x20000 /* do not generate SIGPIPE on EOF. */
pthread_t cheat_thr = nullptr;

#define PAD_BUTTON_OPTIONS	0x00000008

// Structure definitions
typedef struct {
    int32_t type;             // 0x00
    int32_t req_id;           // 0x04
    int32_t priority;         // 0x08
    int32_t msg_id;           // 0x0C
    int32_t target_id;        // 0x10
    int32_t user_id;          // 0x14
    int32_t unk1;             // 0x18
    int32_t unk2;             // 0x1C
    int32_t app_id;           // 0x20
    int32_t error_num;        // 0x24
    int32_t unk3;             // 0x28
    char use_icon_image_uri;  // 0x2C
    char message[1024];       // 0x2D
    char uri[1024];           // 0x42D
    char unkstr[1024];        // 0x82D
  } OrbisNotificationRequest; // Size = 0xC30

typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  uint32_t app_type;
  char title_id[10];
  char unknown2[0x3c];
} app_info_t;

// External C declarations
extern "C" {
    int sceKernelSendNotificationRequest(int32_t device,
        OrbisNotificationRequest *req,
        size_t size, int32_t blocking);
    int sceUserServiceGetUserName(const int userId, char *userName, const size_t size);
    uint64_t sceKernelGetProcessTime();
    int sceSystemServiceGetAppId(const char *title_id);
    int sceSystemServiceGetAppIdOfRunningBigApp(void);
    int scePadSetProcessPrivilege(int priv);

    int sceUserServiceGetForegroundUser(int *userId);
    int sceLncUtilLaunchApp(const char *tid, const char *argv[], LncAppParam *param);
    uint32_t _sceApplicationGetAppId(int pid, uint32_t *appId);
    uint32_t sceLncUtilKillApp(uint32_t appId);
    int sceSysmoduleLoadModuleInternal(int id);
    int sceNetCtlInit();
    int sceUserServiceInitialize(const int *);
    int sceKernelDlsym(uint32_t lib, const char *name, void **fun);
    int scePadClose(int handle);
    int sceSystemStateMgrEnterStandby(void);
    int sceKernelMprotect(void *addr, size_t len, int prot);
    ssize_t _read(int, void *, size_t);
    int sceKernelGetProcessName(int pid, char *name);
    int sceKernelGetAppInfo(int pid, app_info_t *info);
    void free(void *);


    // PayloadAPI definitions
    #include <ps5/payload.h>
    
    int sceNotificationSend(int userId, bool isLogged, const char* payload);

}

// Global variables
uint64_t p_syscall = 0;
char _end[1] = {};
int fd = -1;
static constexpr auto DEFAULT_PRIORITY = 256;
uintptr_t kernel_base = 0;

// Function declarations
int launchApp(const char *titleId);
void sig_handler(int signo);
int elfldr_raise_privileges(pid_t pid);
extern void makenewapp();
// fifo_and_dumper_thread / get_ip_address / IPC_loop: daemon_ops.hpp
extern bool is_handler_enabled;

namespace {

void install_crash_handlers() {
  struct sigaction action {};
  action.sa_handler = sig_handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  for (int i = 0; i < 12; i++)
    sigaction(i, &action, nullptr);
}

void start_worker_threads(pthread_t* fifo_thr, pthread_t* msg_thr) {
  pthread_create(fifo_thr, nullptr, fifo_and_dumper_thread, nullptr);
  pthread_create(msg_thr, nullptr, IPC_loop, nullptr);
  if (!onion::daemon::plugin_ipc::start())
    LOG_ERROR("plugin IPC server failed to start");
  pthread_t ctrl_thr = nullptr;
  pthread_create(&ctrl_thr, nullptr, control_tcp_loop, nullptr);
  pthread_detach(ctrl_thr);
  pthread_t supervisor_thr = nullptr;
  pthread_create(&supervisor_thr, nullptr, runtime_supervisor_thread, nullptr);
  pthread_detach(supervisor_thr);
  pthread_t fan_thr = nullptr;
  pthread_create(&fan_thr, nullptr, fan_maintenance_thread, nullptr);
  pthread_detach(fan_thr);
  pthread_t fps_thr = nullptr;
  pthread_create(&fps_thr, nullptr, fps_sampler_thread, nullptr);
  pthread_detach(fps_thr);
  pthread_t vsync_fps_thr = nullptr;
  pthread_create(&vsync_fps_thr, nullptr, vsync_fps_sampler_thread, nullptr);
  pthread_detach(vsync_fps_thr);
  pthread_t language_thr = nullptr;
  pthread_create(&language_thr, nullptr, system_language_poll_thread, nullptr);
  pthread_detach(language_thr);
  pthread_t resume_thr = nullptr;
  pthread_create(&resume_thr, nullptr, resume_recovery_thread, nullptr);
  pthread_detach(resume_thr);
}

/** Keep IPC_loop alive: rejoin + restart on exit. */
[[noreturn]] void ipc_supervisor_loop(pthread_t* msg_thr) {
  while (true) {
    pthread_join(*msg_thr, nullptr);
    pthread_create(msg_thr, nullptr, IPC_loop, nullptr);
    sleep(1);
  }
}

} // namespace

int launchApp(const char *titleId) {
    int id = 0;

    uint32_t res = sceUserServiceGetForegroundUser(&id);
    if (res != 0) {
        LOG_ERROR("sceUserServiceGetForegroundUser failed: 0x%x", res);
        return res;
    }
    LOG_DEBUG("[LA] user id %u", id);

    // the thread will clean this up
    Flag flag = Flag_None;
    LncAppParam param{sizeof(LncAppParam), id, 0, 0, flag};

    LOG_DEBUG("calling sceLncUtilLaunchApp");
    int err = sceLncUtilLaunchApp(titleId, nullptr, &param);
    LOG_DEBUG("sceLncUtilLaunchApp returned 0x%x", (uint32_t)err);
    if (err >= 0) {
        return err;
    }
    
    switch ((uint32_t)err) {
    case SCE_LNC_UTIL_ERROR_ALREADY_RUNNING:
        LOG_WARN("app %s is already running", titleId);
        break;
    case SCE_LNC_ERROR_APP_NOT_FOUND:
        LOG_ERROR("app %s not found", titleId);
        onion_notify(true, "notify.app.not_found", titleId);
        break;
    default:
        LOG_ERROR("[LA] unknown error 0x%x", (uint32_t)err);
        // onion_notify(true, "unknown error 0x%llx", (uint32_t)err);
        break;
    }
    return err;
}

void sig_handler(int signo) {
    if(!is_handler_enabled){
        onion_log_emergency("signal handler disabled, ignoring signal %d", signo);
        return;
    }
    onion_log_emergency("signal %d received; main OnionHEN has crashed", signo);
    onion_print_backtrace(onion_log_emergency);
    onion_notify(true, "notify.crash.main");
    _exit(128 + signo);
}

bool is_800 = false;

int main() {
  /* Raw elfldr uploads default to "payload.elf"; publish our stable name. */
  (void)syscall(SYS_thr_set_name, -1, "onion_daemon.elf");

  onion_log_configure("OnionHEN", "/data/OnionHEN/OnionHEN.log");
  onion_log_configure_crash("/data/OnionHEN/OnionHEN_crash.log");
  /* Real linked kernel export (not a dlsym function-pointer variable). */
  onion_notify_set_send(reinterpret_cast<onion_notify_send_fn>(
      sceKernelSendNotificationRequest));

  char buz[255];
  pthread_t fifo_thr = nullptr;
  pthread_t msg_thr = nullptr;

  sceNetCtlInit();
  sceUserServiceInitialize(&DEFAULT_PRIORITY);
  LOG_DEBUG("daemon entered");

  /* Settings (incl. notify i18n language) before any user-facing toast. */
  LoadSettings();

  OrbisKernelSwVersion sys_ver;
  sceKernelGetProsperoSystemSwVersion(&sys_ver);
  const int fw_ver = (sys_ver.version >> 16);
  const auto debug_settings_route =
      onion::debug_settings_route::DebugSettingsRoutePolicy::for_system_version(
          sys_ver.version);

  install_crash_handlers();

  payload_args_t* args = payload_get_args();
  kernel_base = args->kdata_base_addr;

  LOG_INFO("=========== starting OnionHEN (0x%X) ... ===========", fw_ver);
  (void)sceKernelMprotect(&buz[0], 100, 0x7); // probe mprotect / kstuff state
  const bool toolbox_only = (fw_ver >= 0x10000);
  is_800 = (fw_ver >= 0x800);

  /* Drop any stale FPS-overlay ready flag from older builds/configs. */
  onion_ready_clear(ONION_FLAG_FPS_OVERLAY);

  /* libonion_proc big-app / name lookups used by get_game_pid / inject paths. */
  onion_proc_set_sce_hooks(
      [](int pid, char *name) -> int {
        return sceKernelGetProcessName(pid, name);
      },
      [](pid_t pid, void *info) -> int {
        return sceKernelGetAppInfo(pid, static_cast<app_info_t *>(info));
      },
      []() -> int { return sceSystemServiceGetAppIdOfRunningBigApp(); });

  (void)onion_net_get_ip_address(&buz[0], sizeof(buz));
  start_worker_threads(&fifo_thr, &msg_thr);
  for (int attempt = 0;
       attempt < 20 && !onion::daemon::plugin_ipc::is_listening(); ++attempt)
    usleep(50 * 1000);
  if (onion::daemon::plugin_ipc::is_listening())
    onion::daemon::plugins::start();
  else
    LOG_ERROR("[plugins] startup skipped because plugin IPC is not listening");
  onion_ready_signal_pid(ONION_READY_DAEMON, getpid());

  LOG_DEBUG("is toolbox only: %s | ver: %x", toolbox_only ? "Yes" : "No",
            sys_ver.version);

  /* Toolbox injection is independent from the optional post-load navigation. */
  cmd_enable_toolbox();

  const onion::Settings boot_settings = g_settings.snapshot();

  const std::string welcome_toast_json = onion::daemon::make_welcome_toast_json(
      debug_settings_route.toolbox_uri(
          onion::debug_settings_route::UriKind::Simple));
  sceNotificationSend(0xFE, true, welcome_toast_json.c_str());
  LOG_INFO("StartUp thread created!! - welcome to OnionHEN");

  onion::daemon::apply_startup_destination(boot_settings);

  ipc_supervisor_loop(&msg_thr);
  // unreachable
  return 0;
}
