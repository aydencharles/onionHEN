#include <onion/log.h>
#include "cheats/cheat_applier.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <vector>

#include "cheats/i_memory_backend.hpp"
#include "util_platform.h"

extern "C" {
}

namespace onion::cheats {
namespace {

bool containsToken(const char *hay, const char *needle) {
  return hay && needle && std::strstr(hay, needle) != nullptr;
}

struct ResolvedPatch {
  onion_patch_t *patch = nullptr;
  uint64_t address = 0;
  size_t on_len = 0;
  size_t off_len = 0;
};

struct Snapshot {
  uint64_t address = 0;
  std::vector<uint8_t> bytes;
};

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
                   onion_cheat_entry_t &entry, uint64_t base,
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
      mp.absolute ? mp.offset : (base + mp.offset);
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

} // namespace

int CheatApplier::toggle(const game_context_t &game, onion_cheat_file_t &file,
                         int index, std::string &status,
                         const std::string &source_key) {
  status.clear();
  if (index < 0 || static_cast<size_t>(index) >= file.cheat_count) {
    status = "invalid cheat index";
    return -1;
  }

  auto &entry = file.cheats[index];
  const std::string owner = ownerFor(file, index, source_key);
  const uint32_t fw = util_system_fw_major();
  if (fw == 0) {
    status = std::string(entry.name) + " -> firmware version unavailable";
    return -1;
  }

  auto backend = MemoryBackendFactory::create(fw);
  if (!backend) {
    status = std::string(entry.name) + " -> memory backend unavailable";
    return -1;
  }
  game_context_t target = game;
  pid_t pid = game.pid;

  if (file.last_applied_pid != 0 && file.last_applied_pid != pid) {
    const pid_t stale_pid = file.last_applied_pid;
    LOG_DEBUG("[Cheat] PID changed %d -> %d, reset states",
              (int)stale_pid, (int)pid);
    for (size_t i = 0; i < file.cheat_count; ++i) {
      file.cheats[i].enabled = false;
    }
    file.master_code_id = -1;
    /* A PID identifies the address space. Once it changes, every range
     * belonging to the old process is stale, including ranges owned by other
     * loaded sources. */
    active_ranges_.erase(
        std::remove_if(active_ranges_.begin(), active_ranges_.end(),
                       [&](const ActiveRange &range) {
                         return range.pid == stale_pid;
                       }),
        active_ranges_.end());
  }

  if (entry.module_name[0] == '\0' && game.process_name[0] != '\0') {
    std::snprintf(entry.module_name, sizeof(entry.module_name), "%s",
                  game.process_name);
  }

  util_module_info_t mod{};
  util_module_info_t ps2{};
  if (util_find_module(pid, entry.module_name, &mod) < 0) {
    pid_t fb = -1;
    if (util_find_module_in_app(game.appid, entry.module_name, &fb, &mod) < 0) {
      status = std::string("module not found: ") + entry.module_name;
      return -1;
    }
    pid = fb;
    target.pid = fb;
  }

  const bool is_ps2 =
      util_find_module(pid, "libScePs2EmuMenuDialog.sprx", &ps2) == 0;
  const uint64_t base = mod.sections[0].vaddr;

  if (file.master_code_id < 0 &&
      (containsToken(entry.name, "Master Code") ||
       containsToken(entry.name, "Mastercode"))) {
    file.master_code_id = index;
  } else {
    fixMasterCode(target, file, entry, base, *backend);
  }

  std::vector<ResolvedPatch> resolved;
  resolved.reserve(entry.patch_count);
  for (size_t i = 0; i < entry.patch_count; ++i) {
    auto &patch = entry.patches[i];
    const size_t reserve_len = std::max(patch.on_len, patch.off_len);
    if (reserve_len == 0 || reserve_len > ONION_MAX_PATCH_BYTES ||
        (!entry.enabled && patch.on_len == 0) ||
        (entry.enabled && patch.off_len == 0)) {
      status = std::string(entry.name) + " -> invalid patch size";
      return -1;
    }
    if (patch.is_asm) {
      status = std::string(entry.name) + " -> ASM text not assembled";
      return -1;
    }
    resolved.push_back({&patch, resolveAddr(mod, patch, is_ps2, base),
                        patch.on_len, patch.off_len});
  }

  const bool enabling = !entry.enabled;
  if (enabling) {
    for (const ResolvedPatch &candidate : resolved) {
      const size_t candidate_len =
          std::max(candidate.on_len, candidate.off_len);
      for (const ActiveRange &active : active_ranges_) {
        const bool master_dependency =
            !source_key.empty() && active.source_key == source_key &&
            ((active.cheat_index == file.master_code_id &&
              isMasterDependent(file, index)) ||
             (index == file.master_code_id &&
              isMasterDependent(file, active.cheat_index)));
        if (active.pid == pid && active.owner != owner && !master_dependency &&
            rangesOverlap(candidate.address, candidate_len, active.start,
                          active.length)) {
          status = std::string(entry.name) + " conflicts with " +
                   active.cheat_name + " (source " + active.source_key +
                   ") at 0x";
          char address[32];
          std::snprintf(address, sizeof(address), "%llx",
                        static_cast<unsigned long long>(active.start));
          status += address;
          return -1;
        }
      }
    }
  }

  LOG_DEBUG("[Cheat] Toggle '%s' patches=%zu base=0x%llx %s", entry.name,
            entry.patch_count, (unsigned long long)base,
            entry.enabled ? "ON->OFF" : "OFF->ON");

  std::vector<Snapshot> snapshots;
  snapshots.reserve(resolved.size());
  for (const ResolvedPatch &patch : resolved) {
    const size_t reserve_len = std::max(patch.on_len, patch.off_len);
    Snapshot snapshot;
    snapshot.address = patch.address;
    snapshot.bytes.resize(reserve_len);
    if (backend->read(pid, snapshot.address, snapshot.bytes.data(),
                      snapshot.bytes.size()) < 0) {
      status = std::string(entry.name) + " -> snapshot read failed";
      return -1;
    }
    snapshots.push_back(std::move(snapshot));
  }

  size_t touched_count = 0;
  for (size_t i = 0; i < resolved.size(); ++i) {
    const ResolvedPatch &resolved_patch = resolved[i];
    auto &patch = *resolved_patch.patch;
    const uint8_t *data = enabling ? patch.on : patch.off;
    const size_t len = enabling ? patch.on_len : patch.off_len;

    /* Count the range before the write: a backend may report an error after a
     * partial copy, and the snapshot must still be restored. */
    ++touched_count;
    if (backend->write(pid, resolved_patch.address, data, len) < 0) {
      status = std::string(entry.name) + " -> write failed";
      break;
    }

    {
      uint8_t verify[ONION_MAX_PATCH_BYTES];
      bool ok = backend->read(pid, resolved_patch.address, verify, len) >= 0 &&
                std::memcmp(verify, data, len) == 0;
      if (!ok && enabling && !patch.code_cave_reloc) {
        patch.code_cave_reloc = true;
        if (backend->mapCodeCave(pid, resolved_patch.address, len) == 0 &&
            backend->write(pid, resolved_patch.address, data, len) >= 0 &&
            backend->read(pid, resolved_patch.address, verify, len) >= 0 &&
            std::memcmp(verify, data, len) == 0) {
          ok = true;
        }
      }
      if (!ok) {
        status = std::string(entry.name) + " -> verify mismatch";
        break;
      }
    }
  }

  if (touched_count != resolved.size() || !status.empty()) {
    for (size_t i = touched_count; i > 0; --i) {
      const Snapshot &snapshot = snapshots[i - 1];
      (void)backend->write(pid, snapshot.address, snapshot.bytes.data(),
                           snapshot.bytes.size());
    }
    if (status.empty()) {
      status = std::string(entry.name) + " -> activation failed";
    }
    return -1;
  }

  entry.enabled = enabling;
  file.last_applied_pid = pid;
  if (enabling) {
    for (const ResolvedPatch &patch : resolved) {
      active_ranges_.push_back({pid, patch.address,
                                std::max(patch.on_len, patch.off_len), owner,
                                source_key, index, entry.name});
    }
  } else {
    active_ranges_.erase(
        std::remove_if(active_ranges_.begin(), active_ranges_.end(),
                       [&](const ActiveRange &range) {
                         return range.owner == owner;
                       }),
        active_ranges_.end());
  }
  status = std::string(entry.name) + (entry.enabled ? " -> enabled" : " -> disabled");
  LOG_INFO("[Cheat] %s", status.c_str());
  return 0;
}

void CheatApplier::clearOwnership() { active_ranges_.clear(); }

} // namespace onion::cheats
