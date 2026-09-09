#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <sys/types.h>

namespace onion::cheats {

enum class CheatViewMode { Browse, Runtime };

struct GameKey {
  std::string title_id;
  std::string version;

  bool operator==(const GameKey &other) const {
    return title_id == other.title_id && version == other.version;
  }
};

struct ProcessIdentity {
  pid_t pid = 0;
  int appid = 0;
  std::string process_name;
  uint64_t session_generation = 0;

  bool valid() const {
    return pid > 0 && !process_name.empty() && session_generation != 0;
  }
};

struct CheatRequest {
  CheatViewMode mode = CheatViewMode::Browse;
  GameKey game;
  std::optional<ProcessIdentity> process;
};

struct CheatKey {
  std::string source_path;
  std::string source_id;
  size_t local_index = 0;

  std::string serialize() const {
    return source_id + "|" + source_path + "|" +
           std::to_string(local_index);
  }

  bool operator==(const CheatKey &other) const {
    return source_path == other.source_path && source_id == other.source_id &&
           local_index == other.local_index;
  }
};

struct CheatListMetadata {
  CheatViewMode mode = CheatViewMode::Browse;
  std::string session_id;
  bool can_toggle = false;
};

} // namespace onion::cheats
