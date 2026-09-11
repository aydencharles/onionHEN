#include <onion/fs.h>
#include "cheats/cheat_repository.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <algorithm>
#include <strings.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#include <onion/log.h>
#include "cheats/i_cheat_parser.hpp"
#include "cheats/cheat_engine.h"
#include "cheats/runtime.h"

namespace onion::cheats {

namespace {

void uppercaseAscii(char *value) {
  if (value == nullptr) {
    return;
  }
  for (size_t i = 0; value[i] != '\0'; ++i) {
    value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
  }
}

int64_t statMtimeNsec(const struct stat &st) {
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#else
  return static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
}

int64_t statCtimeNsec(const struct stat &st) {
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_ctimespec.tv_nsec);
#else
  return static_cast<int64_t>(st.st_ctim.tv_nsec);
#endif
}

void fillSignature(FileSignature &out, const std::string &path,
                   const struct stat &st) {
  out.path = path;
  out.size = static_cast<uint64_t>(st.st_size);
  out.inode = static_cast<uint64_t>(st.st_ino);
  out.mtime = static_cast<int64_t>(st.st_mtime);
  out.mtime_nsec = statMtimeNsec(st);
  out.ctime = static_cast<int64_t>(st.st_ctime);
  out.ctime_nsec = statCtimeNsec(st);
}

bool cheatDirectoryAvailable() {
  struct stat st {};
  if (::stat(ONION_CHEATS_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
    return false;
  }
  return true;
}

bool isRegularEntry(const char *directory, const struct dirent *entry) {
  if (directory == nullptr || entry == nullptr) {
    return false;
  }
#ifdef DT_DIR
  if (entry->d_type == DT_DIR) {
    return false;
  }
#endif
#ifdef DT_REG
  if (entry->d_type == DT_REG) {
    return true;
  }
#endif

  char path[512];
  const int written =
      std::snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(path)) {
    return false;
  }
  struct stat st {};
  return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

struct ScannedCandidate {
  onion_cheat_filename_t parts{};
  std::string name;
};

std::vector<ScannedCandidate> scanMatchingPaths(
    const char *title_id, const std::string &version, std::string_view process,
    bool allow_any_process) {
  std::vector<ScannedCandidate> matches;
  DIR *directory = ::opendir(ONION_CHEATS_DIR);
  if (directory == nullptr) {
    return matches;
  }

  while (const struct dirent *entry = ::readdir(directory)) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    onion_cheat_filename_t parts{};
    if (onion_cheat_parse_filename(entry->d_name, &parts) < 0) {
      LOG_DEBUG("[cheats] skip %s: filename parse failed", entry->d_name);
      continue;
    }
    if (strcasecmp(parts.title_id, title_id) != 0 ||
        std::strcmp(parts.version, version.c_str()) != 0) {
      continue;
    }
    if (!isRegularEntry(ONION_CHEATS_DIR, entry)) {
      LOG_DEBUG("[cheats] skip %s: not a regular file", entry->d_name);
      continue;
    }
    if (!allow_any_process &&
        !onion_cheat_filename_compatible(&parts, process.data())) {
      LOG_DEBUG("[cheats] skip %s: process mismatch file_process='%s' "
                "runtime_process='%s' suffix='%s' source_id='%s'",
                entry->d_name, parts.process, process.data(), parts.suffix,
                parts.source_id);
      continue;
    }
    LOG_DEBUG("[cheats] match %s: process='%s' source_id='%s'%s", entry->d_name,
              parts.process, parts.source_id,
              allow_any_process ? " (title browsing)" : "");
    matches.push_back({parts, entry->d_name});
  }
  ::closedir(directory);
  std::sort(matches.begin(), matches.end(),
            [](const ScannedCandidate &lhs, const ScannedCandidate &rhs) {
              return onion_cheat_filename_compare(&lhs.parts, lhs.name.c_str(),
                                                  &rhs.parts,
                                                  rhs.name.c_str()) < 0;
            });
  return matches;
}

std::vector<CheatSourceDescriptor> toDescriptors(
    const std::vector<ScannedCandidate> &candidates) {
  std::vector<CheatSourceDescriptor> result;
  result.reserve(candidates.size());
  for (const ScannedCandidate &candidate : candidates) {
    CheatSourceDescriptor descriptor;
    descriptor.path = std::string(ONION_CHEATS_DIR) + "/" + candidate.name;
    descriptor.source_id = candidate.parts.source_id;
    descriptor.process = candidate.parts.process;
    descriptor.extension_rank = candidate.parts.extension_rank;
    result.push_back(std::move(descriptor));
  }
  return result;
}

bool normalizeGame(const GameKey &game, std::string &title_id,
                   std::string &version) {
  if (game.title_id.empty() || game.version.empty() ||
      game.version == "unknown") {
    return false;
  }
  char title[32];
  char ver[32];
  std::snprintf(title, sizeof(title), "%s", game.title_id.c_str());
  uppercaseAscii(title);
  onion_cheat_normalize_filename_token(game.version.c_str(), ver, sizeof(ver));
  title_id = title;
  version = ver;
  return !title_id.empty() && !version.empty();
}

} // namespace

std::vector<CheatSourceDescriptor> CheatRepository::resolveBrowse(
    const GameKey &game) {
  std::string title_id;
  std::string version;
  if (!normalizeGame(game, title_id, version) ||
      !cheatDirectoryAvailable()) {
    return {};
  }
  return toDescriptors(
      scanMatchingPaths(title_id.c_str(), version, {}, true));
}

std::vector<CheatSourceDescriptor> CheatRepository::resolveRuntime(
    const GameKey &game, const ProcessIdentity &process_identity) {
  std::string title_id;
  std::string version;
  char process[ONION_CHEAT_PROCESS_LEN] = {};
  onion_cheat_normalize_filename_token(process_identity.process_name.c_str(),
                                        process, sizeof(process));
  if (!normalizeGame(game, title_id, version) || process[0] == '\0' ||
      !cheatDirectoryAvailable()) {
    return {};
  }
  return toDescriptors(
      scanMatchingPaths(title_id.c_str(), version, process, false));
}

bool CheatRepository::fileExists(const std::string &path) {
  return !path.empty() && if_exists(path.c_str());
}

bool CheatRepository::statSignature(const std::string &path,
                                    FileSignature &out) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }
  fillSignature(out, path, st);
  return true;
}

int CheatRepository::loadFile(const std::string &path, onion_cheat_file_t &out) {
  return CheatParserFactory::loadFile(path, out);
}

void CheatRepository::ensureCheatsDir() {
  ::mkdir(ONION_DATA_ROOT, 0777);
  ::mkdir(ONION_CHEATS_DIR, 0777);
}

int CheatRepository::flattenInstallTree(const std::string &root,
                                        onion_cheat_progress_fn progress,
                                        void *progress_user,
                                        onion_cheat_cancel_fn should_cancel,
                                        void *cancel_user) {
  return onion_cheat_flatten_install_tree_cancellable(
      root.c_str(), progress, progress_user, should_cancel, cancel_user);
}

} // namespace onion::cheats
