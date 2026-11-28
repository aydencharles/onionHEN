/* Copyright (C) 2026 OnionHEN Contributors
 *
 * OnionHEN PS5 PRX Example Plugin
 *
 * Demonstrates a minimal, game-agnostic PS5 PRX loaded via OnionHEN's
 * SprxLoader (sceKernelLoadStartModule):
 *   1. Shows a PS5 system toast notification when module_start() runs.
 *   2. Logs diagnostics to PS5 klog (sceKernelDebugOutText) and optionally
 *      appends to /data/OnionHEN/sprx/onion_example.log if accessible.
 *   3. Spawns a lightweight background thread via scePthreadCreate() that
 *      emits 3 periodic heartbeats (every 5 s) without touching game rendering
 *      or hooking any game functions.
 *
 * Imports ONLY libkernel.prx so it stays independent of game libc versions.
 */

#include <stddef.h>
#include <stdint.h>

/* ---- Minimal libkernel.prx declarations ---- */

typedef void *ScePthread;
typedef int64_t ssize_t;

extern int getpid(void);
extern int sceKernelSendNotificationRequest(int device, const void *req,
                                            size_t size, int blocking);
extern int sceKernelDebugOutText(int channel, const char *text);
extern int sceKernelUsleep(unsigned int microseconds);
extern uint64_t sceKernelGetProcessTime(void);
extern int sceKernelOpen(const char *path, int flags, int mode);
extern ssize_t sceKernelWrite(int fd, const void *buf, size_t nbytes);
extern int sceKernelClose(int fd);
extern int scePthreadCreate(ScePthread *thread, const void *attr,
                            void *(*entry)(void *), void *arg,
                            const char *name);
extern int scePthreadJoin(ScePthread thread, void **value);

/* FreeBSD/Orbis open(2) flags */
#define SCE_O_WRONLY 0x0001
#define SCE_O_APPEND 0x0008
#define SCE_O_CREAT  0x0200

struct SceNotificationRequest {
  int32_t type;
  int32_t req_id;
  int32_t priority;
  int32_t msg_id;
  int32_t target_id;
  int32_t user_id;
  int32_t unk1;
  int32_t unk2;
  int32_t app_id;
  int32_t error_num;
  int32_t unk3;
  uint8_t use_icon_image_uri;
  char message[1024];
  char icon_uri[1024];
  char unk[1027];
};

_Static_assert(sizeof(struct SceNotificationRequest) == 3120,
               "SceNotificationRequest size mismatch");

/* ---- Tiny freestanding string helpers (no libc dependency) ---- */

static void zero_mem(void *dst, size_t n) {
  volatile uint8_t *p = (volatile uint8_t *)dst;
  for (size_t i = 0; i < n; ++i)
    p[i] = 0;
}

static size_t append_str(char *dst, size_t pos, size_t cap, const char *src) {
  if (!dst || !src || cap == 0)
    return pos;
  while (*src && pos + 1 < cap)
    dst[pos++] = *src++;
  dst[pos] = '\0';
  return pos;
}

static size_t append_u64(char *dst, size_t pos, size_t cap, uint64_t val) {
  char tmp[24];
  size_t n = 0;
  if (val == 0) {
    tmp[n++] = '0';
  } else {
    while (val > 0 && n < sizeof(tmp)) {
      tmp[n++] = (char)('0' + (val % 10));
      val /= 10;
    }
  }
  while (n > 0 && pos + 1 < cap)
    dst[pos++] = tmp[--n];
  if (cap > 0)
    dst[pos] = '\0';
  return pos;
}

static size_t append_i64(char *dst, size_t pos, size_t cap, int64_t val) {
  if (val < 0) {
    pos = append_str(dst, pos, cap, "-");
    return append_u64(dst, pos, cap, (uint64_t)(-(val + 1)) + 1u);
  }
  return append_u64(dst, pos, cap, (uint64_t)val);
}

/* ---- Notification and logging helpers ---- */

static const char kLogPath[] = "/data/OnionHEN/sprx/onion_example.log";

static void send_notification(const char *msg) {
  struct SceNotificationRequest req;
  zero_mem(&req, sizeof(req));
  req.target_id = -1;
  append_str(req.message, 0, sizeof(req.message), msg);
  (void)sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void log_line(const char *msg) {
  char line[256];
  size_t len = 0;
  uint64_t now_us = sceKernelGetProcessTime();

  len = append_str(line, len, sizeof(line), "[onion_example t=");
  len = append_u64(line, len, sizeof(line), now_us / 1000ULL);
  len = append_str(line, len, sizeof(line), "ms] ");
  len = append_str(line, len, sizeof(line), msg);
  len = append_str(line, len, sizeof(line), "\n");

  (void)sceKernelDebugOutText(0, line);

  int fd = sceKernelOpen(kLogPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND,
                         0666);
  if (fd >= 0) {
    (void)sceKernelWrite(fd, line, len);
    (void)sceKernelClose(fd);
  }
}

/* ---- Background worker thread ---- */

static volatile int g_stop = 0;
static ScePthread g_worker = NULL;

static void *worker_thread(void *arg) {
  int pid = (int)(intptr_t)arg;

  for (int tick = 1; tick <= 3 && !g_stop; ++tick) {
    /* Sleep 5 seconds in 100 ms slices so module_stop() exits promptly. */
    for (int i = 0; i < 50 && !g_stop; ++i)
      (void)sceKernelUsleep(100000u);
    if (g_stop)
      break;

    char msg[160];
    size_t pos = 0;
    pos = append_str(msg, pos, sizeof(msg), "worker heartbeat #");
    pos = append_i64(msg, pos, sizeof(msg), tick);
    pos = append_str(msg, pos, sizeof(msg), " alive in pid=");
    pos = append_i64(msg, pos, sizeof(msg), pid);
    log_line(msg);
  }

  log_line("worker thread exiting cleanly");
  return NULL;
}

/* ---- PRX lifecycle entry points ---- */

__attribute__((visibility("hidden")))
int module_start(size_t argc, const void *argp) {
  (void)argc;
  (void)argp;

  int pid = getpid();
  g_stop = 0;

  char msg[192];
  size_t pos = 0;
  pos = append_str(msg, pos, sizeof(msg), "OnionHEN Example PRX loaded!\nPID: ");
  pos = append_i64(msg, pos, sizeof(msg), pid);
  send_notification(msg);

  pos = 0;
  pos = append_str(msg, pos, sizeof(msg), "module_start ok pid=");
  pos = append_i64(msg, pos, sizeof(msg), pid);
  log_line(msg);

  int rc = scePthreadCreate(&g_worker, NULL, worker_thread,
                            (void *)(intptr_t)pid, "OnionExamplePrx");
  if (rc != 0) {
    pos = 0;
    pos = append_str(msg, pos, sizeof(msg), "scePthreadCreate failed rc=");
    pos = append_i64(msg, pos, sizeof(msg), rc);
    log_line(msg);
    g_worker = NULL;
  }

  return 0; /* SCE_OK */
}

__attribute__((visibility("hidden")))
int module_stop(size_t argc, const void *argp) {
  (void)argc;
  (void)argp;

  g_stop = 1;
  if (g_worker) {
    (void)scePthreadJoin(g_worker, NULL);
    g_worker = NULL;
  }
  log_line("module_stop ok");
  return 0;
}
