#include <onion/log.h>
#include <onion/notify.h>
#include "cheats/cheat_applier.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
#include <sys/mman.h>

#include "cheats/i_memory_backend.hpp"
#include "util_platform.h"

extern "C" {
}

namespace onion::cheats {
namespace {

bool containsToken(const char *hay, const char *needle) {
  return hay && needle && std::strstr(hay, needle) != nullptr;
}

struct ResolvedSegment {
  uint64_t address = 0;
  size_t patch_index = 0;
  size_t segment_index = 0;
  int transaction_phase = 2;
  std::vector<uint8_t> on;
  std::vector<uint8_t> off;
  bool restore_original = false;
  bool allow_code_cave_reloc = false;
};

struct ResolvedPatch {
  onion_patch_t *patch = nullptr;
  std::vector<ResolvedSegment> segments;
};

struct Snapshot {
  uint64_t address = 0;
  std::vector<uint8_t> bytes;
};

struct ModuleRange {
  uint64_t start = 0;
  uint64_t size = 0;
};

struct Elf64Header {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t program_offset;
  uint64_t section_offset;
  uint32_t flags;
  uint16_t header_size;
  uint16_t program_entry_size;
  uint16_t program_count;
  uint16_t section_entry_size;
  uint16_t section_count;
  uint16_t section_name_index;
};

struct Elf64ProgramHeader {
  uint32_t type;
  uint32_t flags;
  uint64_t offset;
  uint64_t virtual_address;
  uint64_t physical_address;
  uint64_t file_size;
  uint64_t memory_size;
  uint64_t alignment;
};

static_assert(sizeof(Elf64Header) == 64, "unexpected ELF64 header layout");
static_assert(sizeof(Elf64ProgramHeader) == 56,
              "unexpected ELF64 program header layout");

constexpr size_t kScanChunkSize = 64 * 1024;
constexpr size_t kMaxElfProgramHeaders = 128;
constexpr uint64_t kMaxModuleScanBytes = uint64_t{1} << 30;

bool rangesOverlap(uint64_t lhs_start, size_t lhs_length, uint64_t rhs_start,
                   size_t rhs_length) {
  if (lhs_length == 0 || rhs_length == 0) {
    return false;
  }
  const uint64_t lhs_end = lhs_start > std::numeric_limits<uint64_t>::max() -
                                    lhs_length
                                ? std::numeric_limits<uint64_t>::max()
                                : lhs_start + lhs_length;
  const uint64_t rhs_end = rhs_start > std::numeric_limits<uint64_t>::max() -
                                    rhs_length
                                ? std::numeric_limits<uint64_t>::max()
                                : rhs_start + rhs_length;
  return lhs_start < rhs_end && rhs_start < lhs_end;
}

std::string ownerFor(const onion_cheat_file_t &file, int index,
                     const std::string &source_key) {
  if (!source_key.empty()) {
    return source_key + "#" + std::to_string(index);
  }
  return "file@" + std::to_string(reinterpret_cast<uintptr_t>(&file)) +
         "#" + std::to_string(index);
}

uint64_t resolveAddr(const util_module_info_t &mod, const onion_patch_t &patch,
                     bool is_ps2, uint64_t base) {
  if (is_ps2 || patch.absolute) {
    return patch.offset;
  }
  (void)mod;
  return base + patch.offset;
}

void fixMasterCode(const game_context_t &game, onion_cheat_file_t &file,
                   onion_cheat_entry_t &entry, bool is_ps2, uint64_t base,
                   IMemoryBackend &mem) {
  if (file.master_code_id < 0 || !containsToken(entry.name, "MC") ||
      entry.patch_count != 1 || entry.patches[0].section == 0) {
    return;
  }
  auto &master = file.cheats[file.master_code_id];
  if (master.patch_count == 0) {
    return;
  }
  auto &mp = master.patches[0];
  auto &dp = entry.patches[0];
  const uint64_t mc_addr =
      (is_ps2 || mp.absolute) ? mp.offset : (base + mp.offset);
  std::vector<uint8_t> buf(mp.on_len);
  if (mem.read(game.pid, mc_addr, buf.data(), mp.on_len) < 0) {
    return;
  }
  for (size_t i = 0; i + dp.off_len <= mp.on_len; ++i) {
    if (std::memcmp(buf.data() + i, dp.off, dp.off_len) == 0) {
      dp.offset = mp.offset + i;
      return;
    }
  }
  dp.offset = ((mp.offset >> 8) << 8) | (dp.offset & 0xff);
}

std::string status_tr(const char *key, ...) {
  char buf[384];
  va_list ap;
  va_start(ap, key);
  onion_notify_format(buf, sizeof(buf), 0, key, ap);
  va_end(ap);
  return buf;
}

const char *source_label(const std::string &source_key) {
  if (source_key.empty()) {
    return "-";
  }
  const size_t slash = source_key.find_last_of('/');
  if (slash != std::string::npos && slash + 1 < source_key.size()) {
    return source_key.c_str() + slash + 1;
  }
  return source_key.c_str();
}

bool isMasterDependent(const onion_cheat_file_t &file, int index) {
  if (index < 0 || static_cast<size_t>(index) >= file.cheat_count ||
      index == file.master_code_id) {
    return false;
  }
  const onion_cheat_entry_t &entry = file.cheats[index];
  return containsToken(entry.name, "MC") && entry.patch_count == 1 &&
         entry.patches != nullptr &&
         entry.patches[0].section != 0;
}

bool addReadableRange(std::vector<ModuleRange> &ranges, uint64_t start,
                     uint64_t size, uint64_t &total_size) {
  if (start == 0 || size == 0 ||
      start > std::numeric_limits<uint64_t>::max() - size ||
      size > kMaxModuleScanBytes ||
      total_size > kMaxModuleScanBytes - size) {
    return false;
  }
  ranges.push_back({start, size});
  total_size += size;
  return true;
}

bool deriveElfReadableRanges(pid_t pid, uint64_t base, IMemoryBackend &mem,
                            std::vector<ModuleRange> &ranges) {
  Elf64Header header{};
  if (base == 0 || mem.read(pid, base, &header, sizeof(header)) < 0 ||
      std::memcmp(header.ident, "\x7f" "ELF", 4) != 0 ||
      header.ident[4] != 2 || header.ident[5] != 1 || header.ident[6] != 1 ||
      (header.type != 2 && header.type != 3) || header.machine != 62 ||
      header.version != 1 || header.header_size != sizeof(header) ||
      header.program_entry_size != sizeof(Elf64ProgramHeader) ||
      header.program_count == 0 ||
      header.program_count > kMaxElfProgramHeaders ||
      header.program_offset > 1024 * 1024) {
    return false;
  }
  const size_t program_bytes =
      static_cast<size_t>(header.program_count) * sizeof(Elf64ProgramHeader);
  if (base > std::numeric_limits<uint64_t>::max() - header.program_offset) {
    return false;
  }
  const uint64_t program_address = base + header.program_offset;
  if (program_address >
      std::numeric_limits<uint64_t>::max() - program_bytes) {
    return false;
  }
  std::vector<Elf64ProgramHeader> programs(header.program_count);
  if (mem.read(pid, program_address, programs.data(), program_bytes) < 0) {
    return false;
  }

  uint64_t min_virtual_address = std::numeric_limits<uint64_t>::max();
  uint64_t alignment = 0x1000;
  for (const Elf64ProgramHeader &program : programs) {
    if (program.type != 1 || program.memory_size == 0) {
      continue;
    }
    if (program.file_size > program.memory_size ||
        program.virtual_address >
            std::numeric_limits<uint64_t>::max() - program.memory_size ||
        (program.alignment > 1 &&
         ((program.alignment & (program.alignment - 1)) != 0 ||
          program.alignment > (uint64_t{1} << 20)))) {
      return false;
    }
    min_virtual_address =
        std::min(min_virtual_address, program.virtual_address);
    alignment = std::max(alignment, program.alignment > 1
                                        ? program.alignment
                                        : uint64_t{0x1000});
  }
  if (min_virtual_address == std::numeric_limits<uint64_t>::max()) {
    return false;
  }
  const uint64_t first_page = min_virtual_address & ~(alignment - 1);
  if (base < first_page) {
    return false;
  }
  const uint64_t load_bias = base - first_page;
  uint64_t total_size = 0;
  for (const Elf64ProgramHeader &program : programs) {
    if (program.type != 1 || (program.flags & 4) == 0 ||
        program.memory_size == 0) {
      continue;
    }
    if (load_bias > std::numeric_limits<uint64_t>::max() -
                        program.virtual_address ||
        !addReadableRange(ranges, load_bias + program.virtual_address,
                          program.memory_size, total_size)) {
      ranges.clear();
      return false;
    }
  }
  return !ranges.empty();
}

bool getReadableModuleRanges(pid_t pid, const util_module_info_t &module,
                             uint64_t base, IMemoryBackend &mem,
                             std::vector<ModuleRange> &ranges) {
  ranges.clear();
  uint64_t total_size = 0;
  for (const util_module_section_t &section : module.sections) {
    if (section.size == 0 || (section.prot & PROT_READ) == 0) {
      continue;
    }
    if (!addReadableRange(ranges, section.vaddr, section.size, total_size)) {
      ranges.clear();
      return false;
    }
  }
  if (!ranges.empty()) {
    std::sort(ranges.begin(), ranges.end(),
              [](const ModuleRange &lhs, const ModuleRange &rhs) {
                return lhs.start < rhs.start;
              });
    return true;
  }
  return deriveElfReadableRanges(pid, base, mem, ranges);
}

bool containsRange(const std::vector<ModuleRange> &ranges, uint64_t address,
                   size_t length) {
  if (length == 0 ||
      address > std::numeric_limits<uint64_t>::max() - length) {
    return false;
  }
  const uint64_t end = address + length;
  for (const ModuleRange &range : ranges) {
    if (range.start <= address && range.start <=
                                      std::numeric_limits<uint64_t>::max() -
                                          range.size &&
        end <= range.start + range.size) {
      return true;
    }
  }
  return false;
}

bool addSignedOffset(uint64_t address, int64_t offset, uint64_t &result) {
  if (offset >= 0) {
    const uint64_t amount = static_cast<uint64_t>(offset);
    if (address > std::numeric_limits<uint64_t>::max() - amount) {
      return false;
    }
    result = address + amount;
    return true;
  }
  const uint64_t amount = static_cast<uint64_t>(-(offset + 1)) + 1;
  if (address < amount) {
    return false;
  }
  result = address - amount;
  return true;
}

bool scanUnique(pid_t pid, const std::vector<ModuleRange> &ranges,
                const uint8_t *pattern, const uint8_t *pattern_mask,
                size_t pattern_length, IMemoryBackend &mem,
                uint64_t &match_address) {
  if (pattern == nullptr || pattern_length == 0 ||
      pattern_length > ONION_MAX_PATTERN_BYTES || ranges.empty()) {
    return false;
  }
  bool found = false;
  uint64_t found_address = 0;
  std::vector<uint8_t> buffer;
  for (const ModuleRange &range : ranges) {
    if (range.size < pattern_length) {
      continue;
    }
    for (uint64_t offset = 0; offset < range.size;) {
      const uint64_t starts =
          std::min<uint64_t>(kScanChunkSize, range.size - offset);
      const uint64_t needed = std::min<uint64_t>(
          range.size - offset, starts + pattern_length - 1);
      if (range.start > std::numeric_limits<uint64_t>::max() - offset ||
          needed > std::numeric_limits<size_t>::max()) {
        return false;
      }
      buffer.resize(static_cast<size_t>(needed));
      if (mem.read(pid, range.start + offset, buffer.data(), buffer.size()) <
          0) {
        return false;
      }
      for (uint64_t local = 0;
           local < starts && local + pattern_length <= needed; ++local) {
        bool matches = true;
        for (size_t byte = 0; byte < pattern_length; ++byte) {
          if ((pattern_mask == nullptr || pattern_mask[byte] != 0) &&
              buffer[static_cast<size_t>(local) + byte] != pattern[byte]) {
            matches = false;
            break;
          }
        }
        if (!matches) {
          continue;
        }
        const uint64_t candidate = range.start + offset + local;
        if (found && candidate != found_address) {
          return false;
        }
        found = true;
        found_address = candidate;
      }
      offset += starts;
    }
  }
  if (!found) {
    return false;
  }
  match_address = found_address;
  return true;
}

bool makeRel32(uint64_t instruction_address, uint64_t destination,
               int32_t &relative) {
  if (instruction_address > std::numeric_limits<uint64_t>::max() - 5) {
    return false;
  }
  const uint64_t next_instruction = instruction_address + 5;
  if (destination >= next_instruction) {
    const uint64_t delta = destination - next_instruction;
    if (delta > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      return false;
    }
    relative = static_cast<int32_t>(delta);
    return true;
  }
  const uint64_t delta = next_instruction - destination;
  const uint64_t min_magnitude =
      static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) + 1;
  if (delta > min_magnitude) {
    return false;
  }
  relative = delta == min_magnitude
                 ? std::numeric_limits<int32_t>::min()
                 : -static_cast<int32_t>(delta);
  return true;
}

std::vector<uint8_t> makeJumpBytes(uint64_t instruction_address,
                                   uint64_t destination, size_t size) {
  std::vector<uint8_t> bytes(size, 0x90);
  int32_t relative = 0;
  if (size < 5 || !makeRel32(instruction_address, destination, relative)) {
    return {};
  }
  const uint32_t encoded = static_cast<uint32_t>(relative);
  bytes[0] = 0xe9;
  for (size_t index = 0; index < 4; ++index) {
    bytes[index + 1] = static_cast<uint8_t>(encoded >> (index * 8));
  }
  return bytes;
}

} // namespace

int CheatApplier::toggle(const game_context_t &game, onion_cheat_file_t &file,
                         int index, std::string &status,
                         const std::string &source_key) {
  status.clear();
  if (index < 0 || static_cast<size_t>(index) >= file.cheat_count) {
    status = status_tr("notify.cheats.invalid_index");
    return -1;
  }

  auto &entry = file.cheats[index];
  const std::string owner = ownerFor(file, index, source_key);
  const uint32_t fw = util_system_fw_major();
  if (fw == 0) {
    status = status_tr("notify.cheats.fw_unavailable", entry.name);
    return -1;
  }

  auto backend = MemoryBackendFactory::create(fw);
  if (!backend) {
    status = status_tr("notify.cheats.backend_unavailable", entry.name);
    return -1;
  }
  game_context_t target = game;
  pid_t pid = game.pid;

  if (file.last_applied_pid != 0 && file.last_applied_pid != pid) {
    const pid_t stale_pid = file.last_applied_pid;
    LOG_DEBUG("[Cheat] PID changed %d -> %d, reset states", (int)stale_pid,
              (int)pid);
    for (size_t i = 0; i < file.cheat_count; ++i) {
      file.cheats[i].enabled = false;
    }
    file.master_code_id = -1;
    active_ranges_.erase(
        std::remove_if(active_ranges_.begin(), active_ranges_.end(),
                       [&](const ActiveRange &range) {
                         return range.pid == stale_pid;
                       }),
        active_ranges_.end());
  }

  const bool enabling = !entry.enabled;
  if (entry.module_name[0] == '\0' && game.process_name[0] != '\0') {
    std::snprintf(entry.module_name, sizeof(entry.module_name), "%s",
                  game.process_name);
  }

  util_module_info_t mod{};
  util_module_info_t ps2{};
  if (util_find_module(pid, entry.module_name, &mod) < 0) {
    pid_t fallback_pid = -1;
    if (util_find_module_in_app(game.appid, entry.module_name, &fallback_pid,
                                &mod) < 0) {
      status = status_tr("notify.cheats.module_missing", entry.module_name);
      return -1;
    }
    pid = fallback_pid;
    target.pid = fallback_pid;
  }

  const bool is_ps2 =
      util_find_module(pid, "libScePs2EmuMenuDialog.sprx", &ps2) == 0;
  const uint64_t base = mod.sections[0].vaddr;
  if (file.master_code_id < 0 &&
      (containsToken(entry.name, "Master Code") ||
       containsToken(entry.name, "Mastercode"))) {
    file.master_code_id = index;
  } else {
    fixMasterCode(target, file, entry, is_ps2, base, *backend);
  }

  std::vector<ModuleRange> readable_ranges;
  bool ranges_ready = false;
  std::vector<ResolvedPatch> resolved;
  resolved.reserve(entry.patch_count);
  for (size_t patch_index = 0; patch_index < entry.patch_count; ++patch_index) {
    onion_patch_t &patch = entry.patches[patch_index];
    if (patch.is_asm) {
      status = status_tr("notify.cheats.asm_unassembled", entry.name);
      return -1;
    }
    if (patch.on_len == 0 || patch.on_len > sizeof(patch.on) ||
        patch.off_len > sizeof(patch.off)) {
      status = status_tr("notify.cheats.invalid_patch", entry.name);
      return -1;
    }

    ResolvedPatch candidate;
    candidate.patch = &patch;
    if (enabling) {
      if (patch.kind == ONION_PATCH_DIRECT) {
        if (!is_ps2 && !patch.absolute &&
            patch.offset > std::numeric_limits<uint64_t>::max() - base) {
          status = status_tr("notify.cheats.invalid_patch", entry.name);
          return -1;
        }
        ResolvedSegment segment;
        segment.address = resolveAddr(mod, patch, is_ps2, base);
        segment.patch_index = patch_index;
        segment.segment_index = 0;
        segment.transaction_phase = 2;
        segment.on.assign(patch.on, patch.on + patch.on_len);
        segment.off.assign(patch.off, patch.off + patch.off_len);
        if (patch.restore_original && segment.off.empty()) {
          segment.off.resize(segment.on.size());
        }
        segment.restore_original = patch.restore_original;
        segment.allow_code_cave_reloc = !patch.code_cave_reloc;
        candidate.segments.push_back(std::move(segment));
      } else if (patch.kind == ONION_PATCH_MASK ||
                 patch.kind == ONION_PATCH_MASK_JUMP32) {
        if (is_ps2 || patch.pattern_len == 0 ||
            patch.pattern_len > ONION_MAX_PATTERN_BYTES ||
            patch.on_len == 0) {
          status = status_tr("notify.cheats.invalid_patch", entry.name);
          return -1;
        }
        if (!ranges_ready) {
          if (!getReadableModuleRanges(pid, mod, base, *backend,
                                       readable_ranges)) {
            LOG_WARN("[Cheat] Cannot establish safe scan ranges for '%s'",
                     entry.name);
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          ranges_ready = true;
        }
        uint64_t pattern_match = 0;
        uint64_t patch_address = 0;
        if (!scanUnique(pid, readable_ranges, patch.pattern,
                        patch.pattern_mask, patch.pattern_len, *backend,
                        pattern_match) ||
            !addSignedOffset(pattern_match, patch.pattern_offset,
                             patch_address)) {
          LOG_WARN("[Cheat] Pattern missing, ambiguous, or offset invalid for '%s'",
                   entry.name);
          status = status_tr("notify.cheats.invalid_patch", entry.name);
          return -1;
        }
        if (patch.kind == ONION_PATCH_MASK) {
          if (!containsRange(readable_ranges, patch_address, patch.on_len)) {
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          ResolvedSegment segment;
          segment.address = patch_address;
          segment.patch_index = patch_index;
          segment.segment_index = 0;
          segment.transaction_phase = 2;
          segment.on.assign(patch.on, patch.on + patch.on_len);
          segment.off.resize(segment.on.size());
          segment.restore_original = true;
          candidate.segments.push_back(std::move(segment));
        } else {
          if (patch.target_len == 0 ||
              patch.target_len > ONION_MAX_PATTERN_BYTES ||
              patch.jump_size < 5 ||
              patch.jump_size > ONION_MAX_PATCH_BYTES ||
              patch.on_len > ONION_MAX_PATCH_BYTES ||
              patch.on_len > std::numeric_limits<size_t>::max() - 5) {
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          uint64_t cave_address = 0;
          const size_t cave_length = patch.on_len + 5;
          if (!scanUnique(pid, readable_ranges, patch.target, nullptr,
                          patch.target_len, *backend, cave_address) ||
              !containsRange(readable_ranges, cave_address, cave_length) ||
              !containsRange(readable_ranges, patch_address, patch.jump_size) ||
              cave_address >
                  std::numeric_limits<uint64_t>::max() - patch.on_len) {
            LOG_WARN("[Cheat] mask_jump32 cave or source range invalid for '%s'",
                     entry.name);
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          const uint64_t cave_return_address = cave_address + patch.on_len;
          if (patch_address > std::numeric_limits<uint64_t>::max() -
                                  patch.jump_size) {
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          std::vector<uint8_t> source_jump =
              makeJumpBytes(patch_address, cave_address, patch.jump_size);
          std::vector<uint8_t> cave_return = makeJumpBytes(
              cave_return_address, patch_address + patch.jump_size, 5);
          if (source_jump.empty() || cave_return.empty()) {
            LOG_WARN("[Cheat] mask_jump32 rel32 displacement is out of range for '%s'",
                     entry.name);
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          ResolvedSegment cave_payload;
          cave_payload.address = cave_address;
          cave_payload.patch_index = patch_index;
          cave_payload.segment_index = 0;
          cave_payload.transaction_phase = 0;
          cave_payload.on.assign(patch.on, patch.on + patch.on_len);
          cave_payload.off.resize(cave_payload.on.size());
          cave_payload.restore_original = true;
          candidate.segments.push_back(std::move(cave_payload));

          ResolvedSegment return_branch;
          return_branch.address = cave_return_address;
          return_branch.patch_index = patch_index;
          return_branch.segment_index = 1;
          return_branch.transaction_phase = 1;
          return_branch.on = std::move(cave_return);
          return_branch.off.resize(return_branch.on.size());
          return_branch.restore_original = true;
          candidate.segments.push_back(std::move(return_branch));

          ResolvedSegment source_branch;
          source_branch.address = patch_address;
          source_branch.patch_index = patch_index;
          source_branch.segment_index = 2;
          source_branch.transaction_phase = 3;
          source_branch.on = std::move(source_jump);
          source_branch.off.resize(source_branch.on.size());
          source_branch.restore_original = true;
          candidate.segments.push_back(std::move(source_branch));
        }
      } else {
        status = status_tr("notify.cheats.invalid_patch", entry.name);
        return -1;
      }
    } else {
      if (patch.kind != ONION_PATCH_DIRECT &&
          patch.kind != ONION_PATCH_MASK &&
          patch.kind != ONION_PATCH_MASK_JUMP32) {
        status = status_tr("notify.cheats.invalid_patch", entry.name);
        return -1;
      }
      const size_t expected_segments =
          patch.kind == ONION_PATCH_MASK_JUMP32 ? 3 : 1;
      std::vector<const ActiveRange *> owned_segments;
      for (const ActiveRange &active : active_ranges_) {
        if (active.pid == pid && active.owner == owner &&
            active.cheat_index == index &&
            active.patch_index == patch_index) {
          owned_segments.push_back(&active);
        }
      }
      if (owned_segments.size() != expected_segments) {
        LOG_WARN("[Cheat] Missing active segments for '%s' patch=%zu",
                 entry.name, patch_index);
        status = status_tr("notify.cheats.invalid_patch", entry.name);
        return -1;
      }
      for (size_t segment_index = 0; segment_index < expected_segments;
           ++segment_index) {
        const auto active = std::find_if(
            owned_segments.begin(), owned_segments.end(),
            [&](const ActiveRange *range) {
              return range->segment_index == segment_index;
            });
        if (active == owned_segments.end() || (*active)->applied.empty()) {
          status = status_tr("notify.cheats.invalid_patch", entry.name);
          return -1;
        }
        ResolvedSegment segment;
        segment.address = (*active)->start;
        segment.patch_index = patch_index;
        segment.segment_index = segment_index;
        segment.transaction_phase =
            patch.kind == ONION_PATCH_MASK_JUMP32
                ? (segment_index == 2 ? 3
                                      : static_cast<int>(segment_index))
                : 2;
        segment.on = (*active)->applied;
        segment.restore_original = patch.restore_original;
        if (patch.restore_original) {
          if ((*active)->original.size() != segment.on.size()) {
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          segment.off = (*active)->original;
        } else {
          if (patch.off_len == 0) {
            status = status_tr("notify.cheats.invalid_patch", entry.name);
            return -1;
          }
          segment.off.assign(patch.off, patch.off + patch.off_len);
        }
        candidate.segments.push_back(std::move(segment));
      }
    }
    if (candidate.segments.empty()) {
      LOG_ERROR(
          "[Cheat] Invalid patch for %s pid=%d patch=%zu/%zu reason=%s "
          "on_len=%zu off_len=%zu limit=%d asm=%d",
          enabling ? "enable" : "disable", (int)pid, i + 1,
          entry.patch_count, "no resolved segments", patch.on_len,
          patch.off_len, ONION_MAX_PATCH_BYTES, patch.is_asm);
      status = status_tr("notify.cheats.invalid_patch", entry.name);
      return -1;
    }
    for (size_t segment_index = 0;
         segment_index < candidate.segments.size(); ++segment_index) {
      const ResolvedSegment &segment = candidate.segments[segment_index];
      const size_t reserve_len =
          std::max(segment.on.size(), segment.off.size());
      const bool address_overflow =
          reserve_len != 0 && segment.address >
                                  std::numeric_limits<uint64_t>::max() - reserve_len;
      if (reserve_len == 0 || reserve_len > ONION_MAX_PATCH_BYTES ||
          address_overflow || (enabling && segment.on.empty()) ||
          (!enabling && segment.off.empty())) {
        const char *reason = "missing on bytes";
        if (reserve_len == 0) {
          reason = "empty segment";
        } else if (reserve_len > ONION_MAX_PATCH_BYTES) {
          reason = "segment exceeds byte limit";
        } else if (address_overflow) {
          reason = "address range overflows";
        } else if (!enabling) {
          reason = "missing off bytes";
        }
        LOG_ERROR(
            "[Cheat] Invalid patch for %s pid=%d patch=%zu/%zu segment=%zu "
            "reason=%s address=0x%llx on_len=%zu off_len=%zu limit=%d asm=%d",
            enabling ? "enable" : "disable", (int)pid, i + 1,
            entry.patch_count, segment_index + 1, reason,
            static_cast<unsigned long long>(segment.address),
            segment.on.size(), segment.off.size(), ONION_MAX_PATCH_BYTES,
            patch.is_asm);
        status = status_tr("notify.cheats.invalid_patch", entry.name);
        return -1;
      }
    }
    resolved.push_back(std::move(candidate));
  }

  struct TransactionItem {
    ResolvedSegment *segment = nullptr;
    onion_patch_t *patch = nullptr;
  };
  std::vector<TransactionItem> transaction;
  size_t segment_count = 0;
  for (const ResolvedPatch &patch : resolved) {
    segment_count += patch.segments.size();
  }
  transaction.reserve(segment_count);
  for (ResolvedPatch &patch : resolved) {
    for (ResolvedSegment &segment : patch.segments) {
      transaction.push_back({&segment, patch.patch});
    }
  }
  std::stable_sort(transaction.begin(), transaction.end(),
                   [&](const TransactionItem &lhs,
                       const TransactionItem &rhs) {
                     return enabling
                                ? lhs.segment->transaction_phase <
                                      rhs.segment->transaction_phase
                                : lhs.segment->transaction_phase >
                                      rhs.segment->transaction_phase;
                   });

  if (enabling) {
    for (size_t i = 0; i < transaction.size(); ++i) {
      const ResolvedSegment &candidate = *transaction[i].segment;
      const size_t candidate_len =
          std::max(candidate.on.size(), candidate.off.size());
      if (candidate_len == 0 || candidate_len > ONION_MAX_PATCH_BYTES ||
          candidate.address >
              std::numeric_limits<uint64_t>::max() - candidate_len) {
        status = status_tr("notify.cheats.invalid_patch", entry.name);
        return -1;
      }
      for (size_t j = 0; j < i; ++j) {
        const ResolvedSegment &previous = *transaction[j].segment;
        const size_t previous_len =
            std::max(previous.on.size(), previous.off.size());
        if (rangesOverlap(candidate.address, candidate_len, previous.address,
                          previous_len)) {
          LOG_WARN("[Cheat] Refusing overlapping lines in '%s'", entry.name);
          status = status_tr("notify.cheats.invalid_patch", entry.name);
          return -1;
        }
      }
      for (const ActiveRange &active : active_ranges_) {
        const bool master_dependency =
            !source_key.empty() && active.source_key == source_key &&
            ((active.cheat_index == file.master_code_id &&
              isMasterDependent(file, index)) ||
             (index == file.master_code_id &&
              isMasterDependent(file, active.cheat_index)));
        if (active.pid == pid && active.owner != owner &&
            !master_dependency &&
            rangesOverlap(candidate.address, candidate_len, active.start,
                          active.length)) {
          char address[32];
          std::snprintf(address, sizeof(address), "%llx",
                        static_cast<unsigned long long>(active.start));
          status = status_tr("notify.cheats.conflict", entry.name,
                             active.cheat_name.c_str(),
                             source_label(active.source_key), address);
          return -1;
        }
      }
    }
  }

  LOG_DEBUG("[Cheat] Toggle '%s' patches=%zu base=0x%llx %s", entry.name,
            entry.patch_count, (unsigned long long)base,
            entry.enabled ? "ON->OFF" : "OFF->ON");

  std::vector<Snapshot> snapshots;
  snapshots.reserve(transaction.size());
  for (size_t i = 0; i < transaction.size(); ++i) {
    const TransactionItem &item = transaction[i];
    const ResolvedSegment &segment = *item.segment;
    const size_t reserve_len = std::max(segment.on.size(), segment.off.size());
    if (reserve_len == 0 || reserve_len > ONION_MAX_PATCH_BYTES ||
        segment.address >
            std::numeric_limits<uint64_t>::max() - reserve_len) {
      status = status_tr("notify.cheats.invalid_patch", entry.name);
      return -1;
    }
    Snapshot snapshot;
    snapshot.address = segment.address;
    snapshot.bytes.resize(reserve_len);
    const int read_result = backend->read(
        pid, snapshot.address, snapshot.bytes.data(), snapshot.bytes.size());
    if (read_result < 0) {
      LOG_ERROR(
          "[Cheat] Snapshot read failed pid=%d patch=%zu/%zu segment=%zu "
          "addr=0x%llx "
          "len=%zu rc=%d",
          (int)pid, segment.patch_index + 1, entry.patch_count,
          segment.segment_index + 1,
          (unsigned long long)snapshot.address, snapshot.bytes.size(),
          read_result);
      status = status_tr("notify.cheats.snapshot_failed", entry.name);
      return -1;
    }
    snapshots.push_back(std::move(snapshot));
  }

  if (!enabling) {
    for (size_t i = 0; i < transaction.size(); ++i) {
      const ResolvedSegment &segment = *transaction[i].segment;
      if (segment.restore_original &&
          std::memcmp(snapshots[i].bytes.data(), segment.on.data(),
                      segment.on.size()) != 0) {
        LOG_WARN("[Cheat] Refusing restore for '%s': live bytes changed",
                 entry.name);
        status = status_tr("notify.cheats.verify_failed", entry.name);
        return -1;
      }
    }
  } else {
    for (size_t i = 0; i < transaction.size(); ++i) {
      ResolvedSegment &segment = *transaction[i].segment;
      if (segment.restore_original) {
        segment.off.assign(snapshots[i].bytes.begin(),
                           snapshots[i].bytes.begin() + segment.on.size());
      }
    }
  }

  std::vector<ActiveRange> pending_active_ranges;
  if (enabling) {
    try {
      active_ranges_.reserve(active_ranges_.size() + transaction.size());
      pending_active_ranges.reserve(transaction.size());
      for (const TransactionItem &item : transaction) {
        const ResolvedSegment &segment = *item.segment;
        ActiveRange active;
        active.pid = pid;
        active.start = segment.address;
        active.length = std::max(segment.on.size(), segment.off.size());
        active.owner = owner;
        active.source_key = source_key;
        active.cheat_index = index;
        active.patch_index = segment.patch_index;
        active.segment_index = segment.segment_index;
        active.cheat_name = entry.name;
        active.applied = segment.on;
        if (segment.restore_original) {
          active.original = segment.off;
        }
        pending_active_ranges.push_back(std::move(active));
      }
    } catch (const std::exception &) {
      status = status_tr("notify.cheats.activation_failed", entry.name);
      return -1;
    }
  }

  size_t touched_count = 0;
  for (size_t i = 0; i < transaction.size(); ++i) {
    ResolvedSegment &segment = *transaction[i].segment;
    onion_patch_t &patch = *transaction[i].patch;
    const std::vector<uint8_t> &data = enabling ? segment.on : segment.off;
    const size_t len = data.size();

    if (!enabling && segment.restore_original) {
      uint8_t current[ONION_MAX_PATCH_BYTES];
      if (backend->read(pid, segment.address, current, len) < 0 ||
          std::memcmp(current, segment.on.data(), len) != 0) {
        LOG_WARN("[Cheat] Refusing restore for '%s': live bytes changed",
                 entry.name);
        status = status_tr("notify.cheats.verify_failed", entry.name);
        break;
      }
    }

    ++touched_count;
    if (backend->write(pid, segment.address, data.data(), len) < 0) {
      status = status_tr("notify.cheats.write_failed", entry.name);
      break;
    }

    uint8_t verify[ONION_MAX_PATCH_BYTES];
    bool ok = backend->read(pid, segment.address, verify, len) >= 0 &&
              std::memcmp(verify, data.data(), len) == 0;
    if (!ok && enabling && segment.allow_code_cave_reloc &&
        !patch.code_cave_reloc) {
      patch.code_cave_reloc = true;
      if (backend->mapCodeCave(pid, segment.address, len) == 0 &&
          backend->write(pid, segment.address, data.data(), len) >= 0 &&
          backend->read(pid, segment.address, verify, len) >= 0 &&
          std::memcmp(verify, data.data(), len) == 0) {
        ok = true;
      }
    }
    if (!ok) {
      status = status_tr("notify.cheats.verify_failed", entry.name);
      break;
    }
  }

  if (touched_count != transaction.size() || !status.empty()) {
    for (size_t i = touched_count; i > 0; --i) {
      const Snapshot &snapshot = snapshots[i - 1];
      (void)backend->write(pid, snapshot.address, snapshot.bytes.data(),
                           snapshot.bytes.size());
    }
    if (status.empty()) {
      status = status_tr("notify.cheats.activation_failed", entry.name);
    }
    return -1;
  }

  entry.enabled = enabling;
  file.last_applied_pid = pid;
  if (enabling) {
    for (ActiveRange &active : pending_active_ranges) {
      active_ranges_.push_back(std::move(active));
    }
  } else {
    active_ranges_.erase(
        std::remove_if(active_ranges_.begin(), active_ranges_.end(),
                       [&](const ActiveRange &range) {
                         return range.owner == owner;
                       }),
        active_ranges_.end());
  }
  status = std::string(entry.name) +
           (entry.enabled ? " -> enabled" : " -> disabled");
  LOG_INFO("[Cheat] %s", status.c_str());
  return 0;
}
void CheatApplier::clearOwnership() { active_ranges_.clear(); }

} // namespace onion::cheats
