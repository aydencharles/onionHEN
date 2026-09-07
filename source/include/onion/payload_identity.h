/* Stable per-file identity for user Payload runtime state. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "p" followed by a 64-bit FNV-1a digest and a NUL terminator. */
#define ONION_PAYLOAD_IDENTITY_SIZE 18

static inline bool onion_payload_identity_from_path(const char *path,
                                                    char *out,
                                                    size_t out_sz) {
  if (!path || !path[0] || !out || out_sz < ONION_PAYLOAD_IDENTITY_SIZE)
    return false;

  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
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
