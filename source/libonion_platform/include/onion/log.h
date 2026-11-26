/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * OnionHEN logging.
 *
 * Sinks: klog (volatile, read live from a PC) + an optional bounded file.
 * stdout is also written when the process has one.
 *
 * Two independent gates decide whether a record is emitted:
 *
 *   compile-time  ONION_LOG_COMPILE_LEVEL — records above this level generate
 *                 no code at all. The level test is a constant expression, so
 *                 the arguments are never even evaluated: a
 *                 LOG_TRACE("%s", expensive()) costs nothing in a release
 *                 build rather than costing a call whose result is discarded.
 *
 *   run-time      onion_log_set_level() — lets a user raise verbosity on a
 *                 retail unit from config.ini without reflashing a payload.
 *                 Only meaningful for levels that survived the compile gate.
 *
 * Threading: onion_log_write() is safe to call from any thread. It is NOT
 * async-signal-safe (it takes a lock and formats into a shared buffer); a
 * fault handler must use onion_log_emergency() instead.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ONION_LOG_OFF = 0,
  ONION_LOG_ERROR = 1, /* the operation failed and the user is affected */
  ONION_LOG_WARN = 2,  /* recovered, degraded, or a retry is coming */
  ONION_LOG_INFO = 3,  /* lifecycle milestones: started, injected, loaded */
  ONION_LOG_DEBUG = 4, /* decisions and state transitions, for bug reports */
  ONION_LOG_TRACE = 5, /* per-item / per-frame detail; noisy by design */
  /*
   * Not a level. Without a negative enumerator the compiler is free to give
   * this enum an unsigned underlying type, and a negative value cast into it
   * would wrap to a huge positive one — clamping "too low" up to TRACE, the
   * loudest setting, instead of down to OFF.
   */
  ONION_LOG__FORCE_SIGNED = -1,
} onion_log_level;

/*
 * Highest level compiled in. Release keeps DEBUG so a user can temporarily
 * raise verbosity from the Toolbox without installing a different payload;
 * TRACE — the noisiest per-item/per-frame detail — remains debug-build only.
 *
 * Override per target with -DONION_LOG_COMPILE_LEVEL=ONION_LOG_DEBUG.
 */
#ifndef ONION_LOG_COMPILE_LEVEL
#ifdef NDEBUG
#define ONION_LOG_COMPILE_LEVEL ONION_LOG_DEBUG
#else
#define ONION_LOG_COMPILE_LEVEL ONION_LOG_TRACE
#endif
#endif

/*
 * Verbosity a process starts at, before it applies its own settings.
 *
 * A debug build defaults to the noisiest level it can compile in: a payload
 * built to collect a bug report should not need anyone to edit config.ini
 * first, and the reporter is often not the person who built it. Release stays
 * at INFO so a normal payload is not chatty — a user can still raise it from
 * the Toolbox, because Release compiles DEBUG in.
 *
 * Override per target with -DONION_LOG_DEFAULT_LEVEL=ONION_LOG_<level>.
 */
#ifndef ONION_LOG_DEFAULT_LEVEL
#ifdef NDEBUG
#define ONION_LOG_DEFAULT_LEVEL ONION_LOG_INFO
#else
#define ONION_LOG_DEFAULT_LEVEL ONION_LOG_COMPILE_LEVEL
#endif
#endif

/*
 * Default file rotation policy: 768 KiB live log + three generations, i.e. a
 * 3 MiB budget for the whole history.
 *
 * The budget is what matters, not the per-file size. At TRACE a busy daemon can
 * write megabytes in seconds, so a tighter cap evicts the startup records — the
 * ones a bug report actually needs — before anyone reads them. 3 MiB is still
 * negligible on /data while giving TRACE runs enough room to keep their
 * beginning.
 */
#define ONION_LOG_DEFAULT_MAX_BYTES (768u * 1024u)
#define ONION_LOG_DEFAULT_ROTATE_COUNT 3u

/**
 * Crash sink bound. The crash file is append-only with no rotation, so without
 * a cap it is the one log that grows without limit on a console the user cannot
 * clean out. Once it is past this size the next process to configure the sink
 * moves it aside to `<path>.1` instead of appending to it.
 */
#define ONION_LOG_DEFAULT_CRASH_MAX_BYTES (1024u * 1024u)

/*
 * Current runtime threshold. Read directly by the macros so the common
 * "record is disabled" path is a load and a compare, with no call.
 * Deliberately not atomic: a racing set_level may cost one record its old
 * verdict, which is harmless, and it keeps the hot path free of barriers.
 */
extern volatile int onion_log_runtime_level;

/**
 * Configure tag and optional file sink. Pass NULL path to disable the file.
 *
 * The path may be shared by several payload processes (the daemon owns
 * OnionHEN.log; ShellUI and the bootstrapper append to the same file). Each
 * process must call this itself — the sink state is per-process, so a process
 * that never calls it writes to klog and stdout only.
 *
 * Sharing a file is safe because a record is emitted with exactly one write()
 * on an O_APPEND descriptor, which makes the append atomic against the other
 * writers. The tail of a short write is deliberately never retried: a second
 * write() could land after a peer's record and tear this one across it, and a
 * truncated record is easier to read than an interleaved one.
 *
 * Rotation is serialised across processes with a `<path>.lock` sidecar, so a
 * shared path keeps a coherent `.1`/`.2`/`.3` history instead of letting two
 * writers interleave their rename chains. The lock is taken non-blocking — a
 * peer caught mid-rotation makes this process skip its turn rather than stall
 * every thread that logs, since the caller holds the process-wide sink lock.
 * The sidecar is created next to the log on first rotation and is intentionally
 * left on disk; it carries no state beyond the kernel lock.
 */
void onion_log_configure(const char *tag, const char *log_path);

/**
 * Configure the append-only crash sink used by onion_log_emergency().
 * Existing contents are preserved across process restarts. Pass NULL to
 * disable it.
 *
 * This sink has no rotation, so it is bounded instead: if the file already
 * exceeds ONION_LOG_DEFAULT_CRASH_MAX_BYTES it is moved aside to `<path>.1`
 * (replacing any previous one) before this process starts appending. Growth
 * *within* one session is not capped — the writer is a fault handler and has
 * to stay async-signal-safe.
 */
void onion_log_configure_crash(const char *crash_path);

/** Bound the file sink. 0 restores ONION_LOG_DEFAULT_MAX_BYTES. */
void onion_log_set_max_bytes(size_t max_bytes);

void onion_log_set_level(onion_log_level level);
onion_log_level onion_log_get_level(void);

/** "off"/"error"/"warn"/"info"/"debug"/"trace", case-insensitive. */
bool onion_log_level_from_name(const char *name, onion_log_level *out);
const char *onion_log_level_name(onion_log_level level);

/** Emit a record. Prefer the LOG_* macros, which apply both gates first. */
void onion_log_write(onion_log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Fault-handler path: formats onto the stack and writes straight to the normal
 * and crash sinks with no locking and no allocation, so it stays usable when
 * the process is already dying and when the log lock may be held by the
 * faulting thread. Bypasses both gates — a crash is always worth recording.
 */
void onion_log_emergency(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/** Close the file sink. */
void onion_log_shutdown(void);

/*
 * The compile-time test comes first and is a constant expression, so for a
 * disabled level the whole statement — including the arguments — is discarded
 * before the runtime load is ever reached.
 *
 * There is deliberately no second backend for the -nostdlib libraries
 * (NidResolver, NineS, onion_elfldr). Those flags only stop the static
 * library itself from pulling in stdlib at its own link step; every one of
 * them is ultimately linked into a host that has full libc and pthread, which
 * is why they already call vsnprintf today. Giving them their own logger
 * would recreate exactly the split this replaced.
 */
#define ONION_LOG_AT(level, ...)                                               \
  do {                                                                         \
    if ((level) <= ONION_LOG_COMPILE_LEVEL &&                                  \
        (int)(level) <= onion_log_runtime_level) {                             \
      onion_log_write((level), __VA_ARGS__);                                   \
    }                                                                          \
  } while (0)

#define LOG_ERROR(...) ONION_LOG_AT(ONION_LOG_ERROR, __VA_ARGS__)
#define LOG_WARN(...) ONION_LOG_AT(ONION_LOG_WARN, __VA_ARGS__)
#define LOG_INFO(...) ONION_LOG_AT(ONION_LOG_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) ONION_LOG_AT(ONION_LOG_DEBUG, __VA_ARGS__)
#define LOG_TRACE(...) ONION_LOG_AT(ONION_LOG_TRACE, __VA_ARGS__)


#ifdef __cplusplus
}
#endif
