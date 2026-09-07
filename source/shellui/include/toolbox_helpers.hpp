/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure ShellUI helpers (host-testable, no Mono/PS5 I/O).
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace toolbox {

/**
 * UI-facing path: strip "/user" mount prefix, map "/usb*" → "/mnt/usb*".
 * Only exact "/user" or paths under "/user/…" (not "/userdata").
 */
inline std::string display_path_for_ui(const std::string &path) {
  if (path == "/user")
    return {};
  if (path.rfind("/user/", 0) == 0)
    return path.substr(5); /* keep leading '/' of remainder */
  if (path.rfind("/usb", 0) == 0)
    return "/mnt" + path;
  return path;
}

/**
 * True for payload basenames: must end with ".elf" and have a non-empty stem.
 * Rejects .auto_start markers and bare ".elf".
 */
inline bool is_payload_elf_name(const char *name) {
  if (!name || !name[0])
    return false;
  if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
    return false;
  if (std::strstr(name, ".auto_start") != nullptr)
    return false;
  const std::size_t n = std::strlen(name);
  if (!(n > 4 && std::strcmp(name + (n - 4), ".elf") == 0))
    return false;
  return (n - 4) > 0;
}

/**
 * Previous builds left runtime staging files in the visible /data payload
 * directory. Keep their reserved identity format out of the list after upgrade.
 */
inline bool is_legacy_payload_staging_name(const char *name) {
  if (!name || std::strlen(name) != 21 || name[0] != 'p' ||
      std::strcmp(name + 17, ".elf") != 0)
    return false;
  for (std::size_t i = 1; i < 17; ++i) {
    const char c = name[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  }
  return true;
}

/**
 * Launch/PID key for payload: "foo.elf" → "foo".
 * Matches util onion_payload_elf_key_from_name.
 */
inline bool elf_key_from_name(const char *name, char *out, std::size_t out_sz) {
  if (!name || !out || out_sz < 2)
    return false;
  const char *base = std::strrchr(name, '/');
  base = base ? base + 1 : name;
  if (!base[0] || std::strcmp(base, ".") == 0 || std::strcmp(base, "..") == 0)
    return false;
  std::size_t n = std::strlen(base);
  if (n >= 4 && std::strcmp(base + n - 4, ".elf") == 0)
    n -= 4;
  if (n == 0)
    return false;
  if (n >= out_sz)
    n = out_sz - 1;
  std::memcpy(out, base, n);
  out[n] = '\0';
  return out[0] != '\0';}

} // namespace toolbox
