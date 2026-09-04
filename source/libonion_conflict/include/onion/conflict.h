/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Strategy table of foreign payload process names. A live exact match on
 * ki_comm / ki_tdname means that family is already running; OnionHEN must
 * refuse to continue its own load chain.
 */
#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef pid_t (*onion_conflict_find_pid_fn)(const char *name);

typedef struct OnionConflictStrategy {
  const char *family;
  const char *const *proc_names; /* NULL-terminated; exact names only */
} OnionConflictStrategy;

const OnionConflictStrategy *onion_conflict_strategies(size_t *out_count);

/**
 * Walk @strategies in order. First family with find_pid(name) > 0 wins.
 * NULL find_pid, missing names, and pid <= 0 are not hits (fail-open).
 */
const char *onion_conflict_scan(const OnionConflictStrategy *strategies,
                                size_t count,
                                onion_conflict_find_pid_fn find_pid);

/** Scan the compiled-in strategy table with @find_pid. */
const char *onion_conflict_detect_with(onion_conflict_find_pid_fn find_pid);

/** Scan with onion_find_pid. Host tests compile this as a no-op. */
const char *onion_conflict_detect(void);

#ifdef __cplusplus
}
#endif
