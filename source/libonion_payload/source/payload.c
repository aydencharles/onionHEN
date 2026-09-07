/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Strict user Payload ELF loader (PID files + private :9020 elfldr).
 */

#include <onion/payload.h>

#include <elfldr_remote.h>
#include <onion/log.h>
#include <onion/notify.h>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int sceKernelGetProcessName(int pid, char *name);

bool onion_payload_is_elf(const void *buf, size_t size) {
  if (!buf || size < 4)
    return false;
  static const unsigned char kMagic[] = {0x7F, 'E', 'L', 'F'};
  return memcmp(buf, kMagic, 4) == 0;
}

bool onion_payload_elf_key_from_name(const char *name, char *out, size_t out_sz) {
  if (!name || !out || out_sz < 2)
    return false;

  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  if (!base[0] || strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
    return false;

  size_t n = strlen(base);
  if (n >= 4 && strcmp(base + n - 4, ".elf") == 0)
    n -= 4;
  if (n == 0)
    return false; /* bare ".elf" */
  if (n >= out_sz)
    n = out_sz - 1;

  memcpy(out, base, n);
  out[n] = '\0';
  if (out[0] == '\0' || strchr(out, '/') != NULL || strchr(out, '\\') != NULL)
    return false;
  return true;
}

void onion_payload_pid_path(char *out, size_t out_sz, const char *title_id) {
  (void)onion_system_tmp_pid_path(out, out_sz, title_id);
}

pid_t onion_payload_read_pid_file(const char *pid_path) {
  const int f = open(pid_path, O_RDONLY);
  if (f < 0)
    return -1;
  char t[32];
  const int r = (int)read(f, t, sizeof(t) - 1);
  close(f);
  if (r <= 0)
    return -1;
  t[r] = '\0';
  return (pid_t)atoi(t);
}

void onion_payload_write_pid_file(const char *pid_path, pid_t pid) {
  if (!pid_path) {
    return;
  }
  if (pid <= 1) {
    unlink(pid_path);
    return;
  }

  mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  mkdir(ONION_SYSTEM_TMP_PID_ROOT, 0777);
  const int f = open(pid_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (f < 0)
    return;
  char t[32];
  const int len = snprintf(t, sizeof(t), "%d", pid);
  (void)write(f, t, (size_t)len);
  close(f);
}

bool onion_payload_running(const char *title_id) {
  if (!title_id || !title_id[0])
    return false;
  char path[256];
  onion_payload_pid_path(path, sizeof(path), title_id);
  const pid_t pid = onion_payload_read_pid_file(path);
  if (pid <= 1) {
    if (pid == 1) {
      LOG_WARN("Ignoring bogus payload PID 1 for %s", title_id);
      unlink(path);
    }
    return false;
  }
  if (!onion_proc_is_alive(pid)) {
    LOG_WARN("Stale payload PID file for %s (pid=%d dead), removing",
             title_id, (int)pid);
    unlink(path);
    return false;
  }
  return true;
}

static bool valid_payload_key(const char *title_id) {
  return title_id && title_id[0] && strcmp(title_id, ".") != 0 &&
         strcmp(title_id, "..") != 0 && strlen(title_id) < 64 &&
         strchr(title_id, '/') == NULL && strchr(title_id, '\\') == NULL;
}

static pid_t launch_user_payload(const char *title_id, const char *abs_path) {
  if (!elfldr_remote_onion_available()) {
    LOG_ERROR("Private elfldr :%u unavailable; launch failed for %s",
              ONION_ELFLDR_PORT, title_id);
    return -1;
  }

  const pid_t pid = elfldr_remote_onion_launch_file_get_pid(abs_path, NULL);
  if (pid <= 1) {
    LOG_ERROR("Private elfldr :%u returned no valid PID for %s (pid=%d)",
              ONION_ELFLDR_PORT, title_id, (int)pid);
    return -1;
  }

  char pname[32] = {0};
  if (sceKernelGetProcessName(pid, pname) == 0)
    LOG_INFO("Launched via %u (pid=%d name=%s)", ONION_ELFLDR_PORT, (int)pid,
             pname);
  else
    LOG_INFO("Launched via %u (pid=%d)", ONION_ELFLDR_PORT, (int)pid);
  return pid;
}

pid_t onion_payload_launch_elfldr(const char *title_id, const char *path) {
  if (!valid_payload_key(title_id) || !path || path[0] != '/') {
    LOG_ERROR("launch_elfldr: invalid args title=%s path=%s",
              title_id ? title_id : "(null)", path ? path : "(null)");
    return -1;
  }

  LOG_INFO("loading payload via elfldr key=%s path=%s", title_id, path);
  return launch_user_payload(title_id, path);
}

uint8_t *onion_payload_read_file(const char *path, size_t *out_size) {
  if (!path || !out_size)
    return NULL;

  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open file %s (%s)", path, strerror(errno));
    return NULL;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    LOG_ERROR("Failed to stat file %s", path);
    close(fd);
    return NULL;
  }
  if (st.st_size <= 0) {
    LOG_WARN("Empty payload file %s", path);
    close(fd);
    return NULL;
  }

  uint8_t *buf = (uint8_t *)malloc((size_t)st.st_size);
  if (!buf) {
    LOG_ERROR("Failed to allocate %lld bytes for payload",
                 (long long)st.st_size);
    close(fd);
    return NULL;
  }

  if (read(fd, buf, (size_t)st.st_size) != st.st_size) {
    LOG_ERROR("Failed to read payload file %s", path);
    free(buf);
    close(fd);
    return NULL;
  }
  close(fd);
  *out_size = (size_t)st.st_size;
  return buf;
}

bool onion_payload_load_with_key(const char *path, const char *filename,
                                 const char *requested_key) {
  size_t size = 0;
  uint8_t *buf = onion_payload_read_file(path, &size);
  if (!buf)
    return false;

  char name_buf[256];
  const char *base = filename;
  if (!base || !base[0]) {
    snprintf(name_buf, sizeof(name_buf), "%s", path);
    base = basename(name_buf);
  }

  const size_t base_len = strlen(base);
  if (!(base_len > 4 && strcmp(base + base_len - 4, ".elf") == 0)) {
    LOG_WARN("Not a .elf payload: %s", base);
    onion_notify(1, "notify.payload.elf_only", base);
    free(buf);
    return false;
  }

  if (!onion_payload_is_elf(buf, size)) {
    LOG_ERROR("Invalid ELF file: %s", base);
    onion_notify(1, "notify.payload.invalid_elf", base);
    free(buf);
    return false;
  }

  char key[64];
  if (requested_key && valid_payload_key(requested_key)) {
    snprintf(key, sizeof(key), "%s", requested_key);
  } else if (!onion_payload_elf_key_from_name(base, key, sizeof(key))) {
    LOG_ERROR("Invalid ELF basename (empty stem): %s", base);
    onion_notify(1, "notify.payload.invalid_name", base);
    free(buf);
    return false;
  }

  LOG_DEBUG("payload launch key=%s (from %s)", key, base);
  char pid_path[256];
  onion_payload_pid_path(pid_path, sizeof(pid_path), key);

  if (onion_payload_running(key)) {
    LOG_INFO("Payload %s is already running; keeping the recorded instance",
             key);
    free(buf);
    return true;
  }

  if (!elfldr_remote_onion_available()) {
    LOG_WARN("Private elfldr :%u unavailable; payload %s not started",
             ONION_ELFLDR_PORT, key);
    free(buf);
    return false;
  }

  const pid_t pid = onion_payload_launch_elfldr(key, path);
  free(buf);
  /* Only persist real pids; never write 0/1 (PID 1 would hit system init). */
  if (pid > 1)
    onion_payload_write_pid_file(pid_path, pid);
  else
    onion_payload_write_pid_file(pid_path, -1);
  return pid > 1;
}

bool onion_payload_load(const char *path, const char *filename) {
  return onion_payload_load_with_key(path, filename, NULL);
}
