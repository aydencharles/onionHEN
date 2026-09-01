/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * PS4 BC frame-rate hook. The daemon patches a remote detour into the GNM
 * flip submission of a running PS4 backwards-compatibility title
 * (sceGnmSubmitAndFlipCommandBuffersForWorkload) through libhijacker kernel
 * primitives. No ELF is ever injected into the game.
 */
#pragma once

#include <cstdint>
#include <sys/types.h>

namespace onion {
namespace fps {

enum class BcHookStatus : uint8_t {
  NotAttempted,
  Ok,
  ProcessGone,
  HijackFailed,
  GnmDriverNotFound,
  SymbolNotFound,
  DisasmFailed,
  AllocFailed,
  MprotectFailed,
  WriteFailed,
  ReadFailed,
};

const char *bc_hook_status_name(BcHookStatus status);

class BcGnmHook {
public:
  BcGnmHook() = default;
  ~BcGnmHook();
  BcGnmHook(const BcGnmHook &) = delete;
  BcGnmHook &operator=(const BcGnmHook &) = delete;

  /* Install the flip detour into `pid`. Safe to retry after a failure. */
  BcHookStatus install(pid_t pid);

  /* Tear the hook state down (the in-game patch itself stays until exit). */
  void reset();

  bool installed() const { return installed_; }

  /* Read the flip counter incremented by the detour. */
  bool sample(uint64_t *count, BcHookStatus *status = nullptr);

  pid_t pid() const { return pid_; }
  uint64_t target_addr() const { return target_va_; }
  uint64_t counter_addr() const { return counter_va_; }
  uint32_t hook_len() const { return hook_len_; }

private:
  struct Impl;

  Impl *impl_ = nullptr;
  pid_t pid_ = -1;
  uint64_t target_va_ = 0;
  uint64_t counter_va_ = 0;
  uint32_t hook_len_ = 0;
  bool installed_ = false;
};

} // namespace fps
} // namespace onion
