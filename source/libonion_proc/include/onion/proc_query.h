/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Portable process lookup (sysctl allproc) shared by util / daemon / shellui /
 * bootstrapper. Complements the kernel allproc helpers in onion/proc.h.
 */

#pragma once

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef ONION_PROC_PROCESS_NAME_LEN
#define ONION_PROC_PROCESS_NAME_LEN 128
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Exact match on kinfo_proc.ki_comm. Returns pid or -1. */
pid_t onion_find_pid(const char *name);

/** Substring match on ki_comm. Returns first match or -1. */
pid_t onion_find_pid_substr(const char *substr);

/**
 * Extended lookup used by ShellUI toolbox / cheats / daemon inject.
 * @param needle     if true, substring match process name; else exact.
 * @param for_bigapp if true, match running big-app (requires host SCE helpers
 *                   via onion_proc_set_sce_hooks).
 * Pass name="" + for_bigapp=true for "any process of the running BigApp"
 * (daemon get_game_pid). Call onion_resolve_running_bigapp when the unique
 * main process identity is required.
 */
pid_t onion_find_pid_ex(const char *name, bool needle, bool for_bigapp);

/** True if pid still exists (KERN_PROC_PID). */
bool onion_proc_is_alive(pid_t pid);

typedef int (*onion_get_process_name_fn)(int pid, char *name);
typedef int (*onion_get_app_info_fn)(pid_t pid, void *app_info /* app_info_t */);
typedef int (*onion_get_bigapp_id_fn)(void);

typedef struct onion_bigapp_process {
  pid_t pid;
  int appid;
  char process_name[ONION_PROC_PROCESS_NAME_LEN];
  uint64_t session_generation;
} onion_bigapp_process_t;

void onion_proc_set_sce_hooks(onion_get_process_name_fn get_name,
                              onion_get_app_info_fn get_app_info,
                              onion_get_bigapp_id_fn get_bigapp);

/** Resolve the unique main process of the running BigApp. Returns -2 when
 * multiple equally plausible processes remain. */
int onion_resolve_running_bigapp(onion_bigapp_process_t *out);

/* Historical name used across the tree. */
pid_t find_pid(const char *name);

#ifdef __cplusplus
} // extern "C"

inline bool isProcessAlive(int pid) noexcept {
  return onion_proc_is_alive(static_cast<pid_t>(pid));
}
#else
static inline bool isProcessAlive(pid_t pid) { return onion_proc_is_alive(pid); }
#endif
