#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cheats/cheat_engine.h"
#include "cheats/cheat_types.hpp"
#include "cheats/runtime.h"

namespace onion::cheats {

struct FileSignature {
  std::string path;
  uint64_t size = 0;
  uint64_t inode = 0;
  int64_t mtime = 0;
  int64_t mtime_nsec = 0;
  int64_t ctime = 0;
  int64_t ctime_nsec = 0;

  bool sameIdentity(const FileSignature &o) const {
    return size == o.size && inode == o.inode && mtime == o.mtime &&
           mtime_nsec == o.mtime_nsec && ctime == o.ctime &&
           ctime_nsec == o.ctime_nsec;
  }
  bool operator==(const FileSignature &o) const {
    return path == o.path && sameIdentity(o);
  }
  bool operator!=(const FileSignature &o) const { return !(*this == o); }
};

struct CheatSourceDescriptor {
  std::string path;
  std::string source_id;
  std::string process;
  int extension_rank = -1;
};

/**
 * Resolves flat cheat paths and loads format-specific files
 * via CheatParserFactory (Strategy: json / shn / mc4 / ShnExt).
 */
class CheatRepository {
public:
  static std::vector<CheatSourceDescriptor>
  resolveBrowse(const GameKey &game);

  static std::vector<CheatSourceDescriptor>
  resolveRuntime(const GameKey &game, const ProcessIdentity &process);

  static bool fileExists(const std::string &path);
  static bool statSignature(const std::string &path, FileSignature &out);

  /** Load path into out (clears out first). 0 = ok. */
  static int loadFile(const std::string &path, onion_cheat_file_t &out);

  static void ensureCheatsDir();
  /** Copy HENCC json/shn/mc4 files into ONION_CHEATS_DIR, keeping names. */
  static int flattenInstallTree(const std::string &root,
                                onion_cheat_progress_fn progress,
                                void *progress_user,
                                onion_cheat_cancel_fn should_cancel,
                                void *cancel_user);
};

} // namespace onion::cheats
