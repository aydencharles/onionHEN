/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Minimal multipart/form-data header parsing (host-clean). Used by the
 * vendored DPI package server for its web UI upload endpoint and unit-tested
 * on the host.
 */
#ifndef PKGSERVER_MULTIPART_H
#define PKGSERVER_MULTIPART_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Extract the boundary token from a Content-Type header value.
 *  Returns 0 on success, -1 if absent or malformed. */
int pkgserver_multipart_boundary(const char *content_type, char *out,
                                 size_t cap);

/** Extract the filename from a Content-Disposition part header.
 *  Handles quoted and unquoted values; returns 0 on success, -1 otherwise. */
int pkgserver_multipart_filename(const char *disposition, char *out,
                                 size_t cap);

#ifdef __cplusplus
}
#endif

#endif
