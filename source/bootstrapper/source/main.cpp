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


/******************************************************************************
 * Standard and System Header Includes
 ******************************************************************************/
 #include <csignal>
 #include <dirent.h>
 #include <errno.h>
 #include <fcntl.h>
 #include <netinet/in.h>
 #include <pthread.h>
 #include <setjmp.h>
 #include <stdarg.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <sys/_iovec.h>
 #include <sys/mount.h>
 #include <sys/signal.h>
 #include <sys/socket.h>
 #include <sys/stat.h>
 #include <sys/sysctl.h>
 #include <sys/types.h>
 #include <sys/un.h>
 #include <sys/wait.h>
 #include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <onion/ready.h>
#include <onion/settings.hpp>
#include <onion/log_settings.hpp>
#include <onion/platform.h>
#include <onion/notify.h>
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/payload.h>
#include <errno.h>
 
 /******************************************************************************
  * Custom Header Includes
  ******************************************************************************/
 #include <freebsd-helper.h>
 #include <elfldr_remote.h>
 
 extern "C" {
 #include "elfldr.h"
 #include "faulthandler.h"
 #include "hbldr.h"
 #include "kstuff_probe.h"
 #include "pt.h"
 #include <ps5/klog.h>
 #include <ps5/kernel.h>

 int sceKernelMprotect(void* addr, size_t len, int prot);

 extern uint8_t kstuff_start[];
 extern const unsigned int kstuff_size;
 extern uint8_t ftpsrv_start[];
 extern const unsigned int ftpsrv_size;

 }

 /******************************************************************************
  * Macros and Constants
  ******************************************************************************/
 #define QAFLAGS_SIZE 16
 #define USER_SERVICE_ID 0x80000011
 #define SYSTEM_SERVICE_ID 0x80000010
 #define LNC_UTIL_ERROR_ALREADY_RUNNING 0x8094000c
 #define LNC_ERROR_APP_NOT_FOUND 0x80940031
 #define ENTRYPOINT_OFFSET 0x70
 
 #define PROCESS_LAUNCHED 1
 
 #define LOOB_BUILDER_SIZE 21
 #define LOOP_BUILDER_TARGET_OFFSET 3
 
 #define USLEEP_NID "QcteRwbsnV0"
 
 #define LOOKUP_SYMBOL(resolver, sym) \
   resolver_lookup_symbol(resolver, sym, strlen(sym))
   
 #define SET_FUNCTION_ADDRESS(resolver, function) \
   *(void **)&(function) = \
       (void *)LOOKUP_SYMBOL(resolver, #function) /* NOLINT */
 
 #define BUILD_IOVEC(str) \
   { .iov_base = (str), .iov_length = __builtin_strlen(str) + 1 }
 
 /******************************************************************************
  * Type Definitions and Structures
  ******************************************************************************/
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
 
 typedef enum {
   Flag_None = 0,
   SkipLaunchCheck = 1,
   SkipResumeCheck = 1,
   SkipSystemUpdateCheck = 2,
   RebootPatchInstall = 4,
   VRMode = 8,
   NonVRMode = 16,
   Pft = 32UL,
   RaIsConfirmed = 64UL,
   ShellUICheck = 128UL
 } Flag;
 
 typedef struct {
   uint32_t sz;
   int user_id;
   uint32_t app_opt;
   uint64_t crash_report;
   Flag check_flag;
 } LncAppParam;
 
 typedef struct {
   const void *iov_base;
   size_t iov_length;
 } iovec_t;
 
 typedef struct FileDescriptors {
   int fd = 1;
 } FileDescriptor;
 
 typedef struct {
   uint64_t pad0;
   char version_str[0x1C];
   uint32_t version;
   uint64_t pad1;
 } OrbisKernelSwVersion;
 
 typedef struct app_info {
   uint32_t app_id;
   uint64_t unknown1;
   uint32_t app_type;
   char     title_id[10];
   char     unknown2[0x3c];
 } app_info_t;
 
 /******************************************************************************
  * External Declarations
  ******************************************************************************/
 extern "C" {
     int sceKernelSendNotificationRequest(int32_t device,
                                          OrbisNotificationRequest *req,
                                          size_t size, int32_t blocking);
     int sceNotificationSend(int user_id, bool is_logged, const char *payload);
     int sceUserServiceGetForegroundUser(uint32_t *userId);
     int sceLncUtilLaunchApp(const char *tid, const char *argv[],
                             LncAppParam *param);
     uint32_t sceLncUtilKillApp(uint32_t appId);
     int sceSystemServiceGetAppId(const char *titleId);
     int sceSystemServiceParamGetInt(int param_id, int *value);
     int sceUserServiceInitialize(void *param);
     int sceKernelGetProsperoSystemSwVersion(OrbisKernelSwVersion *sw);
     int unmount(const char *path, int flags);
     int sceKernelGetAppInfo(int pid, app_info_t *title);
     int sceKernelGetProcessName(int pid, char *name);
     int sceKernelGetOpenPsIdForSystem(void *psid);
     int sceKernelIsGenuineDevKit();

     bool devkit_byepervisor(void);
     void notify(const char *text, ...) {
     va_list args;
     va_start(args, text);
     onion_notify_v(/*show_watermark=*/0, text, args);
     va_end(args);
 }

    
 }
 
 extern int _write(int fd, const void *, size_t); // NOLINT
 extern ssize_t _read(int, void *, size_t);       // NOLINT
 
 extern const unsigned int daemon_size;
 extern uint8_t daemon_start[];
 extern uint8_t util_start[];
 extern const unsigned int util_size;
 extern uint8_t onion_elfldr_start[];
 extern const unsigned int onion_elfldr_size;
 extern uint8_t sicon_start[];
 extern const unsigned int sicon_size;

/*
 * Toolbox icon symbols and the runtime manifest both come from the generated
 * icon list, so adding an icon means dropping an SVG into assets/ and nothing
 * else. See bootstrapper/CMakeLists.txt.
 */
#define ONION_ICON(name)                                                       \
  extern uint8_t name##_start[];                                               \
  extern const unsigned int name##_size;
#include "icon_manifest.inc"
#undef ONION_ICON

namespace {
struct EmbeddedIcon {
  const char *name;
  const uint8_t *data;
  unsigned int size;
};

/* Not constexpr: the _size symbols are resolved at link time. */
const EmbeddedIcon kEmbeddedIcons[] = {
#define ONION_ICON(name) {#name, name##_start, name##_size},
#include "icon_manifest.inc"
#undef ONION_ICON
};
} // namespace
 
 /******************************************************************************
  * Global Variables
  ******************************************************************************/
 int payload_count = 0;
 char buff[255];
 char **loaded_filenames = NULL;
 jmp_buf g_catch_buf;
 FileDescriptor sock;
 
 // Constants
 /* Must NOT use 9021 — that is external elfldr. */
 static const int LOGGER_PORT = 9088;
 static const int STDOUT = 1;
 static const int STDERR = 2;
 
 /******************************************************************************
  * Function Prototypes
  ******************************************************************************/
/** Materialize embedded assets; return whether the startup icon is ready. */
static bool write_embedded_assets();
 void notify(const char *text, ...);
static void notify_starting(bool custom_icon_ready);
static void cleanup(void);
 FileDescriptor FileDescriptor_init(int fd);
 int initStdout();
 void release(FileDescriptor *fd);
 void patch_app_db(void);
 static bool remount(const char *dev, const char *path);
 
 /******************************************************************************
  * Function Implementations
  ******************************************************************************/
 extern uint8_t shellui_prx_start[];
 extern const unsigned int shellui_prx_size;

  static bool write_blob_file(const char *path, const void *data, size_t size) {
    char temp_path[1024];
    const int path_len = snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", path,
                                  getpid());
    if (path_len < 0 || static_cast<size_t>(path_len) >= sizeof(temp_path)) {
      LOG_DEBUG("write_embedded_assets: path too long: %s", path);
      return false;
    }

    unlink(temp_path);
    int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
      LOG_ERROR("write_embedded_assets: open failed: %s (%s)", path,
                  strerror(errno));
      return false;
    }

    const uint8_t *cursor = static_cast<const uint8_t *>(data);
    size_t remaining = size;
    while (remaining > 0) {
      const ssize_t written = write(fd, cursor, remaining);
      if (written < 0 && errno == EINTR)
        continue;
      if (written < 0) {
        LOG_ERROR("write_embedded_assets: write failed: %s (%s)", path,
                    strerror(errno));
        close(fd);
        unlink(temp_path);
        return false;
      }
      if (written == 0) {
        LOG_DEBUG("write_embedded_assets: zero-byte write: %s", path);
        close(fd);
        unlink(temp_path);
        return false;
      }
      cursor += written;
      remaining -= static_cast<size_t>(written);
    }

    if (close(fd) != 0 || rename(temp_path, path) != 0) {
      LOG_ERROR("write_embedded_assets: commit failed: %s (%s)", path,
                  strerror(errno));
      unlink(temp_path);
      return false;
    }
    return true;
  }

  static bool write_embedded_assets() {
    mkdir("/data/OnionHEN/", 0777);
    mkdir("/data/OnionHEN/assets/", 0777);
    /* Always refresh only OnionHEN-owned assets.  NPXS40008's stock
     * icon_setting.png stays untouched; ShellUI redirects that URI while its
     * hooks are active so Debug Settings returns to the Sony icon by itself
     * when the plugin is not running. */
    const bool startup_icon_ready = write_blob_file(
        "/data/OnionHEN/onionhen.png", &sicon_start, sicon_size);

    // Toolbox category icons
    for (const EmbeddedIcon &icon : kEmbeddedIcons) {
      char path[256];
      snprintf(path, sizeof(path), "/data/OnionHEN/assets/%s.png", icon.name);
      write_blob_file(path, icon.data, icon.size);
    }


    mkdir("/system_ex/vsh_asset/", 0777);
    write_blob_file("/system_ex/vsh_asset/onionhen.png", &sicon_start,
                    sicon_size);

    return startup_icon_ready;
}


static void notify_starting(bool custom_icon_ready) {
  if (!custom_icon_ready) {
    LOG_WARN("Startup icon unavailable; using system notification icon");
    notify("notify.boot.starting");
    return;
  }
  onion_notify_rich("notify.brand", "notify.boot.starting",
                    "/user/data/OnionHEN/onionhen.png", "download",
                    "588193128");
}

  bool is_elf_header(uint8_t* data)
  {
      uint8_t header[] = { 0x7f, 'E', 'L', 'F' };

      return !memcmp(data, header, 4);
  }


  uint8_t* get_kstuff_address(bool& require_cleanup) {
      const char* path = "/data/OnionHEN/kstuff.elf";
      long offset = 0;
      off_t size;
      uint8_t* address;
      int fd;

      if (!if_exists(path)) {
          goto embedded_kstuff;
      }

      fd = open(path, O_RDONLY);
      if (fd <= 0) {
          goto embedded_kstuff;
      }

      size = lseek(fd, 0, SEEK_END);
      address = (uint8_t*)malloc(size);

      if (!address) {
          goto close_fd;
      }

      lseek(fd, 0, SEEK_SET);

      while (offset != size) {
          int n = read(fd, address + offset, size - offset);

          if (n <= 0)
          {
              goto free_mem;
          }

          offset += n;
      }

      if (!is_elf_header(address)) {
          notify( "notify.kstuff.no_elf_header", path);
          goto free_mem;
      }

      require_cleanup = true;
      notify("notify.kstuff.loading", path);
      return address;

  free_mem:
      free(address);
  close_fd:
      close(fd);
  embedded_kstuff:
      require_cleanup = false;
      return kstuff_start;
  }
 
 static bool remount(const char *dev, const char *path) {
   iovec_t iov[] = {BUILD_IOVEC("fstype"),    BUILD_IOVEC("exfatfs"),
                    BUILD_IOVEC("fspath"),    BUILD_IOVEC(path),
                    BUILD_IOVEC("from"),      BUILD_IOVEC(dev),
                    BUILD_IOVEC("large"),     BUILD_IOVEC("yes"),
                    BUILD_IOVEC("timezone"),  BUILD_IOVEC("static"),
                    BUILD_IOVEC("async"),     {NULL, 0},
                    BUILD_IOVEC("ignoreacl"), {NULL, 0}};
   return nmount((struct iovec *)iov, sizeof(iov) / sizeof(iov[0]),
                 MNT_UPDATE) == 0;
 }
 static void cleanup(void) { 
    if (sock.fd != -1) {
      close(sock.fd);
      sock.fd = -1;
    }
  
    // Notify user about cleanup
    notify("notify.boot.cleaned_up");
  
    // Exit the program
    exit(0);
 }
 
 // FileDescriptor methods implementations
 FileDescriptor FileDescriptor_init(int fd) {
   FileDescriptor newFd;
   newFd.fd = fd;
   return newFd;
 }
 
 void release(FileDescriptor *fd) { 
   fd->fd = -1; 
 }
 
 // Stdout initialization logic
 int initStdout() {
   // Check for logging file existence logic here
   // For simplicity, I'm assuming it always exists
   sock.fd = -1;
   sock = FileDescriptor_init(socket(AF_INET, SOCK_STREAM, 0));
   if (sock.fd == -1) {
     notify("notify.net.socket_create", strerror(errno));
     return -1;
   }
 
   int value = 1;
   if (setsockopt(sock.fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
     notify("notify.net.socket_options", strerror(errno));
     return -1;
   }
 
   struct sockaddr_in server_addr;
   (void)memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(LOGGER_PORT);
   server_addr.sin_addr.s_addr = 0;
 
   if (bind(sock.fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
     notify("notify.net.socket_bind", strerror(errno));
     return -1;
   }
 
   if (listen(sock.fd, 1) != 0) {
     notify("notify.net.socket_listen", strerror(errno));
     return -1;
   }
 
   struct sockaddr client_addr;
   socklen_t addr_len = sizeof(client_addr);
   int conn = accept(sock.fd, &client_addr, &addr_len);
   if (conn != -1) {
     dup2(conn, STDOUT);
     dup2(conn, STDERR);
     close(conn);
     return conn;
   }
 
   notify("notify.net.socket_accept", strerror(errno));
   return -1;
 }
 
 // Load a payload .elf
bool load_payload(const char *path, const char *filename) {
  return onion_payload_load(path, filename);
}

/*=================== LOAD PAYLOADS (autostart .elf) =========================*/
char **find_payload_files() {
  const char *base_dirs[] = {
      "/mnt/usb0/onionhen/payloads", "/mnt/usb0/OnionHEN/payloads",
      "/mnt/usb1/onionhen/payloads", "/mnt/usb2/onionhen/payloads",
      "/mnt/usb3/onionhen/payloads", "/user/data/OnionHEN/payloads",
      "/user/data/onionhen/payloads", "/data/OnionHEN/payloads",
  };

  int base_dirs_count = sizeof(base_dirs) / sizeof(base_dirs[0]);

  char **payload_paths = NULL;
  char full_path[255];
  char auto_start_path[255];
  payload_count = 0;
  loaded_filenames = (char **)malloc(255 * sizeof(char *));

  for (int i = 0; i < base_dirs_count; i++) {
    DIR *dir = opendir(base_dirs[i]);
    if (!dir)
      continue;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      (void)memset(full_path, 0, sizeof(full_path));
      if (entry->d_type != DT_REG)
        continue;
      const char *ext = strrchr(entry->d_name, '.');
      if (!ext || strcmp(ext, ".elf") != 0)
        continue;

      bool skip = false;
      snprintf(full_path, sizeof(full_path), "%s/%s", base_dirs[i],
               entry->d_name);
      snprintf(auto_start_path, sizeof(auto_start_path), "%s/%s.auto_start",
               base_dirs[i], entry->d_name);

      if (!if_exists(auto_start_path)) {
        LOG_WARN("skipping auto start for payload: %s", full_path);
        continue;
      }

      for (int j = 0; j < payload_count; j++) {
        if (strcmp(loaded_filenames[j], entry->d_name) == 0) {
          skip = true;
          LOG_WARN("skipping duplicate payload: %s | already loaded: %s",
                 full_path, loaded_filenames[j]);
          break;
        }
      }
      if (skip)
        continue;

      payload_paths =
          (char **)realloc(payload_paths, (payload_count + 1) * sizeof(char *));
      payload_paths[payload_count] = strdup(full_path);
      loaded_filenames[payload_count] = strdup(entry->d_name);
      payload_count++;
    }
    closedir(dir);
  }

  return payload_paths;
}
void free_payload_files(char **plugin_files) {
  // Free memory for loaded_filenames
  for (int i = 0; i < payload_count; i++) {
    free(loaded_filenames[i]);
  }
  free(loaded_filenames);

  for (int i = 0; i < payload_count; i++) {
    free((void *)plugin_files[i]);
  }
  free((void *)plugin_files);
}

/*=================== Launch pipeline (phased) =========================*/
/*
 * Policy: bootstrap through external 9021, then prefer OnionHEN's private
 * embedded elfldr on 9020 for runtime payload launches.
 * 1) launch_chain — send embedded util/kstuff/daemon bytes to elfldr
 * 2) load_autostart_payloads — payloads .elf with .auto_start (skip *elfldr*)
 */

static void kill_by_name(const char *a, const char *b) {
  int p = -1;
  while ((a && (p = onion_find_pid_substr(a)) > 0) ||
         (b && (p = onion_find_pid_substr(b)) > 0)) {
    kill(p, SIGKILL);
    sleep(1);
  }
}

static uint16_t g_payload_loader_port = ELFLDR_REMOTE_PORT;

static bool loader_available(uint16_t port) {
  return port == ONION_ELFLDR_PORT ? elfldr_remote_onion_available()
                                   : elfldr_remote_available_on(port);
}

static bool wait_onion_loader(int timeout_ms) {
  const int poll_ms = 200;
  for (int waited = 0; waited < timeout_ms; waited += poll_ms) {
    if (elfldr_remote_onion_available())
      return true;
    usleep(poll_ms * 1000);
  }
  return false;
}

static bool ensure_embedded_elfldr_9020(void) {
  if (elfldr_remote_onion_available()) {
    LOG_WARN("embedded elfldr :9020 already available");
    return true;
  }

  LOG_DEBUG("Starting embedded elfldr on %u via external %u ...",
              ONION_ELFLDR_PORT, ELFLDR_REMOTE_PORT);
  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, onion_elfldr_start,
                                   onion_elfldr_size)) {
    LOG_ERROR("  embedded elfldr launch send failed");
    return false;
  }

  if (wait_onion_loader(10000)) {
    LOG_DEBUG("  embedded elfldr :9020 ready");
    return true;
  }

  LOG_DEBUG("  embedded elfldr :9020 did not become ready");
  return false;
}

static bool launch_blob(const uint8_t *elf, size_t size, const char *label,
                        const char *wait_name) {
  if (g_payload_loader_port != ELFLDR_REMOTE_PORT &&
      !loader_available(g_payload_loader_port)) {
    g_payload_loader_port = ELFLDR_REMOTE_PORT;
  }
  LOG_DEBUG("%u memory ELF: %s (%zu bytes)", g_payload_loader_port, label,
              size);
  if (!elfldr_remote_send_bytes_to(g_payload_loader_port, elf, size)) {
    if (g_payload_loader_port != ELFLDR_REMOTE_PORT &&
        elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, elf, size)) {
      LOG_ERROR("  send via %u failed; fallback %u accepted %s",
                  g_payload_loader_port, ELFLDR_REMOTE_PORT, label);
      g_payload_loader_port = ELFLDR_REMOTE_PORT;
    } else {
      LOG_ERROR("  send FAILED %s", label);
      return false;
    }
  }
  for (int i = 0; i < 30; i++) {
    if (wait_name && onion_find_pid_substr(wait_name) > 0) {
      LOG_DEBUG("  running: %s", wait_name);
      return true;
    }
    sleep(1);
  }
  LOG_DEBUG("  sent %s (process name not seen yet, continuing)", label);
  return true;
}

/**
 * Launch util → kstuff → daemon via the selected elfldr port (serialized).
 * Soft-fails kstuff; hard-fails missing elfldr / util / daemon.
 * Returns 0 or -2.
 */
static int launch_chain(const OrbisKernelSwVersion &sys_ver) {
  char buz[100] = {0};

  if (!elfldr_remote_available()) {
    LOG_DEBUG("FATAL: no elfldr on 127.0.0.1:9021");
    notify("notify.elfldr.need_9021");
    return -2;
  }
  g_payload_loader_port =
      ensure_embedded_elfldr_9020() ? ONION_ELFLDR_PORT : ELFLDR_REMOTE_PORT;
  LOG_DEBUG("payload loader port: %u", g_payload_loader_port);
  LOG_DEBUG("launching embedded ELFs (serialized)");
  sleep(3); /* settle after remount/unmount */

  /*
   * Order: util → kstuff → daemon
   * (daemon injects toolbox; kstuff must patch ShellUI first)
   */
  LOG_DEBUG("Starting util via %u ...", g_payload_loader_port);
  kill_by_name("onion_util.elf", "util.elf");
  kill_by_name("OnionHEN Utility", nullptr);
  onion_ready_clear(ONION_READY_UTIL);
  onion_ready_clear(ONION_READY_KSTUFF);
  onion_ready_clear(ONION_READY_DAEMON);
  /*
   * Keep Toolbox's PID marker across a daemon/re-HEN restart.  A stale marker
   * is harmless: daemon compares it with the current SceShellUI PID before
   * deciding whether to inject.
   */
  /* Runtime flags that must not stick across re-HEN without reboot. */
  onion_ready_clear(ONION_FLAG_UTIL_BOOTED);
  onion_ready_clear(ONION_FLAG_FPS_OVERLAY);

  if (!launch_blob(util_start, util_size, "util", "onion_util.elf")) {
    notify("notify.elfldr.launch_util");
    return -2;
  }
  if (!onion_ready_wait(ONION_READY_UTIL, /*timeout_ms=*/15000, /*poll_ms=*/200))
    LOG_WARN("util ready timeout — continuing (process may still be starting)");

  onion::Settings boot_settings{};
  (void)onion::settings_load(&boot_settings);
  const bool skip_kstuff = if_exists("/mnt/usb0/no_kstuff") ||
                           !boot_settings.kstuff_autoload;
  if (skip_kstuff) {
    LOG_DEBUG("kstuff disabled (%s)",
              if_exists("/mnt/usb0/no_kstuff") ? "usb no_kstuff"
                                               : "kstuff.autoload=false");
    onion_ready_signal(ONION_READY_KSTUFF);
  } else if (sys_ver.version >= 0x3000000) {
    if (kstuff_already_running()) {
      /* Re-HEN after stack shutdown leaves kstuff alive; do not double-load. */
      LOG_WARN("kstuff already running / mprotect OK — skip launch");
      onion_ready_signal(ONION_READY_KSTUFF);
    } else {
      LOG_DEBUG("Loading kstuff via %u (before daemon/toolbox) ...",
                  g_payload_loader_port);
      uint8_t *override_elf = nullptr;
      size_t override_size = 0;
      if (if_exists("/data/OnionHEN/kstuff.elf"))
        override_elf = onion_payload_read_file("/data/OnionHEN/kstuff.elf",
                                               &override_size);
      const uint8_t *kelf = override_elf ? override_elf : kstuff_start;
      const size_t kelf_size =
          override_elf ? override_size : (size_t)kstuff_size;
      const bool kstuff_sent =
          launch_blob(kelf, kelf_size, "kstuff", "kstuff.elf");
      free(override_elf);
      if (kstuff_sent) {
        int wait = 0;
        bool not_loaded = true;
        while ((not_loaded = (sceKernelMprotect(&buz[0], 100, 0x7) < 0))) {
          if (wait++ > 15) {
            notify("notify.kstuff.load_failed");
            break;
          }
          sleep(1);
        }
        if (!not_loaded) {
          LOG_DEBUG("kstuff mprotect OK — signal ready");
          onion_ready_signal(ONION_READY_KSTUFF);
          sleep(1); /* brief settle for ShellUI trophy patches */
        }
      } else {
        notify("notify.kstuff.load_elfldr_failed");
      }
    }
  } else {
    onion_ready_signal(ONION_READY_KSTUFF);
  }

  kill_by_name("ftpsrv.elf", "ftpsrv");
  if (boot_settings.ftp_autoload) {
    LOG_DEBUG("Loading ftpsrv via %u ...", g_payload_loader_port);
    if (!launch_blob(ftpsrv_start, (size_t)ftpsrv_size, "ftpsrv",
                     "ftpsrv.elf")) {
      LOG_WARN("failed to launch ftpsrv; continuing without FTP service");
    }
  } else {
    LOG_DEBUG("FTP server disabled (ftp.autoload=false)");
  }

  LOG_DEBUG("Starting daemon via %u (toolbox inject) ...",
              g_payload_loader_port);
  kill_by_name("onion_daemon.elf", "daemon.elf");
  kill_by_name("OnionHEN Critical", nullptr);
  onion_ready_clear(ONION_READY_DAEMON);
  if (!launch_blob(daemon_start, daemon_size, "daemon", "onion_daemon.elf")) {
    notify("notify.elfldr.launch_daemon");
    return -2;
  }
  if (!onion_ready_wait(ONION_READY_DAEMON, /*timeout_ms=*/20000,
                        /*poll_ms=*/200))
    LOG_WARN("daemon ready timeout — continuing");

  return 0;
}

/** Autostart payloads (.elf) with .auto_start marker; skip *elfldr*. */
static void load_autostart_payloads(void) {
  if (!elfldr_remote_onion_available()) {
    LOG_WARN("Skipping user payload autostart: private elfldr :9020 unavailable");
    notify("notify.payload.autostart_skipped");
    return;
  }

  char **payload_paths = find_payload_files();
  if (!payload_paths || payload_count <= 0)
    return;

  int loaded = 0;
  for (int i = 0; i < payload_count; i++) {
    if (strstr(payload_paths[i], "elfldr") != nullptr)
      continue;
    LOG_DEBUG("Loading payload: %s", payload_paths[i]);
    if (!load_payload(payload_paths[i], loaded_filenames[i])) {
      notify("notify.payload.load_failed_path", payload_paths[i]);
      LOG_ERROR("FAILED!");
      continue;
    }
    LOG_DEBUG("Loaded!");
    loaded++;
  }
  LOG_DEBUG("Successfully loaded %d payloads", loaded);
  free_payload_files(payload_paths);
}

int main(void) {
  /* Real linked kernel export — must bind before any notify(). */
  onion_notify_set_send(reinterpret_cast<onion_notify_send_fn>(
      sceKernelSendNotificationRequest));
  onion_notify_set_rich_send(reinterpret_cast<onion_notify_rich_send_fn>(
      sceNotificationSend));
  onion::Settings notification_settings{};
  (void)onion::settings_load(&notification_settings);
  (void)onion::apply_log_settings(notification_settings);
  int system_language = 1;
  if (notification_settings.ui_lang == onion::kUiLanguageSystem)
    (void)sceSystemServiceParamGetInt(1, &system_language);
  onion_notify_apply_ui_language(notification_settings.ui_lang,
                                 system_language);

  signal(SIGCHLD, SIG_IGN);

  LOG_DEBUG("Jailbreaking the boostrapper ...");
  if (elfldr_raise_privileges(getpid())) {
    notify("notify.priv.unable");
    return -1;
  }
  LOG_DEBUG("   Success!");

  if (if_exists("/data/I_want_logging_for_onionhen")) {
    LOG_DEBUG("Redirecting stdout and stderr to logger ...");
    if (initStdout() >= 0)
      LOG_DEBUG("   Success!");
    else
      LOG_ERROR("   Failed!");
  }

  OrbisKernelSwVersion sys_ver;
  sceKernelGetProsperoSystemSwVersion(&sys_ver);

  // Byepervisor (1.xx–2.xx HV path) removed from OnionHEN.
  if (sys_ver.version < 0x3000000 && !sceKernelIsGenuineDevKit()) {
    LOG_DEBUG("FW %s is < 3.00 and Byepervisor is not bundled; continuing without HV path",
                sys_ver.version_str);
  }

  LOG_DEBUG("============== Spawner (Bootstrapper) Started =================");

  // Directory layout
  mkdir("/data/OnionHEN", 0777);
  mkdir("/data/OnionHEN/payloads", 0777);
  mkdir("/data/OnionHEN/assets", 0777);
  mkdir("/data/OnionHEN/games", 0777);

  LOG_DEBUG("Registering signal handler ...");
  fault_handler_init(cleanup);
  LOG_DEBUG("   Success!");

  LOG_DEBUG("Remounting system partitions ...");
  if (!remount("/dev/ssd0.system_ex", "/system_ex")) {
    perror("failed to mount /system_ex\nif you see this reboot");
    notify("notify.mount.system_ex");
    return -1;
  }
  if (!remount("/dev/ssd0.system", "/system")) {
    perror("failed to mount /system_\nif you see this reboot");
    notify("notify.mount.system");
    return -1;
  }
  LOG_DEBUG("   Success!");

  LOG_DEBUG("Writing embedded assets ...");
  const bool startup_icon_ready = write_embedded_assets();
  LOG_DEBUG("   Written!");
  notify_starting(startup_icon_ready);

  LOG_DEBUG("Unmounting /update forcefully ...");
  unlink("/update/PS5UPDATE.PUP");
  unlink("/update/PS5UPDATE.PUP.net.temp");
  if ((int)unmount("/update", 0x80000LL) < 0)
    unmount("/update", 0);
  LOG_DEBUG("   Success!");

  int rc = launch_chain(sys_ver);
  if (rc != 0)
    return rc;

  load_autostart_payloads();

  LOG_DEBUG("============== Spawner (Bootstrapper) Finished =================");
  return 0;
}
