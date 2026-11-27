#pragma once

#include <algorithm>
#include <cstring>
#include <limits>

#include "cheats/cheat_engine.h"

namespace onion::cheats {

// A master code is the nearest preceding entry whose name says so and whose
// ON bytes include a code cave. A one-byte flag line is not the cave.
inline bool name_has_master_code(const char *name) {
  return name != nullptr && (std::strstr(name, "Master Code") != nullptr ||
                             std::strstr(name, "Mastercode") != nullptr);
}

inline bool entry_has_code_cave(const onion_cheat_entry_t &entry) {
  if (entry.patches == nullptr) {
    return false;
  }
  for (size_t i = 0; i < entry.patch_count; ++i) {
    if (entry.patches[i].on_len >= 16) {
      return true;
    }
  }
  return false;
}

inline int preceding_cave_master(const onion_cheat_file_t &file, int index) {
  if (index <= 0 || file.cheats == nullptr) {
    return -1;
  }
  const size_t limit = static_cast<size_t>(index) < file.cheat_count
                           ? static_cast<size_t>(index)
                           : file.cheat_count;
  int found = -1;
  for (size_t i = 0; i < limit; ++i) {
    const onion_cheat_entry_t &entry = file.cheats[i];
    if (name_has_master_code(entry.name) && entry_has_code_cave(entry)) {
      found = static_cast<int>(i);
    }
  }
  return found;
}

inline bool patch_ranges_overlap(const onion_patch_t &lhs,
                                 const onion_patch_t &rhs) {
  if (lhs.absolute != rhs.absolute || lhs.section != rhs.section) {
    return false;
  }
  const size_t lhs_len = std::max(lhs.on_len, lhs.off_len);
  const size_t rhs_len = std::max(rhs.on_len, rhs.off_len);
  if (lhs_len == 0 || rhs_len == 0 ||
      lhs.offset > std::numeric_limits<uint64_t>::max() - lhs_len ||
      rhs.offset > std::numeric_limits<uint64_t>::max() - rhs_len) {
    return false;
  }
  const uint64_t lhs_end = lhs.offset + lhs_len;
  const uint64_t rhs_end = rhs.offset + rhs_len;
  return lhs.offset < rhs_end && rhs.offset < lhs_end;
}

inline bool entries_overlap(const onion_cheat_entry_t &lhs,
                            const onion_cheat_entry_t &rhs) {
  if (lhs.patches == nullptr || rhs.patches == nullptr) {
    return false;
  }
  if (lhs.module_name[0] != '\0' && rhs.module_name[0] != '\0' &&
      std::strcmp(lhs.module_name, rhs.module_name) != 0) {
    return false;
  }
  for (size_t i = 0; i < lhs.patch_count; ++i) {
    for (size_t j = 0; j < rhs.patch_count; ++j) {
      if (patch_ranges_overlap(lhs.patches[i], rhs.patches[j])) {
        return true;
      }
    }
  }
  return false;
}

// True when this cheat writes bytes that also belong to its master code.
inline bool overlaps_preceding_master(const onion_cheat_file_t &file,
                                      int index) {
  const int master = preceding_cave_master(file, index);
  if (master < 0 || file.cheats == nullptr ||
      static_cast<size_t>(index) >= file.cheat_count) {
    return false;
  }
  return entries_overlap(file.cheats[index], file.cheats[master]);
}

} // namespace onion::cheats
