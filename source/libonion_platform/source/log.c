/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/log.h>

#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
/* flock(2) lives in <sys/file.h> on the host build; FreeBSD exposes it via
 * <fcntl.h>, which is what the PS5 target uses. */
#include <sys/file.h>
#endif

/* klog_printf provided by PS5 payload runtime / linked stubs. */
void klog_printf(const char *fmt, ...);

#define LOG_TAG_MAX 64
#define LOG_PATH_MAX 256
#define LOG_MSG_MAX 0x1000

/*
 * Records survive to the file sink only; klog and stdout are best-effort.
 * g_lock covers everything below it.
 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_tag[LOG_TAG_MAX] = "OnionHEN";
static char g_log_path[LOG_PATH_MAX] = "";
static int g_fd = -1;
static char g_crash_path[LOG_PATH_MAX] = "";
static int g_crash_fd = -1;
static size_t g_max_bytes = ONION_LOG_DEFAULT_MAX_BYTES;

/*
 * Cross-process guard for a shared log path. Several payload processes append
 * to the same file (the daemon owns it; ShellUI and the bootstrapper join), so
 * a rotation started by one of them must not interleave with another's rename
 * chain — that is what silently loses a generation. The lock lives on a
 * sidecar path so rotation never renames the file the lock is held on.
 */
typedef enum {
  ROT_LOCK_HELD,        /* we own it: rotate, then release */
  ROT_LOCK_BUSY,        /* a peer is rotating right now: skip this turn */
  ROT_LOCK_UNAVAILABLE, /* no sidecar to lock: rotate unlocked, as before */
} rotate_lock_state;

/* Consecutive busy turns before we conclude the peer is wedged, not slow. */
#define ROT_LOCK_BUSY_LIMIT 8u

static int g_rotate_lock_fd = -1;
static unsigned g_rotate_lock_busy_streak = 0;

volatile int onion_log_runtime_level = ONION_LOG_DEFAULT_LEVEL;

static const char *const kLevelNames[] = {"off",   "error", "warn",
                                          "info",  "debug", "trace"};

const char *onion_log_level_name(onion_log_level level) {
  if (level < ONION_LOG_OFF || level > ONION_LOG_TRACE) {
    return "?";
  }
  return kLevelNames[level];
}

bool onion_log_level_from_name(const char *name, onion_log_level *out) {
  if (name == NULL || out == NULL) {
    return false;
  }
  for (size_t i = 0; i < sizeof(kLevelNames) / sizeof(kLevelNames[0]); i++) {
    const char *want = kLevelNames[i];
    size_t j = 0;
    for (; want[j] != '\0' && name[j] != '\0'; j++) {
      char a = name[j];
      if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
      }
      if (a != want[j]) {
        break;
      }
    }
    if (want[j] == '\0' && name[j] == '\0') {
      *out = (onion_log_level)i;
      return true;
    }
  }
  return false;
}

void onion_log_set_level(onion_log_level level) {
  if (level < ONION_LOG_OFF) {
    level = ONION_LOG_OFF;
  } else if (level > ONION_LOG_TRACE) {
    level = ONION_LOG_TRACE;
  }
  onion_log_runtime_level = (int)level;
}

onion_log_level onion_log_get_level(void) {
  return (onion_log_level)onion_log_runtime_level;
}

/* --- file sink (caller holds g_lock) ------------------------------------- */

static void close_file_locked(void) {
  if (g_fd >= 0) {
    close(g_fd);
    g_fd = -1;
  }
}

static void close_crash_file_locked(void) {
  if (g_crash_fd >= 0) {
    close(g_crash_fd);
    g_crash_fd = -1;
  }
}

/*
 * The crash sink is append-only and shared by every payload process, so unlike
 * the main log it has no rotation to bound it — it is the one file here that
 * could fill /data. Keep a single previous generation: once the file is past
 * the cap, the next process to configure the sink moves it aside before
 * appending.
 *
 * This runs at configure time rather than in the writer because the writer is a
 * fault handler and has to stay async-signal-safe. Growth inside one session is
 * therefore still uncapped; that is the deliberate trade.
 */
static void rotate_crash_if_oversized_locked(void) {
  struct stat st;
  char backup[LOG_PATH_MAX + 8];

  if (g_crash_path[0] == '\0' || stat(g_crash_path, &st) != 0 ||
      st.st_size < 0 ||
      (uint64_t)st.st_size < (uint64_t)ONION_LOG_DEFAULT_CRASH_MAX_BYTES) {
    return;
  }
  if (snprintf(backup, sizeof(backup), "%s.1", g_crash_path) < 0) {
    return;
  }
  (void)unlink(backup);
  (void)rename(g_crash_path, backup);
}

static void open_file_locked(void) {
  if (g_log_path[0] == '\0') {
    return;
  }
  /* Keep the descriptor open: a log line should not cost open+close. O_CLOEXEC
   * keeps it out of any process this one spawns (elfldr_spawn rforks and then
   * execs, which would otherwise inherit the sink and the rotation lock). */
  g_fd = open(g_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0777);
}

static bool rotated_path(const char *base, unsigned index, char *out,
                         size_t out_size) {
  const int n = snprintf(out, out_size, "%s.%u", base, index);
  return n >= 0 && (size_t)n < out_size;
}

static bool lock_path(const char *base, char *out, size_t out_size) {
  const int n = snprintf(out, out_size, "%s.lock", base);
  return n >= 0 && (size_t)n < out_size;
}

static void close_rotate_lock_locked(void) {
  if (g_rotate_lock_fd >= 0) {
    (void)flock(g_rotate_lock_fd, LOCK_UN);
    close(g_rotate_lock_fd);
    g_rotate_lock_fd = -1;
  }
  g_rotate_lock_busy_streak = 0;
}

/*
 * Serialise rotation between the processes sharing the log.
 *
 * Non-blocking on purpose. The caller holds the process-wide sink lock, so
 * waiting on a peer here would stop every thread that logs — a peer parked in
 * a debugger or stuck on slow storage must not be able to freeze us. A busy
 * peer is already rotating, which is the outcome we wanted; the next record
 * retries.
 *
 * Two degradations keep the file bounded regardless:
 *   - no sidecar (read-only directory, unsupported filesystem): rotate
 *     unlocked, exactly the behaviour that predates the lock;
 *   - the lock stays busy for many records in a row: the peer is wedged rather
 *     than slow, so take over unlocked instead of growing the file forever.
 */
static rotate_lock_state rotate_lock_try_locked(void) {
  char path[LOG_PATH_MAX + 8];

  if (g_log_path[0] == '\0' || !lock_path(g_log_path, path, sizeof(path))) {
    return ROT_LOCK_UNAVAILABLE;
  }
  if (g_rotate_lock_fd < 0) {
    g_rotate_lock_fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0777);
    if (g_rotate_lock_fd < 0) {
      return ROT_LOCK_UNAVAILABLE;
    }
  }
  if (flock(g_rotate_lock_fd, LOCK_EX | LOCK_NB) == 0) {
    g_rotate_lock_busy_streak = 0;
    return ROT_LOCK_HELD;
  }
  return ROT_LOCK_BUSY;
}

static void rotate_lock_release_locked(void) {
  if (g_rotate_lock_fd >= 0) {
    (void)flock(g_rotate_lock_fd, LOCK_UN);
  }
}

/* Follow the configured path if it was replaced or removed externally. */
static void refresh_file_locked(void) {
  struct stat fd_st;
  struct stat path_st;

  if (g_fd < 0) {
    open_file_locked();
    return;
  }
  if (fstat(g_fd, &fd_st) == 0 && stat(g_log_path, &path_st) == 0 &&
      fd_st.st_dev == path_st.st_dev && fd_st.st_ino == path_st.st_ino) {
    return;
  }
  close_file_locked();
  open_file_locked();
}

/* Keep .1 as the newest backup and .3 as the oldest retained generation. */
static void rotate_locked(void) {
  char src[LOG_PATH_MAX + 16];
  char dst[LOG_PATH_MAX + 16];

  close_file_locked();
  for (unsigned index = ONION_LOG_DEFAULT_ROTATE_COUNT; index > 0; --index) {
    if (!rotated_path(g_log_path, index, dst, sizeof(dst))) {
      continue;
    }
    if (index == ONION_LOG_DEFAULT_ROTATE_COUNT) {
      (void)unlink(dst);
    }
    if (index == 1) {
      snprintf(src, sizeof(src), "%s", g_log_path);
    } else if (!rotated_path(g_log_path, index - 1, src, sizeof(src))) {
      continue;
    }
    (void)rename(src, dst);
  }
  open_file_locked();
}

static void rotate_if_needed_locked(size_t incoming) {
  struct stat st;

  if (g_max_bytes == 0) {
    return;
  }
  refresh_file_locked();
  if (g_fd < 0 || fstat(g_fd, &st) != 0 || st.st_size < 0) {
    return;
  }
  if ((uint64_t)st.st_size + (uint64_t)incoming < (uint64_t)g_max_bytes) {
    return;
  }

  /*
   * Rotation is due. Take the cross-process lock and re-check afterwards: a
   * peer that held it first may already have rotated, which leaves our
   * descriptor on the renamed backup and the live file small again.
   */
  const rotate_lock_state lock = rotate_lock_try_locked();
  if (lock == ROT_LOCK_BUSY) {
    if (++g_rotate_lock_busy_streak < ROT_LOCK_BUSY_LIMIT) {
      return; /* the peer is already shrinking the file; retry next record */
    }
    /* Not slow — wedged. Rotate unlocked rather than grow without limit, and
     * give the lock another chance on the next rotation. */
    g_rotate_lock_busy_streak = 0;
  }

  refresh_file_locked();
  if (g_fd >= 0 && fstat(g_fd, &st) == 0 && st.st_size >= 0 &&
      (uint64_t)st.st_size + (uint64_t)incoming >= (uint64_t)g_max_bytes) {
    rotate_locked();
  }
  if (lock == ROT_LOCK_HELD) {
    rotate_lock_release_locked();
  }
}

static void write_file_locked(const char *msg, size_t len) {
  rotate_if_needed_locked(len);
  if (g_fd < 0) {
    return;
  }

  /*
   * Exactly one write() per record. O_APPEND makes a single write atomic
   * against the other processes sharing this file, and only a single write:
   * retrying a short write would append the tail after whatever a peer wrote
   * in between, tearing this record across theirs. A short write therefore
   * loses its tail, but it keeps its line terminator so the next record still
   * starts on a fresh line and the log stays parseable line by line.
   */
  const ssize_t w = write(g_fd, msg, len);
  if (w < 0) {
    /* A failing sink must not wedge the caller; drop the record. */
    close_file_locked();
    return;
  }
  if ((size_t)w < len && w > 0 && msg[w - 1] != '\n') {
    (void)!write(g_fd, "\n", 1);
  }
}

/* --- formatting ---------------------------------------------------------- */

/** Monotonic seconds.milliseconds since boot; correlates events across logs. */
static void format_stamp(char *out, size_t out_size) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    snprintf(out, out_size, "%s", "----.---");
    return;
  }
  snprintf(out, out_size, "%llu.%03u", (unsigned long long)ts.tv_sec,
           (unsigned)(ts.tv_nsec / 1000000));
}

/**
 * Render one record: "[12345.678] [tag] LEVEL: message\n".
 * Returns the length written (never exceeds out_size - 1).
 */
static size_t format_record(char *out, size_t out_size, onion_log_level level,
                            const char *fmt, va_list args) {
  char stamp[32];
  format_stamp(stamp, sizeof(stamp));

  int n = snprintf(out, out_size, "[%s] [%s] %s: ", stamp, g_tag,
                   onion_log_level_name(level));
  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  size_t used = (size_t)n;
  if (used >= out_size) {
    used = out_size - 1;
  }

  n = vsnprintf(out + used, out_size - used, fmt, args);
  if (n > 0) {
    used += (size_t)n;
    if (used >= out_size) {
      used = out_size - 1; /* vsnprintf truncated; it still NUL-terminated */
    }
  }

  /* Exactly one trailing newline, whether or not the caller supplied one. */
  while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == '\r')) {
    used--;
  }
  if (used + 2 > out_size) {
    used = out_size - 2;
  }
  out[used++] = '\n';
  out[used] = '\0';
  return used;
}

/* --- public API ---------------------------------------------------------- */

void onion_log_configure(const char *tag, const char *log_path) {
  pthread_mutex_lock(&g_lock);
  if (tag && tag[0]) {
    snprintf(g_tag, sizeof(g_tag), "%s", tag);
  }
  close_file_locked();
  /* The lock is tied to the path, so a reconfigure must drop it too. */
  close_rotate_lock_locked();
  if (log_path && log_path[0]) {
    snprintf(g_log_path, sizeof(g_log_path), "%s", log_path);
    open_file_locked();
    /*
     * A sink that cannot be opened is the one failure the logger cannot report
     * through itself: every LOG_* below would go to klog and stdout only, and
     * the process looks like it never logged at all. Say so on klog directly —
     * safe here because log_emit() already calls klog_printf() under g_lock.
     *
     * This is how a process whose sandbox hides the path it was handed ends up
     * with no records in the shared file; without this line it is invisible.
     */
    if (g_fd < 0) {
      klog_printf("[%s] error: cannot open log sink %s: %s\n", g_tag,
                  g_log_path, strerror(errno));
    }
  } else {
    g_log_path[0] = '\0';
  }
  pthread_mutex_unlock(&g_lock);
}

void onion_log_configure_crash(const char *crash_path) {
  pthread_mutex_lock(&g_lock);
  close_crash_file_locked();
  if (crash_path && crash_path[0]) {
    snprintf(g_crash_path, sizeof(g_crash_path), "%s", crash_path);
    rotate_crash_if_oversized_locked();
    g_crash_fd =
        open(g_crash_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0777);
  } else {
    g_crash_path[0] = '\0';
  }
  pthread_mutex_unlock(&g_lock);
}

void onion_log_set_max_bytes(size_t max_bytes) {
  pthread_mutex_lock(&g_lock);
  g_max_bytes = (max_bytes == 0) ? ONION_LOG_DEFAULT_MAX_BYTES : max_bytes;
  pthread_mutex_unlock(&g_lock);
}

void onion_log_shutdown(void) {
  pthread_mutex_lock(&g_lock);
  close_file_locked();
  close_crash_file_locked();
  close_rotate_lock_locked();
  pthread_mutex_unlock(&g_lock);
}

/**
 * Format onto the caller's stack, then hold the lock only for the writes.
 * Keeping vsnprintf and clock_gettime outside the critical section matters:
 * the daemon logs from five threads, and a record must not serialise other
 * threads for the duration of its formatting.
 *
 * The lock still spans all three sinks so records never interleave.
 */
static void log_emit(onion_log_level level, const char *fmt, va_list args) {
  char msg[LOG_MSG_MAX];
  const size_t len = format_record(msg, sizeof(msg), level, fmt, args);

  pthread_mutex_lock(&g_lock);
  /* Volatile sinks first: if the file write fails the record is still seen. */
  klog_printf("%s", msg);
  (void)!write(STDOUT_FILENO, msg, len);
  write_file_locked(msg, len);
  pthread_mutex_unlock(&g_lock);
}

void onion_log_write(onion_log_level level, const char *fmt, ...) {
  va_list args;

  /*
   * Re-check the runtime gate: onion_log_write is public, and a caller that
   * bypassed the macros should not defeat the level.
   */
  if ((int)level > onion_log_runtime_level) {
    return;
  }

  va_start(args, fmt);
  log_emit(level, fmt, args);
  va_end(args);
}

void onion_log_emergency(const char *fmt, ...) {
  /*
   * No lock and no shared buffer: this runs from a fault handler, where the
   * faulting thread may already hold g_lock. Losing interleaving is a fair
   * trade for not deadlocking while recording a crash.
   */
  char msg[1024];
  va_list args;

  va_start(args, fmt);
  const size_t len = format_record(msg, sizeof(msg), ONION_LOG_ERROR, fmt, args);
  va_end(args);

  klog_printf("%s", msg);
  (void)!write(STDOUT_FILENO, msg, len);
  if (g_fd >= 0) {
    (void)!write(g_fd, msg, len);
  }
  if (g_crash_fd >= 0) {
    (void)!write(g_crash_fd, msg, len);
  }
}
