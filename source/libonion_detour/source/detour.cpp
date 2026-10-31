/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <onion/detour.h>
#include <onion/log.h>
#include <onion/hotpatch.h>

#include <cstdint>
#include <cstring>
#include <cstdlib>

#include <onion/ipc_client.hpp>
#include <onion/trampoline_arena.hpp>
#include <onion/x64_relocator.h>

#include <machine/param.h>
#include <sys/mman.h>
#include <ps5/kernel.h>

/*
 * Host (shellui) exports sceKernelMprotect as a *function pointer*
 * filled by dlsym — not as a real code symbol. Declaring it as
 *   extern "C" int sceKernelMprotect(...);
 * makes the linker resolve CALL to the data object (.bss). Executing the
 * pointer bytes as instructions → SIGILL right after "Hooking …".
 *
 * be62388 Detour lived in shellui and called through that pointer via
 * external_symbols.hpp; keep the same shape here.
 */
extern int (*sceKernelMprotect)(void *addr, size_t len, int prot)
    __attribute__((weak));

// Optional: host sets true when userland mprotect works (kstuff HV path).
extern bool has_hv_bypass __attribute__((weak));

namespace {

constexpr int kProtRwx = PROT_EXEC | PROT_READ | PROT_WRITE;
constexpr size_t kMaxX64InstructionLength = 15;
/*
 * hde64_disasm may inspect a complete 15-byte instruction when the final
 * instruction starts at HOOK_LENGTH - 1. PS5 xotext is execute-only, so every
 * page in that decoder window must be made readable before relocation starts.
 */
constexpr size_t kRelocationReadLength =
    HOOK_LENGTH + kMaxX64InstructionLength - 1;

struct alignas(16) AtomicPatchBlock {
  uint64_t low;
  uint64_t high;
};

static_assert(sizeof(AtomicPatchBlock) == ONION_X64_ATOMIC_PATCH_SIZE);

uintptr_t page_align_down(uintptr_t addr) {
  return addr & ~static_cast<uintptr_t>(PAGE_MASK);
}

int mprotect_user(void *addr, size_t len) {
  if (sceKernelMprotect == nullptr) {
    return -1;
  }
  return sceKernelMprotect(addr, len, kProtRwx);
}

int mprotect_rwx(void *addr, size_t len) {
  if (!addr || len == 0) {
    return -1;
  }
  /* Same policy as be62388 shellui Detour, plus kernel fallback on failure. */
  const bool hv = (&has_hv_bypass != nullptr) && has_hv_bypass;
  if (hv) {
    if (mprotect_user(addr, len) >= 0) {
      return 0;
    }
    /* Probe succeeded earlier; still fall back if this page rejects userland. */
  } else if (mprotect_user(addr, len) >= 0) {
    return 0;
  }
  return kernel_mprotect(getpid(), reinterpret_cast<uint64_t>(addr), len,
                         kProtRwx);
}

/** Make the page(s) covering [addr, addr+len) RWX. */
int mprotect_range_rwx(uintptr_t addr, size_t len) {
  const uintptr_t start = page_align_down(addr);
  const uintptr_t end = page_align_down(addr + len - 1) + PAGE_SIZE;
  return mprotect_rwx(reinterpret_cast<void *>(start),
                      static_cast<size_t>(end - start));
}

bool protect_trampoline_arena(void *address, size_t length) {
  return mprotect_rwx(address, length) == 0;
}

/* Process-lifetime storage; individual trampoline slots are never unmapped. */
onion::TrampolineArena g_trampoline_arena(protect_trampoline_arena);

bool compare_exchange_patch(uintptr_t address,
                            const onion_x64_atomic_patch &patch) {
  if ((address % alignof(AtomicPatchBlock)) != 0) {
    return false;
  }

  AtomicPatchBlock expected{};
  AtomicPatchBlock desired{};
  std::memcpy(&expected, patch.expected, sizeof(expected));
  std::memcpy(&desired, patch.desired, sizeof(desired));

  unsigned char exchanged = 0;
  auto *target = reinterpret_cast<volatile AtomicPatchBlock *>(address);
  __asm__ volatile("lock; cmpxchg16b %1; sete %0"
                   : "=q"(exchanged), "+m"(*target), "+a"(expected.low),
                     "+d"(expected.high)
                   : "b"(desired.low), "c"(desired.high)
                   : "cc", "memory");
  return exchanged != 0;
}

} // namespace

void ReadMemory(uint64_t address, void *buffer, int length) {
  memcpy(buffer, reinterpret_cast<void *>(address), length);
}

void WriteMemory(uint64_t address, void *buffer, int length) {
  memcpy(reinterpret_cast<void *>(address), buffer, length);
}

void PatchInJump(uint64_t address, void *destination) {
  if (!address || !destination) {
    return;
  }
  (void)DetourFunction(address, destination);
}

bool PrepareDetour(uint64_t address, void *destination, DetourHandle *handle) {
  return PrepareDetour(address, destination, handle, true);
}

bool PrepareDetour(uint64_t address, void *destination, DetourHandle *handle,
                   bool log_failure) {
  if (!address || !destination || !handle) {
    return false;
  }

  *handle = DetourHandle{};
  LOG_DEBUG("Hooking %#02lx => %p", address, destination);

  if ((address % ONION_X64_ATOMIC_PATCH_SIZE) != 0) {
    LOG_DEBUG("PrepareDetour: target is not 16-byte aligned: %#02lx", address);
    return false;
  }

  const size_t stubLength = ONION_X64_TRAMPOLINE_CAPACITY;
  void *executableAddress =
      g_trampoline_arena.allocate_near(stubLength, address);
  if (!executableAddress) {
    LOG_DEBUG("DetourFunction: no unique near trampoline for %#02lx", address);
    return false;
  }
  if (!g_trampoline_arena.owns(executableAddress)) {
    LOG_DEBUG("DetourFunction: allocator returned an unowned trampoline");
    return false;
  }

  /* PS5 xotext is execute-only. Never decode/copy it before this succeeds. */
  if (mprotect_range_rwx(address, kRelocationReadLength) < 0) {
    if (log_failure) {
      LOG_ERROR("DetourFunction: failed to mprotect target decoder window");
    } else {
      LOG_DEBUG("DetourFunction: failed to mprotect target decoder window");
    }
    return false;
  }

  onion_x64_relocate_result relocation{};
  if (!onion_x64_relocate(
          reinterpret_cast<const uint8_t *>(address), address,
          reinterpret_cast<uint8_t *>(executableAddress),
          reinterpret_cast<uintptr_t>(executableAddress), HOOK_LENGTH,
          stubLength, &relocation)) {
    if (log_failure) {
      LOG_ERROR("DetourFunction: relocation failed at +%zu: %s",
                relocation.error_offset,
                onion_x64_relocate_error_string(relocation.error));
    } else {
      LOG_DEBUG("DetourFunction: relocation failed at +%zu: %s",
                relocation.error_offset,
                onion_x64_relocate_error_string(relocation.error));
    }
    return false;
  }

  onion_x64_atomic_patch patch{};
  if (!onion_x64_build_atomic_patch(
          address, reinterpret_cast<const uint8_t *>(address),
          reinterpret_cast<uintptr_t>(destination), &patch)) {
    if (log_failure) {
      LOG_ERROR("PrepareDetour: failed to build atomic patch");
    } else {
      LOG_DEBUG("PrepareDetour: failed to build atomic patch");
    }
    return false;
  }

  handle->target = address;
  handle->destination = destination;
  handle->trampoline = executableAddress;
  handle->trampoline_capacity = stubLength;
  handle->stolen_size = relocation.source_size;
  handle->emitted_size = relocation.trampoline_size;
  handle->patch = patch;
  handle->prepared = true;
  return true;
}

bool CommitDetour(DetourHandle *handle) {
  return CommitDetour(handle, true);
}

bool CommitDetour(DetourHandle *handle, bool log_failure) {
  if (!handle || !handle->prepared || handle->committed) {
    return false;
  }

  if (!compare_exchange_patch(handle->target, handle->patch)) {
    if (log_failure) {
      LOG_ERROR("CommitDetour: target changed or atomic patch failed: %#02lx",
                handle->target);
    } else {
      LOG_DEBUG("CommitDetour: target changed or atomic patch failed: %#02lx",
                handle->target);
    }
    return false;
  }

  handle->committed = true;

  LOG_DEBUG(
      "DetourFunction: target=%#02lx hook=%p trampoline=%p stolen=%zu emitted=%zu",
      handle->target, handle->destination, handle->trampoline,
      handle->stolen_size, handle->emitted_size);
  return true;
}

void AbortDetour(DetourHandle *handle) {
  if (!handle) {
    return;
  }
  /* Arena slots are process-lifetime allocations; never unmap one slot. */
  *handle = DetourHandle{};
}

bool InstallDetour(uint64_t address, void *destination,
                   void **original_storage) {
  return InstallDetour(address, destination, original_storage, true);
}

bool InstallDetour(uint64_t address, void *destination,
                   void **original_storage, bool log_failure) {
  DetourHandle handle{};
  if (!PrepareDetour(address, destination, &handle, log_failure)) {
    return false;
  }

  /* The locked CommitDetour operation publishes this earlier pointer write. */
  if (original_storage) {
    std::memcpy(original_storage, &handle.trampoline,
                sizeof(handle.trampoline));
  }

  if (!CommitDetour(&handle, log_failure)) {
    if (original_storage) {
      void *null_original = nullptr;
      std::memcpy(original_storage, &null_original, sizeof(null_original));
    }
    AbortDetour(&handle);
    return false;
  }
  return true;
}

void *DetourFunction(uint64_t address, void *destination) {
  return DetourFunction(address, destination, true);
}

void *DetourFunction(uint64_t address, void *destination, bool log_failure) {
  DetourHandle handle{};
  if (!PrepareDetour(address, destination, &handle, log_failure)) {
    return nullptr;
  }
  if (!CommitDetour(&handle, log_failure)) {
    AbortDetour(&handle);
    return nullptr;
  }
  return handle.trampoline;
}
