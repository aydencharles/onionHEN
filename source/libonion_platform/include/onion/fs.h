/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Single-path / single-file helpers: existence, create, atomic write, whole
 * file read. Recursive directory operations (mkdir_tree / rmtree) live in
 * onion/tree.h.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True if path exists (stat succeeds). */
bool if_exists(const char *path);

/** Create or truncate empty file at path (mode 0777). */
bool touch_file(const char *path);

/**
 * Write @data to @path via a sibling temp file, fsync, then rename.
 * Failure leaves any previous @path untouched. @data may be NULL when
 * @size is 0.
 */
bool write_file_atomic(const char *path, const void *data, size_t size);

/**
 * Read the whole file at @path into a fresh malloc'd buffer (caller free()).
 *
 * False, leaving *@out and *@out_size untouched, when @path is NULL or empty,
 * is not a regular file, is empty, is larger than @max_size, or cannot be read
 * or allocated. @max_size is a byte cap for the caller's own size policy; 0
 * means no limit. The size gate runs before the allocation, so a stale or
 * unrelated file cannot pull in more RAM than the caller accepts.
 *
 * The buffer is allocated with one spare byte and NUL-terminated at
 * [*out_size], so binary and text callers can share this one helper; *@out_size
 * is the file length, not the allocation size.
 *
 * Failure reasons are logged at DEBUG; the caller owns the user-facing message
 * because only it knows whether the file was optional.
 */
bool read_file_alloc(const char *path, size_t max_size, uint8_t **out,
                     size_t *out_size);

/**
 * read_file_alloc() for text: *@out is usable directly as a C string (the
 * spare byte above), so callers that parse the contents need no cast or copy.
 */
bool read_file_alloc_str(const char *path, size_t max_size, char **out,
                         size_t *out_size);

#ifdef __cplusplus
}
#endif
