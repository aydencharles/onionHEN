/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Recursive directory helpers: create a path together with its missing
 * parents, and delete a whole tree (optionally reporting progress).
 *
 * Single-path and single-file helpers (if_exists / touch_file /
 * write_file_atomic / read_file_alloc) live in onion/fs.h.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create path and any missing parents (mode 0777). True if the directory exists. */
bool mkdir_tree(const char *path);

/** Recursively delete directory tree at path. */
bool rmtree(const char *path);

typedef void (*onion_fs_progress_fn)(size_t completed, size_t total,
                                     void *user);

/** Recursively delete a tree and report completed entries, including dirs. */
bool rmtree_with_progress(const char *path, onion_fs_progress_fn progress,
                          void *user);

#ifdef __cplusplus
}
#endif
