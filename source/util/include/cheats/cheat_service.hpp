#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cheats/cheat_applier.hpp"
#include "cheats/cheat_repository.hpp"
#include "cheats/cheat_engine.h"
#include "cheats/runtime.h"
#include "util_platform.h"

namespace onion::cheats {

/**
 * Facade for IPC and lifecycle. Thread-safe process-wide service.
 *
 * Patterns: Facade + Singleton (process lifetime) + RAII locking.
 */
class CheatService {
public:
  static CheatService &instance();

  void ensureDir();
  void onGameExec(pid_t pid, const char *title_id, int appid);
  void onGameExit(pid_t pid);

  /** Write ShellUI list JSON to outPath. 0 = ok. */
  int exportList(const std::string &title_id, int pid, int appid,
                 const std::string &out_path);

  int toggle(int pid, int appid, const std::string &title_id, int index,
             std::string &status);

  CheatService(const CheatService &) = delete;
  CheatService &operator=(const CheatService &) = delete;

private:
  struct LoadedFile {
    std::string path;
    FileSignature signature;
    onion_cheat_filename_t filename{};
    onion_cheat_file_t file{};

    LoadedFile() { file.master_code_id = -1; }
    ~LoadedFile() { onion_cheat_file_clear(&file); }
    LoadedFile(const LoadedFile &) = delete;
    LoadedFile &operator=(const LoadedFile &) = delete;
  };

  struct CheatRef {
    size_t file_index = 0;
    size_t cheat_index = 0;
  };

  CheatService();
  ~CheatService();

  int fillGame(game_context_t &game, const std::string &title_id, int pid,
               int appid);
  int refreshLocked(const game_context_t &game);
  bool disableEnabledLocked(const char *reason);
  void clearFilesLocked();
  int writeListJson(const std::string &out_path) const;

  mutable std::mutex mu_;
  bool loaded_ = false;
  bool has_tracked_game_ = false;
  pid_t tracked_pid_ = 0;
  game_context_t game_{};
  std::vector<std::unique_ptr<LoadedFile>> files_;
  std::vector<CheatRef> cheat_map_;
  CheatApplier applier_{};
};

} // namespace onion::cheats
