/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Payload ELF load helpers (PID files + elfldr socket).
 * User payloads are bare .elf files. Plugin ELFs are managed separately under
 * /data/OnionHEN/plugins by libonion_plugin_manager.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool onion_payload_is_elf(const void *buf, size_t size);

/**
 * Derive launch/PID key from basename: "foo.elf" → "foo".
 * Rejects empty stem (".elf") and path separators.
 */
bool onion_payload_elf_key_from_name(const char *name, char *out, size_t out_sz);

void onion_payload_pid_path(char *out, size_t out_sz, const char *title_id);
pid_t onion_payload_read_pid_file(const char *pid_path);
/** Persist only a real payload PID (>1); otherwise remove the marker. */
void onion_payload_write_pid_file(const char *pid_path, pid_t pid);

/**
 * True when the PID file for @title_id names a still-live user Payload.
 * Invalid and dead PID records are cleared without terminating any process.
 */
bool onion_payload_running(const char *title_id);

/**
 * Stage ELF under /data/OnionHEN/payloads/<key>.elf and launch exclusively via
 * OnionHEN's private 9020 elfldr. User payloads never fall back to the external
 * 9021 bootstrap/recovery loader and never infer a PID from process snapshots.
 *
 * Returns the loader-reported PID (>1) on success or -1 on every failure,
 * including an unavailable loader, protocol timeout, or missing/invalid PID.
 */
pid_t onion_payload_launch_elfldr(const char *title_id, const uint8_t *elf,
                                  size_t elf_sz);

/** malloc'd file contents; caller free(). NULL on error. */
uint8_t *onion_payload_read_file(const char *path, size_t *out_size);

/**
 * Load a payload .elf from @path.
 * @filename Optional basename. If NULL, derived from @path.
 * A live recorded instance is preserved and also returns true. Otherwise true
 * requires the private loader to report a real PID (>1) and persist it.
 */
bool onion_payload_load(const char *path, const char *filename);

#ifdef __cplusplus
}
#endif
