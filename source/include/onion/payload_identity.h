/* Stable per-file identity for user Payload runtime state. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* "p" followed by a 64-bit FNV-1a digest and a NUL terminator. */
#define ONION_PAYLOAD_IDENTITY_SIZE 18
#define ONION_PAYLOAD_PATH_SIZE 1024

/* ShellUI sandbox aliases must identify the same file as the private loader. */
static inline bool onion_payload_canonical_path(const char *path, char *out,
                                                size_t out_sz) {
  if (!path || path[0] != '/' || !out || !out_sz)
    return false;
  if (strncmp(path, "/user/data/", 11) == 0)
    path += 5;
  const bool usb = strncmp(path, "/usb", 4) == 0 &&
                   path[4] >= '0' && path[4] <= '3' && path[5] == '/';
  const size_t prefix = usb ? 4 : 0;
  const size_t length = strlen(path);
  if (prefix + length >= out_sz)
    return false;
  if (usb)
    memcpy(out, "/mnt", prefix);
  memcpy(out + prefix, path, length + 1);
  return true;
}

static inline bool onion_payload_identity_from_path(const char *path,
                                                    char *out,
                                                    size_t out_sz) {
  if (!path || !path[0] || !out || out_sz < ONION_PAYLOAD_IDENTITY_SIZE)
    return false;

  char canonical[ONION_PAYLOAD_PATH_SIZE];
  if (!onion_payload_canonical_path(path, canonical, sizeof(canonical)))
    return false;

  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char *p = (const unsigned char *)canonical; *p; ++p) {
    hash ^= *p;
    hash *= UINT64_C(1099511628211);
  }

  static const char kHex[] = "0123456789abcdef";
  out[0] = 'p';
  for (size_t i = 0; i < 16; ++i)
    out[i + 1] = kHex[(hash >> ((15 - i) * 4)) & 0x0f];
  out[17] = '\0';
  return true;
}
