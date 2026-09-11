#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cheats/cheat_applier.hpp"
#include "cheats/cheat_repository.hpp"
#include "cheats/cheat_types.hpp"

namespace onion::cheats {

struct LoadedCheatFile {
  std::string path;
  FileSignature signature;
  onion_cheat_filename_t filename{};
  onion_cheat_file_t file{};

  LoadedCheatFile() { file.master_code_id = -1; }
  ~LoadedCheatFile() { onion_cheat_file_clear(&file); }
  LoadedCheatFile(const LoadedCheatFile &) = delete;
  LoadedCheatFile &operator=(const LoadedCheatFile &) = delete;
};

/** Routes explicit read-only browse requests and one writable runtime session. */
class CheatService {
public:
  static CheatService &instance();

  void ensureDir();
  int exportList(const CheatRequest &request, const std::string &out_path);

  int toggle(const std::string &session_id, const std::string &cheat_key,
             bool enabled, std::string &status);

  CheatService(const CheatService &) = delete;
  CheatService &operator=(const CheatService &) = delete;

private:
  struct RuntimeState {
    GameKey game;
    ProcessIdentity process;
    std::string session_id;
    game_context_t context{};
    std::vector<std::unique_ptr<LoadedCheatFile>> files;
    CheatApplier applier;
  };

  CheatService();
  ~CheatService();

  int writeListJson(const CheatListMetadata &metadata,
                    const std::vector<std::unique_ptr<LoadedCheatFile>> &files,
                    const std::string &out_path) const;
  int ensureRuntimeLocked(const CheatRequest &request);
  bool disableRuntimeLocked(const char *reason);
  void clearRuntimeLocked();

  mutable std::mutex mu_;
  std::unique_ptr<RuntimeState> runtime_;
  uint64_t next_session_id_ = 1;
};

} // namespace onion::cheats
