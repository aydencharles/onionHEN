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

#include "ipc.hpp"
#include "cheats/cheat_service.hpp"
#include "util_language.h"
#include "util_toolbox.h"
#include "service_facade.hpp"
#include "pkgserver_adapter.h"
#include <onion/builtin_services.h>
#include <onion/settings.hpp>
#include <onion/log_settings.hpp>
#include <onion/platform.h>
#include <onion/ucred.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
extern "C" {
#include "freebsd-helper.h"
}
#include <cstdint>
#include <hijacker/hijacker.hpp>
#include <sys/_pthreadtypes.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    uint32_t app_type;
    char     title_id[10];
    char     unknown2[0x3c];
} app_info_t;

extern "C" {

    #include "faulthandler.h"
  #include "common_utils.h"
  #include <ps5/payload.h>

  int sceKernelGetAppInfo(pid_t pid, app_info_t * info);
  int sceKernelGetProcessName(int pid, char * out);
  int _sceApplicationGetAppId(int pid, uint32_t * appId);
  // set_proc_authid / get_proc_by_pid: libonion_proc
}

extern bool is_handler_enabled;

onion::SettingsStore g_settings;
static bool g_services_initialized = false;
void start_ip_thread(void);
void* IPC_loop(void* args);
bool shellui_patch(void);

jmp_buf g_catch_buf;
uintptr_t kernel_base = 0;
void* __stack_chk_guard = (void*)0xdeadbeef;

static void cleanup(void) {
    onion_notify(true, "notify.util.crash_recovering");
    exit(1);
}

void __stack_chk_fail(void) {
    LOG_DEBUG("Stack smashing detected.");
}

bool LoadSettings() {
    onion::Settings s{};
    if (!onion::settings_load(&s)) {
        LOG_ERROR("config.ini missing; using defaults (path primary=%s)",
                     onion::kConfigPathPrimary);
    } else {
        LOG_INFO("Loaded settings from %s", onion::settings_last_loaded_path());
    }

    const onion_log_level effective = onion::apply_log_settings(s);
    if (effective != static_cast<onion_log_level>(s.log_level)) {
        LOG_WARN("log level '%s' unavailable in this build; using '%s'",
                     onion_log_level_name(static_cast<onion_log_level>(s.log_level)),
                     onion_log_level_name(effective));
    }

    g_settings.store(s);
    util_apply_ui_language(s.ui_lang);

    if (!g_services_initialized) {
        g_services_initialized = true;
        if (s.shadowmount_autoload &&
            !onion::services::shadowMountService().running() &&
            !onion::services::shadowMountService().start()) {
            LOG_WARN("ShadowMount autoload failed");
        }
        if (s.pkgnet_autoload && !onion::services::pkgNetService().running() &&
            !onion::services::pkgNetService().start()) {
            LOG_WARN("pkg-server autoload failed on TCP %u",
                     static_cast<unsigned>(ONION_PKGNET_PORT));
        }
    }
    /* Re-sync on every settings load (boot and IPC reloads) so a Toolbox
       language change reaches the running pkg-server without a restart;
       pkgnet_start_thread() also syncs on each (re)start. */
    pkg_server_set_webui_lang(util_webui_language_code());
    /* Missing file is not an error — defaults were applied. */
    return true;
}

int main(void) {
    /* Raw elfldr uploads default to "payload.elf"; publish our stable name. */
    (void)syscall(SYS_thr_set_name, -1, "onion_util.elf");

    pthread_t ipc_server = 0;
    
    sceNetCtlInit();
    sceUserServiceInitialize(NULL);
    onion_log_configure(
        "OnionHEN utils", "/data/OnionHEN/OnionHEN_util_daemon.log");
    /* Real linked kernel export (not a dlsym function-pointer variable). */
    onion_notify_set_send(reinterpret_cast<onion_notify_send_fn>(
        sceKernelSendNotificationRequest));
    LOG_INFO("util daemon entered");

    if (setjmp(g_catch_buf) == 0)
        LOG_DEBUG("jump has been set");
    else
        onion_notify(true, "notify.crash.resolved");

    LOG_DEBUG("Registering signal handler...");
    fault_handler_init(cleanup);
    LOG_DEBUG("   Success!");

    payload_args_t* args = payload_get_args();
    kernel_base = args->kdata_base_addr;
    /* Preserve the launch credential context for the SystemService query;
       util keeps PTRACE_AUTHID for its remaining lifetime. */
    (void)util_refresh_system_language();
    /* pt_* / code-cave require PTRACE_AUTHID (not DEBUG_AUTHID). */
    set_ucred_to_ptrace();

    unlink("/data/OnionHEN/OnionHEN_util_crash.log");

    LOG_INFO("=========== starting OnionHEN Utilities... ===========");

    LoadSettings();

    start_ip_thread();
    pthread_create(&ipc_server, NULL, IPC_loop, NULL);
    /* IPC thread is up — publish ready for bootstrapper/daemon consumers */
    onion_ready_signal_pid(ONION_READY_UTIL, getpid());

    if (onion_ready_is_set(ONION_FLAG_UTIL_BOOTED)) {
        /* onion_util.elf restarted mid-session (crash recover / re-launch) — not rest. */
        LOG_WARN("util already booted once — toolbox reinject (not rest)");
        toolbox_reinject();
    }
    /* Mark that util completed cold start (typed flag; replaces util_first_boot file). */
    onion_ready_signal(ONION_FLAG_UTIL_BOOTED);

    LOG_INFO("Initializing cheat engine...");
    onion::cheats::CheatService::instance().ensureDir();

    for (;;) {
        sleep(1);
    }

    return 0;
}
