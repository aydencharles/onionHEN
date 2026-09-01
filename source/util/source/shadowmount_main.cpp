/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * In-process adapter for the vendored ShadowMount+ module. Mirrors the
 * startup/shutdown sequence of the upstream standalone main() while leaving
 * process-level concerns to util: signals become
 * shadowmount_module_request_stop(), the single-instance scanner belongs to
 * the service facade, and SceUserService lifetime stays with util's main().
 */

#include "shadowmount_service.h"

#include <onion/log.h>
#include <onion/proc_query.h>

extern "C" {
#include "sm_appdb.h"
#include "sm_config_mount.h"
#include "sm_filesystem.h"
#include "sm_game_lifecycle.h"
#include "sm_image.h"
#include "sm_kstuff.h"
#include "sm_limits.h"
#include "sm_log.h"
#include "sm_mdbg.h"
#include "sm_mount_device.h"
#include "sm_path_utils.h"
#include "sm_paths.h"
#include "sm_platform.h"
#include "sm_runtime.h"
#include "sm_scan.h"
#include "sm_scanner.h"
#include "sm_shellcore_flags.h"
#include "sm_time.h"
#include "sm_types.h"
}

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

#ifndef SHADOWMOUNT_VERSION
#define SHADOWMOUNT_VERSION "unknown"
#endif
#ifndef ONIONHEN_SHADOWMOUNT_TEMPLATE
#define ONIONHEN_SHADOWMOUNT_TEMPLATE "config.ini.example"
#endif
#ifndef ONIONHEN_SHADOWMOUNT_ICON
#define ONIONHEN_SHADOWMOUNT_ICON "smp_icon.png"
#endif

/* The upstream Makefile bakes these assets with xxd. Embed the pinned icon so
 * rich notifications keep their artwork without an extra build step. */
extern "C" const unsigned char smp_icon_png[];
extern "C" const unsigned int smp_icon_png_len;
__asm__(".section .rodata\n"
        ".global smp_icon_png\n"
        ".type smp_icon_png, @object\n"
        ".align 16\n"
        "smp_icon_png:\n"
        ".incbin \"" ONIONHEN_SHADOWMOUNT_ICON "\"\n"
        "smp_icon_png_end:\n"
        ".global smp_icon_png_len\n"
        ".type smp_icon_png_len, @object\n"
        ".align 4\n"
        "smp_icon_png_len:\n"
        ".int smp_icon_png_end - smp_icon_png\n");

/* sm_mdbg.c references this kernel NID directly. Older libkernel_sys stubs do
 * not carry it, so a weak alias keeps the link working; the real kernel export
 * overrides it at load time. Returning 0 merely disables the optional mdbg
 * crash-log monitor. */
__asm__(".weak C49jelxiaVE\n"
        ".set C49jelxiaVE, onion_sm_dbg_log_buffer_size_stub\n");
extern "C" uint64_t onion_sm_dbg_log_buffer_size_stub(void);
uint64_t onion_sm_dbg_log_buffer_size_stub(void) { return 0; }

/* ---- Runtime state lifted out of upstream main.c ------------------------
 * The sm_*.c modules call these entry points; upstream defined them next to
 * main(). Signals are replaced by request_shutdown_stop(), which the facade
 * calls from the util side to end the scanner loop cleanly. */
namespace {

volatile sig_atomic_t g_sm_stop_requested = 0;
std::atomic<bool> g_sm_shutdown_on_going{false};
std::atomic<bool> g_sm_sleep_mode_active{false};
std::atomic<const char *> g_sm_stop_reason{nullptr};
std::atomic<uint64_t> g_sm_next_stop_file_poll_us{0};
pthread_mutex_t g_sm_mount_state_mutex = PTHREAD_MUTEX_INITIALIZER;

constexpr uint64_t kSmStopFilePollIntervalUs = 3000000ull;

pthread_mutex_t g_sm_scan_now_mutex = PTHREAD_MUTEX_INITIALIZER;
char g_sm_scan_now_reason[128] = {0};

} // namespace

extern "C" bool should_stop_requested(void) {
  if (g_sm_stop_requested)
    return true;

  const uint64_t now_us = monotonic_time_us();
  if (now_us != 0) {
    const uint64_t next_poll_us =
        g_sm_next_stop_file_poll_us.load(std::memory_order_acquire);
    if (next_poll_us != 0 && now_us < next_poll_us)
      return false;
    g_sm_next_stop_file_poll_us.store(now_us + kSmStopFilePollIntervalUs,
                                      std::memory_order_release);
  }

  if (remove(KILL_FILE) == 0) {
    g_sm_stop_requested = 1;
    return true;
  }
  return false;
}

extern "C" void request_shutdown_stop(const char *reason) {
  static char g_sm_shutdown_reason[128];
  const char *resolved =
      (reason && reason[0] != '\0') ? reason : "unknown shutdown source";
  bool already = g_sm_shutdown_on_going.exchange(true, std::memory_order_acq_rel);
  if (!already) {
    (void)strlcpy(g_sm_shutdown_reason, resolved,
                  sizeof(g_sm_shutdown_reason));
    g_sm_stop_reason.store(g_sm_shutdown_reason, std::memory_order_release);
    log_debug("[SHUTDOWN] requested by %s", g_sm_shutdown_reason);
  }
  g_sm_stop_requested = 1;
  sm_scanner_wake();
  wake_game_lifecycle_watcher();
}

extern "C" bool runtime_sleep_mode_active(void) {
  return g_sm_sleep_mode_active.load(std::memory_order_acquire);
}

static void sm_clear_scan_now_request(void) {
  pthread_mutex_lock(&g_sm_scan_now_mutex);
  g_sm_scan_now_reason[0] = '\0';
  pthread_mutex_unlock(&g_sm_scan_now_mutex);
}

extern "C" bool request_runtime_sleep_mode(bool active, const char *reason) {
  bool previous =
      g_sm_sleep_mode_active.exchange(active, std::memory_order_acq_rel);
  if (previous == active)
    return false;

  if (active)
    sm_clear_scan_now_request();

  log_debug("[SLEEP] %s by %s", active ? "entered" : "left",
            (reason && reason[0] != '\0') ? reason : "unknown sleep source");
  sm_scanner_wake();
  wake_game_lifecycle_watcher();
  return true;
}

extern "C" void runtime_mount_state_lock(void) {
  pthread_mutex_lock(&g_sm_mount_state_mutex);
}

extern "C" void runtime_mount_state_unlock(void) {
  pthread_mutex_unlock(&g_sm_mount_state_mutex);
}

extern "C" void request_scan_now(const char *reason) {
  const char *resolved =
      (reason && reason[0] != '\0') ? reason : "unknown scan source";
  const bool resume_scan =
      strcmp(resolved, "SceSystemStateMgrInfo=WORKING") == 0;
  if (runtime_sleep_mode_active() && !resume_scan)
    return;

  pthread_mutex_lock(&g_sm_scan_now_mutex);
  if (g_sm_scan_now_reason[0] == '\0') {
    (void)strlcpy(g_sm_scan_now_reason, resolved,
                  sizeof(g_sm_scan_now_reason));
    log_debug("[SCAN] immediate scan requested by %s", g_sm_scan_now_reason);
  }
  pthread_mutex_unlock(&g_sm_scan_now_mutex);
  sm_scanner_wake();
}

extern "C" bool consume_scan_now_request(char *reason_out,
                                         size_t reason_out_size) {
  if (reason_out && reason_out_size > 0)
    reason_out[0] = '\0';
  pthread_mutex_lock(&g_sm_scan_now_mutex);
  if (g_sm_scan_now_reason[0] == '\0') {
    pthread_mutex_unlock(&g_sm_scan_now_mutex);
    return false;
  }
  if (reason_out && reason_out_size > 0)
    (void)strlcpy(reason_out, g_sm_scan_now_reason, reason_out_size);
  g_sm_scan_now_reason[0] = '\0';
  pthread_mutex_unlock(&g_sm_scan_now_mutex);
  return true;
}

extern "C" bool sleep_with_stop_check(unsigned int total_us) {
  const unsigned int chunk_us = 200000;
  unsigned int slept = 0;
  while (slept < total_us) {
    if (should_stop_requested())
      return true;
    unsigned int remain = total_us - slept;
    unsigned int step = remain < chunk_us ? remain : chunk_us;
    sceKernelUsleep(step);
    slept += step;
  }
  return should_stop_requested();
}

/* Process lookup used by the game lifecycle watcher; mirrors upstream's
 * sysctl walk over kinfo_proc entries. */
#define SM_KINFO_PID_OFFSET 72
#define SM_KINFO_TDNAME_OFFSET 447

extern "C" pid_t find_pid_by_name(const char *name, bool exclude_self) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0)
    return -1;
  if (buf_size == 0)
    return 0;

  uint8_t *buf = static_cast<uint8_t *>(malloc(buf_size));
  if (!buf)
    return -1;

  if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
    free(buf);
    return -1;
  }

  const pid_t mypid = exclude_self ? getpid() : -1;
  pid_t found_pid = 0;
  uint8_t *ptr = buf;
  const uint8_t *end = buf + buf_size;
  while (ptr < end) {
    const int ki_structsize = *reinterpret_cast<int *>(ptr);
    const pid_t ki_pid = *reinterpret_cast<pid_t *>(ptr + SM_KINFO_PID_OFFSET);
    const char *ki_tdname =
        reinterpret_cast<const char *>(ptr + SM_KINFO_TDNAME_OFFSET);
    ptr += ki_structsize;
    if ((!exclude_self || ki_pid != mypid) && strcmp(ki_tdname, name) == 0) {
      found_pid = ki_pid;
      break;
    }
  }

  free(buf);
  return found_pid;
}

/* The default config template is embedded so the first run can create the
 * file without shipping a separate asset. */
extern "C" const unsigned char onion_shadowmount_config_ini[];
extern "C" const unsigned int onion_shadowmount_config_ini_len;
__asm__(".section .rodata\n"
        ".global onion_shadowmount_config_ini\n"
        ".type onion_shadowmount_config_ini, @object\n"
        ".align 16\n"
        "onion_shadowmount_config_ini:\n"
        ".incbin \"" ONIONHEN_SHADOWMOUNT_TEMPLATE "\"\n"
        "onion_shadowmount_config_ini_end:\n"
        ".global onion_shadowmount_config_ini_len\n"
        ".type onion_shadowmount_config_ini_len, @object\n"
        ".align 4\n"
        "onion_shadowmount_config_ini_len:\n"
        ".int onion_shadowmount_config_ini_end - onion_shadowmount_config_ini\n");

namespace {

void get_firmware_version_string(char out[32]) {
  uint32_t fw = kernel_get_fw_version();
  uint32_t major_bcd = (fw >> 24) & 0xFFu;
  uint32_t minor_bcd = (fw >> 16) & 0xFFu;
  uint32_t major = ((major_bcd >> 4) & 0xFu) * 10u + (major_bcd & 0xFu);
  uint32_t minor = ((minor_bcd >> 4) & 0xFu) * 10u + (minor_bcd & 0xFu);

  if (major == 0 && minor == 0) {
    (void)snprintf(out, 32, "unknown");
    return;
  }
  (void)snprintf(out, 32, "%u.%02u", major, minor);
}

bool write_buffer_to_fd(int fd, const unsigned char *buf, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    ssize_t written = write(fd, buf + offset, size - offset);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0) {
      errno = EIO;
      return false;
    }
    offset += (size_t)written;
  }
  return true;
}

void ensure_runtime_config_file(void) {
  int fd = open(CONFIG_FILE, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0) {
    if (errno != EEXIST)
      LOG_ERROR("[SMP] failed to create %s: %s", CONFIG_FILE, strerror(errno));
    return;
  }

  size_t template_size = (size_t)onion_shadowmount_config_ini_len;
  int saved_errno = 0;
  if (!write_buffer_to_fd(fd, onion_shadowmount_config_ini, template_size))
    saved_errno = errno;
  if (close(fd) != 0 && saved_errno == 0)
    saved_errno = errno;

  if (saved_errno != 0) {
    LOG_ERROR("[SMP] failed to write %s: %s", CONFIG_FILE,
              strerror(saved_errno));
    (void)unlink(CONFIG_FILE);
    return;
  }
  LOG_INFO("[SMP] created default config from template: %s", CONFIG_FILE);
}

void ensure_kstuff_noautomount_file(void) {
  if (path_exists(KSTUFF_NOAUTOMOUNT_FILE))
    return;

  int fd = open(KSTUFF_NOAUTOMOUNT_FILE, O_RDONLY | O_CREAT, 0666);
  if (fd >= 0) {
    close(fd);
    LOG_DEBUG("[SMP] created startup sentinel: %s", KSTUFF_NOAUTOMOUNT_FILE);
    return;
  }
  LOG_DEBUG("[SMP] failed to create %s: %s", KSTUFF_NOAUTOMOUNT_FILE,
            strerror(errno));
}

void cleanup_kstuff_noautomount_files(void) {
  if (unlink(KSTUFF_NOAUTOMOUNT_FILE) == 0)
    log_debug("[SMP] removed shutdown sentinel: %s", KSTUFF_NOAUTOMOUNT_FILE);
  else if (errno != ENOENT)
    log_debug("[SMP] failed to remove %s: %s", KSTUFF_NOAUTOMOUNT_FILE,
              strerror(errno));
}

/* Upstream kills conflicting backpork fakelib processes by name before the
 * scanner starts. Reuse OnionHEN's sysctl-based lookup instead of upstream's
 * private sysctl walker. */
void stop_conflicting_backpork(void) {
  if (!runtime_config()->backport_fakelib_enabled)
    return;

  static const char *const names[] = {"backpork.elf", "ps5-backpork.elf"};
  for (const char *name : names) {
    while (true) {
      pid_t pid = onion_find_pid_substr(name);
      if (pid <= 0)
        break;
      if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
        log_debug("  [FAKELIB] failed to stop %s pid=%ld: %s", name,
                  (long)pid, strerror(errno));
        break;
      }
      log_debug("  [FAKELIB] stopped conflicting %s pid=%ld", name,
                (long)pid);
      sceKernelUsleep(100000);
    }
  }
}

void log_non_empty_scan_paths(void) {
  for (int i = 0; i < get_scan_path_count(); i++) {
    const char *scan_path = get_scan_path(i);
    DIR *d = opendir(scan_path);
    if (!d)
      continue;

    bool non_empty = false;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
      if ((entry->d_name[0] == '.' && entry->d_name[1] == '\0') ||
          (entry->d_name[0] == '.' && entry->d_name[1] == '.' &&
           entry->d_name[2] == '\0')) {
        continue;
      }
      non_empty = true;
      break;
    }
    closedir(d);

    if (non_empty)
      log_fs_stats("SCAN", scan_path, NULL);
  }
}

void ensure_app_inst_util(void) {
  static bool done = false;
  if (done)
    return;
  done = true;
  if (sceAppInstUtilInitialize() != 0)
    LOG_WARN("[SMP] sceAppInstUtilInitialize failed; title queries degrade");
}

/* Startup steps that lead into the blocking scanner loop. */
void run_scanner_until_stop(void) {
  if (!wait_for_lvd_release()) {
    log_debug("[SHUTDOWN] stop requested while waiting /dev/lvd2 release");
    return;
  }

  log_debug("[STARTUP] cleanup_staged_mount_links begin");
  cleanup_staged_mount_links();
  log_debug("[STARTUP] cleanup_duplicate_title_mounts begin");
  cleanup_duplicate_title_mounts();
  if (!app_db_run_startup_maintenance())
    log_debug("  [DB] startup snd0info maintenance unavailable");

  log_debug("[STARTUP] scanner startup sync begin");
  if (!sm_scanner_run_startup_sync()) {
    log_debug("[STARTUP] scanner startup sync aborted");
    return;
  }
  log_debug("[STARTUP] scanner startup sync done");
  sm_scanner_run_loop();
}

void shadowmount_shutdown_sequence(void) {
  sm_shellcore_flags_stop();
  stop_game_lifecycle_watcher();
  sm_scanner_shutdown();
  sm_kstuff_shutdown();
  sm_mdbg_shutdown();
  cleanup_kstuff_noautomount_files();
  shutdown_title_mounts();
  if (!shutdown_image_mounts())
    log_debug(
        "[SHUTDOWN] some image mounts or devices were not fully released");
  shutdown_app_db();
  sm_log_shutdown();
}

} // namespace

namespace onion::services {

int shadowmount_module_main() {
  ensure_app_inst_util();

  mkdir(LOG_DIR, 0777);
  ensure_runtime_config_file();
  ensure_kstuff_noautomount_file();

  (void)unlink(LOG_FILE_PREV);
  (void)rename(LOG_FILE, LOG_FILE_PREV);

  if (!sm_scanner_init()) {
    log_debug("  [SCAN] scanner service init incomplete; steady-state scanner "
              "will stop if initialization cannot be completed");
  }

  char firmware_version[32];
  get_firmware_version_string(firmware_version);
  log_debug("ShadowMount+ v%s exFAT/UFS/PFS/LVD/MD. FW: %s. Build: %s %s. Thx "
            "to VoidWhisper/Gezine/Earthonion/EchoStretch/Drakmor",
            SHADOWMOUNT_VERSION, firmware_version, __DATE__, __TIME__);

  load_runtime_config();
  sm_notifications_init();
  stop_conflicting_backpork();
  if (!sm_shellcore_flags_start())
    log_debug("  [SHELLFLAG] monitor unavailable");
  sm_mdbg_init();
  sm_kstuff_init();
  if (!refresh_game_lifecycle_watcher())
    log_debug("  [GAME] lifecycle watcher unavailable");

  if (mkdir("/system_ex/app", 0777) != 0 && errno != EEXIST)
    log_debug("  [MOUNT] failed to create /system_ex/app: %s",
              strerror(errno));
  if (remount_system_ex() != 0)
    log_debug("  [MOUNT] remount_system_ex failed: %s", strerror(errno));

  notify_system("ShadowMount+ v%s exFAT/UFS/PFS", SHADOWMOUNT_VERSION);
  log_non_empty_scan_paths();

  if (runtime_config()->legacy_recursive_scan_forced) {
    notify_system_info(
        "ShadowMount+: recursive_scan=1 deprecated, using scan_depth=2.");
  } else if (runtime_config()->scan_depth > 1u) {
    notify_system_info("ShadowMount+: scan depth %u enabled.",
                       runtime_config()->scan_depth);
  }

  cleanup_mount_dirs();
  run_scanner_until_stop();
  shadowmount_shutdown_sequence();
  return 0;
}

void shadowmount_module_request_stop() {
  request_shutdown_stop("onionhen facade");
}

} // namespace onion::services
