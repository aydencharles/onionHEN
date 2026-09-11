#include <onion/log.h>
#include <onion/notify.h>
#include <onion/proc_query.h>

#include "cheats/cheat_service.hpp"
#include "onion_cjson.hpp"

#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <cstdlib>
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

const char *mode_name(CheatViewMode mode) {
  return mode == CheatViewMode::Runtime ? "runtime" : "browse";
}

bool same_process(const ProcessIdentity &lhs, const ProcessIdentity &rhs) {
  return lhs.pid == rhs.pid && lhs.appid == rhs.appid &&
         lhs.process_name == rhs.process_name &&
         lhs.session_generation == rhs.session_generation;
}

bool parse_key(const std::string &serialized, CheatKey &out) {
  const size_t first = serialized.find('|');
  const size_t last = serialized.rfind('|');
  if (first == std::string::npos || first == last || last + 1 >= serialized.size()) {
    return false;
  }
  const std::string index = serialized.substr(last + 1);
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(index.c_str(), &end, 10);
  if (end == index.c_str() || *end != '\0') {
    return false;
  }
  out.source_id = serialized.substr(0, first);
  out.source_path = serialized.substr(first + 1, last - first - 1);
  out.local_index = static_cast<size_t>(parsed);
  return !out.source_path.empty();
}

void fill_context(const CheatRequest &request, game_context_t &out) {
  std::memset(&out, 0, sizeof(out));
  out.pid = request.process ? request.process->pid : 0;
  out.appid = request.process ? request.process->appid : 0;
  std::snprintf(out.title_id, sizeof(out.title_id), "%s",
                request.game.title_id.c_str());
  std::snprintf(out.version, sizeof(out.version), "%s",
                request.game.version.c_str());
  if (request.process) {
    std::snprintf(out.process_name, sizeof(out.process_name), "%s",
                  request.process->process_name.c_str());
  }
  util_game_platform_from_title_id(request.game.title_id.c_str(), out.platform,
                                   sizeof(out.platform));
}

int load_sources(const std::vector<CheatSourceDescriptor> &sources,
                 std::vector<std::unique_ptr<LoadedCheatFile>> &out) {
  out.clear();
  out.reserve(sources.size());
  for (const CheatSourceDescriptor &source : sources) {
    auto loaded = std::make_unique<LoadedCheatFile>();
    loaded->path = source.path;
    if (!CheatRepository::statSignature(source.path, loaded->signature)) {
      out.clear();
      return -1;
    }
    const size_t slash = source.path.find_last_of('/');
    const std::string name = source.path.substr(slash + 1);
    if (onion_cheat_parse_filename(name.c_str(), &loaded->filename) < 0 ||
        CheatRepository::loadFile(source.path, loaded->file) < 0) {
      out.clear();
      return -1;
    }
    out.push_back(std::move(loaded));
  }
  return out.empty() ? -1 : 0;
}

} // namespace

CheatService &CheatService::instance() {
  static CheatService service;
  return service;
}

CheatService::CheatService() = default;

CheatService::~CheatService() {
  std::lock_guard<std::mutex> lock(mu_);
  clearRuntimeLocked();
}

void CheatService::ensureDir() { CheatRepository::ensureCheatsDir(); }

void CheatService::clearRuntimeLocked() {
  if (!runtime_) {
    return;
  }
  (void)disableRuntimeLocked("session cleared");
  runtime_->applier.clearOwnership();
  runtime_.reset();
}

bool CheatService::disableRuntimeLocked(const char *reason) {
  if (!runtime_) {
    return true;
  }
  bool ok = true;
  for (const auto &loaded : runtime_->files) {
    for (size_t index = 0; index < loaded->file.cheat_count; ++index) {
      if (!loaded->file.cheats[index].enabled) {
        continue;
      }
      std::string status;
      if (runtime_->applier.toggle(runtime_->context, loaded->file,
                                    static_cast<int>(index), status,
                                    loaded->path) < 0) {
        ok = false;
        LOG_WARN("[service] failed to disable %s index=%zu reason=%s: %s",
                 loaded->path.c_str(), index, reason ? reason : "unknown",
                 status.c_str());
      }
    }
  }
  return ok;
}

int CheatService::ensureRuntimeLocked(const CheatRequest &request) {
  if (request.mode != CheatViewMode::Runtime || !request.process ||
      !request.process->valid()) {
    return -1;
  }

  const bool live_same_game =
      runtime_ && runtime_->game == request.game &&
      onion_proc_is_alive(runtime_->process.pid);
  const ProcessIdentity requested =
      live_same_game ? runtime_->process : *request.process;
  const bool same = live_same_game ||
                    (runtime_ && runtime_->game == request.game &&
                     same_process(runtime_->process, requested));
  const std::vector<CheatSourceDescriptor> sources =
      CheatRepository::resolveRuntime(request.game, requested);
  if (sources.empty()) {
    LOG_ERROR("[cheats] runtime sources unavailable title='%s' version='%s' "
              "process='%s' pid=%d",
              request.game.title_id.c_str(), request.game.version.c_str(),
              requested.process_name.c_str(), static_cast<int>(requested.pid));
    clearRuntimeLocked();
    return -1;
  }

  bool unchanged = same && runtime_->files.size() == sources.size();
  if (unchanged) {
    for (size_t i = 0; i < sources.size(); ++i) {
      FileSignature signature;
      if (!CheatRepository::statSignature(sources[i].path, signature) ||
          runtime_->files[i]->path != sources[i].path ||
          runtime_->files[i]->signature != signature) {
        unchanged = false;
        break;
      }
    }
  }
  if (unchanged) {
    return 0;
  }

  clearRuntimeLocked();
  auto next = std::make_unique<RuntimeState>();
  next->game = request.game;
  next->process = requested;
  next->session_id = "cheat-" + std::to_string(next_session_id_++) + "-" +
                     std::to_string(static_cast<int>(requested.pid)) + "-" +
                     std::to_string(requested.session_generation);
  CheatRequest bound = request;
  bound.process = next->process;
  fill_context(bound, next->context);
  if (load_sources(sources, next->files) < 0) {
    LOG_ERROR("[cheats] failed to load runtime sources title='%s'",
              request.game.title_id.c_str());
    return -1;
  }
  LOG_INFO("[service] runtime session=%s title=%s pid=%d files=%zu",
           next->session_id.c_str(), request.game.title_id.c_str(),
           static_cast<int>(requested.pid), next->files.size());
  runtime_ = std::move(next);
  return 0;
}

int CheatService::writeListJson(
    const CheatListMetadata &metadata,
    const std::vector<std::unique_ptr<LoadedCheatFile>> &files,
    const std::string &out_path) const {
  cJSON *root = cJSON_CreateObject();
  cJSON *authors = nullptr;
  cJSON *cheats = nullptr;
  cJSON *groups = nullptr;
  if (!root || !cJSON_AddStringToObject(root, "mode", mode_name(metadata.mode)) ||
      !cJSON_AddStringToObject(root, "sessionId", metadata.session_id.c_str()) ||
      !cJSON_AddBoolToObject(root, "canToggle", metadata.can_toggle) ||
      !cJSON_AddStringToObject(root, "name",
                               files.empty() ? "" : files.front()->file.name) ||
      !(authors = cJSON_AddArrayToObject(root, "authors")) ||
      !(cheats = cJSON_AddArrayToObject(root, "cheats")) ||
      !(groups = cJSON_AddArrayToObject(root, "groups"))) {
    cJSON_Delete(root);
    return -1;
  }

  std::set<std::string> seen_authors;
  for (const auto &loaded : files) {
    for (size_t i = 0; i < loaded->file.author_count; ++i) {
      if (!seen_authors.insert(loaded->file.authors[i]).second) {
        continue;
      }
      cJSON *author = cJSON_CreateString(loaded->file.authors[i]);
      if (!author || !cJSON_AddItemToArray(authors, author)) {
        cJSON_Delete(author);
        cJSON_Delete(root);
        return -1;
      }
    }
  }

  for (const auto &loaded : files) {
    cJSON *group = cJSON_CreateObject();
    cJSON *group_authors = nullptr;
    cJSON *group_cheats = nullptr;
    const char *extension =
        onion_cheat_extension_for_rank(loaded->filename.extension_rank);
    std::string format = extension ? extension : "";
    for (char &ch : format) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (!group || !cJSON_AddStringToObject(group, "format", format.c_str()) ||
        !cJSON_AddStringToObject(group, "sourceId", loaded->filename.source_id) ||
        !cJSON_AddStringToObject(group, "process", loaded->filename.process) ||
        !(group_authors = cJSON_AddArrayToObject(group, "authors")) ||
        !(group_cheats = cJSON_AddArrayToObject(group, "cheats")) ||
        !cJSON_AddItemToArray(groups, group)) {
      cJSON_Delete(group);
      cJSON_Delete(root);
      return -1;
    }
    for (size_t i = 0; i < loaded->file.author_count; ++i) {
      cJSON *author = cJSON_CreateString(loaded->file.authors[i]);
      if (!author || !cJSON_AddItemToArray(group_authors, author)) {
        cJSON_Delete(author);
        cJSON_Delete(root);
        return -1;
      }
    }
    for (size_t local = 0; local < loaded->file.cheat_count; ++local) {
      CheatKey key{loaded->path, loaded->filename.source_id, local};
      cJSON *cheat = cJSON_CreateObject();
      if (!cheat || !cJSON_AddStringToObject(cheat, "key", key.serialize().c_str()) ||
          !cJSON_AddStringToObject(cheat, "name",
                                   loaded->file.cheats[local].name) ||
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
    }
  }

  const std::string body = onion_cjson::print_owned(root);
  if (body.empty()) {
    return -1;
  }
  std::ofstream output(out_path, std::ios::trunc);
  if (!output) {
    return -1;
  }
  output.write(body.data(), static_cast<std::streamsize>(body.size()));
  return output.good() ? 0 : -1;
}

int CheatService::exportList(const CheatRequest &request,
                             const std::string &out_path) {
  std::lock_guard<std::mutex> lock(mu_);
  if (runtime_ && !onion_proc_is_alive(runtime_->process.pid)) {
    LOG_INFO("[service] runtime process pid=%d is gone; clearing session=%s",
             static_cast<int>(runtime_->process.pid),
             runtime_->session_id.c_str());
    clearRuntimeLocked();
  }
  if (request.game.title_id.empty() || request.game.version.empty()) {
    return -1;
  }
  if (request.mode == CheatViewMode::Browse) {
    if (request.process) {
      LOG_ERROR("[cheats] browse request must not include process identity");
      return -1;
    }
    const std::vector<CheatSourceDescriptor> sources =
        CheatRepository::resolveBrowse(request.game);
    std::vector<std::unique_ptr<LoadedCheatFile>> files;
    if (load_sources(sources, files) < 0) {
      return -1;
    }
    return writeListJson({CheatViewMode::Browse, {}, false}, files, out_path);
  }
  if (ensureRuntimeLocked(request) < 0) {
    return -1;
  }
  return writeListJson({CheatViewMode::Runtime, runtime_->session_id, true},
                       runtime_->files, out_path);
}

int CheatService::toggle(const std::string &session_id,
                         const std::string &serialized_key, bool enabled,
                         std::string &status) {
  std::lock_guard<std::mutex> lock(mu_);
  status.clear();
  if (!runtime_ || session_id.empty() || runtime_->session_id != session_id) {
    status = status_tr("notify.cheats.invalid_mapping");
    return -1;
  }
  CheatKey key;
  if (!parse_key(serialized_key, key)) {
    status = status_tr("notify.cheats.invalid_mapping");
    return -1;
  }
  for (const auto &loaded : runtime_->files) {
    if (loaded->path != key.source_path ||
        std::strcmp(loaded->filename.source_id, key.source_id.c_str()) != 0 ||
        key.local_index >= loaded->file.cheat_count) {
      continue;
    }
    FileSignature current;
    if (!CheatRepository::statSignature(loaded->path, current) ||
        current != loaded->signature) {
      status = status_tr("notify.cheats.invalid_mapping");
      LOG_WARN("[service] rejected stale cheat key for %s", loaded->path.c_str());
      return -1;
    }
    auto &entry = loaded->file.cheats[key.local_index];
    if (entry.enabled == enabled) {
      status = std::string(entry.name) + (enabled ? " -> enabled" : " -> disabled");
      return 0;
    }
    return runtime_->applier.toggle(runtime_->context, loaded->file,
                                    static_cast<int>(key.local_index), status,
                                    loaded->path);
  }
  status = status_tr("notify.cheats.invalid_mapping");
  return -1;
}

} // namespace onion::cheats
