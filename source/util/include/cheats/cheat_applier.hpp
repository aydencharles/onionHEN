#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cheats/cheat_engine.h"
#include "util_platform.h"

namespace onion::cheats {

/**
 * Applies / restores cheat patches against a live process.
 * Owns master-code dependency fixups and write verification.
 */
class CheatApplier {
public:
  /**
   * Toggle cheat at index. Returns 0 on success; status holds human message.
   */
  int toggle(const game_context_t &game, onion_cheat_file_t &file, int index,
             std::string &status, const std::string &source_key = {});

  /** Drop ownership records that cannot be associated with a live source. */
  void clearOwnership();

private:
  struct ActiveRange {
    pid_t pid = 0;
    uint64_t start = 0;
    size_t length = 0;
    std::string owner;
    std::string source_key;
    int cheat_index = -1;
    std::string cheat_name;
  };

  std::vector<ActiveRange> active_ranges_;
};

} // namespace onion::cheats
