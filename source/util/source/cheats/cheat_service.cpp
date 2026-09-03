#include <onion/log.h>
#include <onion/notify.h>
#include "cheats/cheat_service.hpp"

#include "onion_cjson.hpp"

#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>

extern "C" {
int sceKernelGetProcessName(int pid, char *name);
}

namespace onion::cheats {
namespace {

std::string status_tr(const char *key, ...) {
  char buf[384];
  va_list ap;
  va_start(ap, key);
  onion_notify_format(buf, sizeof(buf), 0, key, ap);
  va_end(ap);
  return buf;
}

} // namespace

CheatService &CheatService::instance() {
  static CheatService svc;
  return svc;
}

CheatService::CheatService() {
  std::memset(&game_, 0, sizeof(game_));
}

CheatService::~CheatService() {
  std::lock_guard<std::mutex> lock(mu_);
  (void)disableEnabledLocked("service shutdown");
  clearFilesLocked();
  applier_.clearOwnership();
}

void CheatService::ensureDir() { CheatRepository::ensureCheatsDir(); }

void CheatService::onGameExec(pid_t pid, const char *title_id, int appid) {
  std::lock_guard<std::mutex> lock(mu_);
  (void)disableEnabledLocked("game exec");
  clearFilesLocked();
  /* The previous process is being replaced; its ranges cannot be live in the
   * new process and must not accumulate across game launches. */
  applier_.clearOwnership();
  has_tracked_game_ = true;
  tracked_pid_ = pid;
  std::memset(&game_, 0, sizeof(game_));
  game_.pid = pid;
  game_.appid = appid;
  if (title_id) {
    std::snprintf(game_.title_id, sizeof(game_.title_id), "%s", title_id);
  }
  LOG_INFO("[service] cheat track exec title=%s pid=%d",
               title_id ? title_id : "?", (int)pid);
}

void CheatService::onGameExit(pid_t pid) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!has_tracked_game_ || tracked_pid_ != pid) {
    return;
  }
  LOG_INFO("[service] cheat track exit title=%s pid=%d", game_.title_id,
               (int)pid);
  has_tracked_game_ = false;
  tracked_pid_ = 0;
  (void)disableEnabledLocked("game exit");
  clearFilesLocked();
  applier_.clearOwnership();
  std::memset(&game_, 0, sizeof(game_));
}

int CheatService::fillGame(game_context_t &game, const std::string &title_id,
                           int pid, int appid) {
  if (title_id.empty()) {
    return -1;
  }
  std::memset(&game, 0, sizeof(game));
  std::snprintf(game.title_id, sizeof(game.title_id), "%s", title_id.c_str());
  game.pid = pid;
  game.appid = appid;
  util_game_platform_from_title_id(title_id.c_str(), game.platform,
                                   sizeof(game.platform));

  if (util_resolve_game_version(title_id.c_str(), game.version,
                                sizeof(game.version)) < 0) {
    std::snprintf(game.version, sizeof(game.version), "unknown");
  }

  game_context_t live{};
  if (util_get_running_bigapp(&live) == 0 && title_id == live.title_id) {
    std::snprintf(game.process_name, sizeof(game.process_name), "%s",
                  live.process_name);
    if (game.appid == 0) {
      game.appid = live.appid;
    }
    if (game.pid <= 0) {
      game.pid = live.pid;
    }
  } else if (pid > 0) {
    sceKernelGetProcessName(pid, game.process_name);
  }
  return 0;
}

bool CheatService::disableEnabledLocked(const char *reason) {
  if (!loaded_) {
    return true;
  }
  bool all_disabled = true;
  for (const auto &loaded : files_) {
    if (!loaded) {
      continue;
    }
    for (size_t i = 0; i < loaded->file.cheat_count; ++i) {
      if (!loaded->file.cheats[i].enabled) {
        continue;
      }
      std::string status;
      if (applier_.toggle(game_, loaded->file, static_cast<int>(i), status,
                          loaded->path) < 0) {
        all_disabled = false;
        LOG_WARN("[service] disable stale cheat %zu (%s): %s", i,
                 reason ? reason : "?", status.c_str());
      }
    }
  }
  return all_disabled;
}

void CheatService::clearFilesLocked() {
  files_.clear();
  cheat_map_.clear();
  loaded_ = false;
}

int CheatService::refreshLocked(const game_context_t &game) {
  const bool same_context =
      loaded_ && std::strcmp(game_.title_id, game.title_id) == 0 &&
      std::strcmp(game_.version, game.version) == 0 &&
      std::strcmp(game_.process_name, game.process_name) == 0 &&
      game_.pid == game.pid;
  const std::vector<std::string> paths = CheatRepository::resolvePaths(game);
  if (paths.empty()) {
    if (!disableEnabledLocked("path unresolved")) {
      return -1;
    }
    clearFilesLocked();
    return -1;
  }

  bool unchanged = loaded_ && paths.size() == files_.size();
  std::vector<FileSignature> signatures;
  signatures.reserve(paths.size());
  if (unchanged) {
    for (size_t i = 0; i < paths.size(); ++i) {
      FileSignature sig;
      if (!CheatRepository::statSignature(paths[i], sig) ||
          !files_[i] || files_[i]->path != paths[i] ||
          files_[i]->signature != sig) {
        unchanged = false;
        break;
      }
      signatures.push_back(std::move(sig));
    }
  }

  if (unchanged && same_context) {
    game_ = game;
    return 0;
  }

  if (!unchanged) {
    signatures.clear();
    for (const std::string &path : paths) {
      FileSignature sig;
      if (!CheatRepository::statSignature(path, sig)) {
        if (!disableEnabledLocked("stat failed")) {
          return -1;
        }
        clearFilesLocked();
        return -1;
      }
      signatures.push_back(std::move(sig));
    }
  }

  if (!disableEnabledLocked("reload")) {
    return -1;
  }
  clearFilesLocked();
  game_ = game;
  files_.reserve(paths.size());
  for (size_t i = 0; i < paths.size(); ++i) {
    auto loaded = std::make_unique<LoadedFile>();
    loaded->path = paths[i];
    loaded->signature = signatures[i];
    const std::string name = paths[i].substr(paths[i].find_last_of('/') + 1);
    if (onion_cheat_parse_filename(name.c_str(), &loaded->filename) < 0 ||
        CheatRepository::loadFile(paths[i], loaded->file) < 0) {
      clearFilesLocked();
      return -1;
    }
    files_.push_back(std::move(loaded));
  }
  for (size_t file_index = 0; file_index < files_.size(); ++file_index) {
    for (size_t cheat_index = 0;
         cheat_index < files_[file_index]->file.cheat_count; ++cheat_index) {
      cheat_map_.push_back({file_index, cheat_index});
    }
    LOG_INFO("[service] loaded %s cheats=%zu", files_[file_index]->path.c_str(),
             files_[file_index]->file.cheat_count);
  }
  loaded_ = !files_.empty();
  return 0;
}

int CheatService::writeListJson(const std::string &out_path) const {
  cJSON *root = cJSON_CreateObject();
  cJSON *authors = nullptr;
  cJSON *cheats = nullptr;
  cJSON *groups = nullptr;
  const char *game_name = files_.empty() ? "" : files_.front()->file.name;
  if (!root || !cJSON_AddStringToObject(root, "name", game_name) ||
      !(authors = cJSON_AddArrayToObject(root, "authors")) ||
      !(cheats = cJSON_AddArrayToObject(root, "cheats")) ||
      !(groups = cJSON_AddArrayToObject(root, "groups"))) {
    cJSON_Delete(root);
    return -1;
  }

  std::set<std::string> seen_authors;
  for (const auto &loaded : files_) {
    for (size_t a = 0; a < loaded->file.author_count; ++a) {
      if (!seen_authors.insert(loaded->file.authors[a]).second) {
        continue;
      }
      cJSON *author = cJSON_CreateString(loaded->file.authors[a]);
      if (!author || !cJSON_AddItemToArray(authors, author)) {
        cJSON_Delete(author);
        cJSON_Delete(root);
        return -1;
      }
    }
  }

  size_t global_id = 0;
  for (size_t file_index = 0; file_index < files_.size(); ++file_index) {
    const auto &loaded = files_[file_index];
    cJSON *group = cJSON_CreateObject();
    cJSON *group_authors = nullptr;
    cJSON *group_cheats = nullptr;
    const char *extension =
        onion_cheat_extension_for_rank(loaded->filename.extension_rank);
    std::string format = extension ? extension : "";
    for (char &ch : format) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (!group ||
        !cJSON_AddStringToObject(group, "format", format.c_str()) ||
        !cJSON_AddStringToObject(group, "sourceId",
                                 loaded->filename.source_id) ||
        !cJSON_AddStringToObject(group, "process", loaded->filename.process) ||
        !(group_authors = cJSON_AddArrayToObject(group, "authors")) ||
        !(group_cheats = cJSON_AddArrayToObject(group, "cheats")) ||
        !cJSON_AddItemToArray(groups, group)) {
      cJSON_Delete(group);
      cJSON_Delete(root);
      return -1;
    }
    for (size_t a = 0; a < loaded->file.author_count; ++a) {
      cJSON *author = cJSON_CreateString(loaded->file.authors[a]);
      if (!author || !cJSON_AddItemToArray(group_authors, author)) {
        cJSON_Delete(author);
        cJSON_Delete(root);
        return -1;
      }
    }
    for (size_t local = 0; local < loaded->file.cheat_count; ++local) {
      cJSON *cheat = cJSON_CreateObject();
      if (!cheat || !cJSON_AddStringToObject(cheat, "name",
                                   loaded->file.cheats[local].name) ||
          !cJSON_AddNumberToObject(cheat, "id", global_id) ||
          !cJSON_AddBoolToObject(cheat, "enabled",
                                 loaded->file.cheats[local].enabled) ||
          !cJSON_AddStringToObject(cheat, "description",
                                   loaded->file.cheats[local].description)) {
        cJSON_Delete(cheat);
        cJSON_Delete(root);
        return -1;
      }
      cJSON *flat = cJSON_Duplicate(cheat, 1);
      if (!flat) {
        cJSON_Delete(cheat);
        cJSON_Delete(root);
        return -1;
      }
      if (!cJSON_AddItemToArray(group_cheats, cheat)) {
        cJSON_Delete(flat);
        cJSON_Delete(cheat);
        cJSON_Delete(root);
        return -1;
      }
      if (!cJSON_AddItemToArray(cheats, flat)) {
        cJSON_Delete(flat);
        cJSON_Delete(root);
        return -1;
      }
      ++global_id;
    }
  }

  const std::string body = onion_cjson::print_owned(root);
  if (body.empty()) {
    return -1;
  }
  std::ofstream ofs(out_path, std::ios::trunc);
  if (!ofs) {
    return -1;
  }
  ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
  return ofs.good() ? 0 : -1;
}

int CheatService::exportList(const std::string &title_id, int pid, int appid,
                             const std::string &out_path) {
  game_context_t game{};
  if (fillGame(game, title_id, pid, appid) < 0) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (refreshLocked(game) < 0) {
    return -1;
  }
  return writeListJson(out_path);
}

int CheatService::toggle(int pid, int appid, const std::string &title_id,
                         int index, std::string &status) {
  game_context_t game{};
  status.clear();
  if (fillGame(game, title_id, pid, appid) < 0) {
    status = status_tr("notify.cheats.invalid_game");
    return -1;
  }
  if (pid > 0) {
    game.pid = pid;
  }
  if (appid != 0) {
    game.appid = appid;
  }

  std::lock_guard<std::mutex> lock(mu_);
  if (refreshLocked(game) < 0) {
    status = status_tr("notify.cheats.load_failed");
    return -1;
  }
  if (index < 0 || static_cast<size_t>(index) >= cheat_map_.size()) {
    status = status_tr("notify.cheats.invalid_index");
    return -1;
  }
  game_.pid = game.pid;
  game_.appid = game.appid;
  const CheatRef ref = cheat_map_[static_cast<size_t>(index)];
  if (ref.file_index >= files_.size() || !files_[ref.file_index] ||
      ref.cheat_index >= files_[ref.file_index]->file.cheat_count) {
    status = status_tr("notify.cheats.invalid_mapping");
    return -1;
  }
  auto &loaded = *files_[ref.file_index];
  return applier_.toggle(game_, loaded.file,
                         static_cast<int>(ref.cheat_index), status,
                         loaded.path);
}

} // namespace onion::cheats
