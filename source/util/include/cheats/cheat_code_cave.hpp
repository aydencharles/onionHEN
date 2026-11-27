#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sys/types.h>

namespace onion::cheats {

/** PS5 logical page size used for mmap/mprotect alignment. */
constexpr uint64_t kCodeCavePageSize = 0x4000ULL;

/**
 * x86-64 hardware PTE leaf size. Touching each 4 KiB sub-page via pt_copyin
 * forces vm_fault to populate present PTEs so DMAP page-table walks
 * (KdirectBackend on FW >= 8.40) can immediately read/write the mapped cave.
 */
constexpr uint64_t kCodeCaveFaultStride = 0x1000ULL;

struct CodeCavePageRange {
  uint64_t start = 0;
  size_t length = 0;
};

inline bool computeCodeCavePageRange(uint64_t addr, size_t len,
                                     CodeCavePageRange &out) {
  out = {};
  if (len == 0 || addr > std::numeric_limits<uint64_t>::max() - len) {
    return false;
  }
  const uint64_t end = addr + len;
  if (end > std::numeric_limits<uint64_t>::max() - (kCodeCavePageSize - 1ULL)) {
    return false;
  }
  const uint64_t page_start = addr & ~(kCodeCavePageSize - 1ULL);
  const uint64_t page_end =
      (end + (kCodeCavePageSize - 1ULL)) & ~(kCodeCavePageSize - 1ULL);
  if (page_end <= page_start) {
    return false;
  }
  const uint64_t span = page_end - page_start;
  if (span > std::numeric_limits<size_t>::max()) {
    return false;
  }
  out.start = page_start;
  out.length = static_cast<size_t>(span);
  return true;
}

inline bool isZeroBytes(const uint8_t *bytes, size_t len) {
  if (bytes == nullptr || len == 0) {
    return false;
  }
  return std::all_of(bytes, bytes + len, [](uint8_t b) { return b == 0; });
}

/**
 * Policy: an unreadable direct patch may lazily map an anonymous code cave
 * during enable preflight only when its OFF bytes are all zeroes (the natural
 * state of an unmapped code cave) and it does not depend on live game code.
 */
inline bool canMapCodeCaveOnSnapshotFailure(bool enabling,
                                            bool allow_code_cave_reloc,
                                            bool already_relocated,
                                            bool restore_original,
                                            const uint8_t *off_bytes,
                                            size_t off_len) {
  return enabling && allow_code_cave_reloc && !already_relocated &&
         !restore_original && isZeroBytes(off_bytes, off_len);
}

struct CodeCaveMapOps {
  int (*attach)(pid_t pid, void *ctx) = nullptr;
  intptr_t (*mmap)(pid_t pid, intptr_t addr, size_t len, void *ctx) = nullptr;
  int (*copyin)(pid_t pid, const void *buf, intptr_t addr, size_t len,
                void *ctx) = nullptr;
  int (*mprotect_rwx)(pid_t pid, uint64_t addr, size_t len,
                      void *ctx) = nullptr;
  int (*munmap)(pid_t pid, intptr_t addr, size_t len, void *ctx) = nullptr;
  int (*detach)(pid_t pid, void *ctx) = nullptr;
  void *ctx = nullptr;
};

enum class CodeCaveMapStatus {
  Ok = 0,
  InvalidRange,
  AttachFailed,
  MmapFailed,
  FaultInFailed,
  MprotectFailed,
};

/**
 * Map an RWX code cave at [addr, addr + len) and fault in every 4 KiB PTE.
 * Always detaches after attach and unmaps any stray/partial mapping on failure.
 */
inline CodeCaveMapStatus mapCodeCaveWithOps(pid_t pid, uint64_t addr,
                                            size_t len,
                                            const CodeCaveMapOps &ops,
                                            uint64_t *mapped_page_out = nullptr,
                                            intptr_t *mmap_ret_out = nullptr) {
  CodeCavePageRange range{};
  if (!computeCodeCavePageRange(addr, len, range) || ops.attach == nullptr ||
      ops.mmap == nullptr || ops.copyin == nullptr ||
      ops.mprotect_rwx == nullptr || ops.detach == nullptr) {
    return CodeCaveMapStatus::InvalidRange;
  }
  if (mapped_page_out != nullptr) {
    *mapped_page_out = range.start;
  }

  if (ops.attach(pid, ops.ctx) < 0) {
    return CodeCaveMapStatus::AttachFailed;
  }

  const intptr_t mapped =
      ops.mmap(pid, static_cast<intptr_t>(range.start), range.length, ops.ctx);
  if (mmap_ret_out != nullptr) {
    *mmap_ret_out = mapped;
  }
  if (mapped != static_cast<intptr_t>(range.start)) {
    if (mapped > 0 && ops.munmap != nullptr) {
      (void)ops.munmap(pid, mapped, range.length, ops.ctx);
    }
    (void)ops.detach(pid, ops.ctx);
    return CodeCaveMapStatus::MmapFailed;
  }

  const uint8_t zero = 0;
  for (uint64_t offset = 0; offset < range.length;
       offset += kCodeCaveFaultStride) {
    if (ops.copyin(pid, &zero, static_cast<intptr_t>(range.start + offset),
                   sizeof(zero), ops.ctx) < 0) {
      if (ops.munmap != nullptr) {
        (void)ops.munmap(pid, static_cast<intptr_t>(range.start), range.length,
                         ops.ctx);
      }
      (void)ops.detach(pid, ops.ctx);
      return CodeCaveMapStatus::FaultInFailed;
    }
  }

  if (ops.mprotect_rwx(pid, range.start, range.length, ops.ctx) < 0) {
    if (ops.munmap != nullptr) {
      (void)ops.munmap(pid, static_cast<intptr_t>(range.start), range.length,
                       ops.ctx);
    }
    (void)ops.detach(pid, ops.ctx);
    return CodeCaveMapStatus::MprotectFailed;
  }

  (void)ops.detach(pid, ops.ctx);
  return CodeCaveMapStatus::Ok;
}

} // namespace onion::cheats
