/*
 * Host stubs for util unit tests (no PS5 SDK).
 *
 * Production OnionHEN_log / platform helpers are linked from
 * libonion_platform when ONION_HOST_TEST builds include them.
 * This file only supplies symbols that are PS5-runtime-only.
 */
#include <elfldr_remote.h>
#include <ps5/net_ctl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

/* --- libonion_payload / elfldr_remote (device-only launch path) --- */

static bool g_test_onion_available = false;
static pid_t g_test_onion_launch_pid = -1;
static int g_test_onion_launch_calls = 0;
static uint16_t g_test_onion_last_port = 0;
static pid_t g_test_live_pid = -1;

void onion_test_elfldr_reset(void) {
  g_test_onion_available = false;
  g_test_onion_launch_pid = -1;
  g_test_onion_launch_calls = 0;
  g_test_onion_last_port = 0;
  g_test_live_pid = -1;
}

void onion_test_elfldr_configure(bool available, pid_t launch_pid) {
  g_test_onion_available = available;
  g_test_onion_launch_pid = launch_pid;
}

int onion_test_elfldr_launch_calls(void) { return g_test_onion_launch_calls; }
uint16_t onion_test_elfldr_last_port(void) { return g_test_onion_last_port; }
void onion_test_live_pid(pid_t pid) { g_test_live_pid = pid; }

bool elfldr_remote_available_on(uint16_t port) {
  (void)port;
  return false;
}

bool elfldr_remote_onion_available(void) { return g_test_onion_available; }

bool elfldr_remote_available(void) { return false; }

bool elfldr_remote_send_bytes_to(uint16_t port, const uint8_t *elf,
                                 size_t size) {
  (void)port;
  (void)elf;
  (void)size;
  return false;
}

bool elfldr_remote_send_file_to(uint16_t port, const char *abs_path) {
  (void)port;
  (void)abs_path;
  return false;
}

pid_t elfldr_remote_onion_write_and_launch_get_pid(const char *abs_path,
                                                   const uint8_t *elf,
                                                   size_t size) {
  (void)abs_path;
  (void)elf;
  (void)size;
  ++g_test_onion_launch_calls;
  g_test_onion_last_port = ONION_ELFLDR_PORT;
  return g_test_onion_launch_pid;
}

pid_t elfldr_remote_onion_launch_file_get_pid(const char *abs_path,
                                              const char *args) {
  (void)abs_path;
  (void)args;
  ++g_test_onion_launch_calls;
  g_test_onion_last_port = ONION_ELFLDR_PORT;
  return g_test_onion_launch_pid;
}

pid_t elfldr_remote_onion_write_and_launch_get_pid_with_args(
    const char *abs_path, const uint8_t *elf, size_t size, const char *args) {
  (void)args;
  return elfldr_remote_onion_write_and_launch_get_pid(abs_path, elf, size);
}

pid_t find_pid(const char *name) {
  (void)name;
  return -1;
}

pid_t onion_find_pid(const char *name) {
  (void)name;
  return -1;
}

pid_t onion_find_pid_substr(const char *substr) {
  (void)substr;
  return -1;
}

bool onion_proc_is_alive(pid_t pid) {
  return pid > 1 && pid == g_test_live_pid;
}

int sceKernelGetProcessName(int pid, char *name) {
  (void)pid;
  if (name)
    name[0] = '\0';
  return -1;
}

/* PS5 klog sink — silence unless verbose. */
void klog_printf(const char *fmt, ...) {
  va_list args;
  if (getenv("ONION_TEST_VERBOSE") == NULL) {
    return;
  }
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
}

/* Notification hardware — no-op on host. */
int32_t sceKernelSendNotificationRequest(int32_t device, void *req, size_t size,
                                         int32_t blocking) {
  (void)device;
  (void)req;
  (void)size;
  (void)blocking;
  return 0;
}

int32_t sceNotificationSend(int32_t user_id, bool is_logged,
                            const char *payload) {
  (void)user_id;
  (void)is_logged;
  (void)payload;
  return 0;
}

static int g_test_system_language_result = 0;
static int g_test_system_language_value = 1;

void onion_test_system_language_configure(int result, int value) {
  g_test_system_language_result = result;
  g_test_system_language_value = value;
}

int sceSystemServiceParamGetInt(int param_id, int *value) {
  (void)param_id;
  if (value != NULL) {
    *value = g_test_system_language_value;
  }
  return g_test_system_language_result;
}

/* Bind platform notify to the host stub (constructor runs before tests). */
#include <onion/notify.h>
__attribute__((constructor)) static void host_bind_notify_send(void) {
  onion_notify_set_send(sceKernelSendNotificationRequest);
  onion_notify_set_rich_send(sceNotificationSend);
}

/* Fallback logger if a TU is compiled without libonion_platform log.c.
 * When log.c is linked, that definition wins if this is weak — but most
 * linkers take the first definition. Prefer always linking log.c and not
 * defining OnionHEN_log here.
 */

int util_file_read_alloc(const char *path, char **buf_out, size_t *size_out,
                         size_t max_size) {
  FILE *fp = NULL;
  long file_size = 0;
  char *buf = NULL;
  size_t read_size = 0;

  if (path == NULL || buf_out == NULL) {
    return -1;
  }
  *buf_out = NULL;
  if (size_out != NULL) {
    *size_out = 0;
  }
  if (max_size == 0) {
    max_size = 1024u * 1024u;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    return -1;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  file_size = ftell(fp);
  if (file_size <= 0 ||
      (max_size != (size_t)-1 && (size_t)file_size > max_size)) {
    fclose(fp);
    return -1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }

  buf = (char *)malloc((size_t)file_size + 1);
  if (buf == NULL) {
    fclose(fp);
    return -1;
  }
  read_size = fread(buf, 1, (size_t)file_size, fp);
  fclose(fp);
  if (read_size != (size_t)file_size) {
    free(buf);
    return -1;
  }
  buf[file_size] = '\0';
  *buf_out = buf;
  if (size_out != NULL) {
    *size_out = (size_t)file_size;
  }
  return 0;
}

/* --- SceNetCtl (device-only; drives test_platform_net.c) --- */

static int32_t g_netctl_ret = 0;
static char g_netctl_ip[SCE_NET_CTL_IPV4_ADDR_STR_LEN];

void onion_test_netctl_set_result(int32_t ret, const char *ip_address) {
  g_netctl_ret = ret;
  memset(g_netctl_ip, 0, sizeof(g_netctl_ip));
  if (ip_address != NULL) {
    /* strncpy semantics: a 16-char address fills the field with no NUL. */
    strncpy(g_netctl_ip, ip_address, sizeof(g_netctl_ip));
  }
}

int32_t sceNetCtlGetInfo(int32_t code, SceNetCtlInfo *info) {
  if (code != SCE_NET_CTL_INFO_IP_ADDRESS || info == NULL) {
    return -1;
  }
  if (g_netctl_ret < 0) {
    return g_netctl_ret;
  }
  memcpy(info->ip_address, g_netctl_ip, sizeof(info->ip_address));
  return 0;
}
