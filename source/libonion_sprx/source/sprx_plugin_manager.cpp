#include <onion/sprx_plugin_manager.hpp>

#include <onion/log.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <limits.h>
#include <mutex>
#include <set>
#include <sys/stat.h>

namespace onion::sprx {
namespace {

void append_issues(std::vector<SprxCatalogIssue> *dst,
                   const std::vector<SprxCatalogIssue> &src) {
  if (dst)
    dst->insert(dst->end(), src.begin(), src.end());
}

std::string_view basename_of(std::string_view path) noexcept {
  const size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string canonical_path(std::string_view path) {
  const std::string input(path);
  char resolved[PATH_MAX] = {};
  if (realpath(input.c_str(), resolved))
    return std::string(resolved);
  return input;
}

std::string canonical_module_name(std::string_view path) {
  return std::string(basename_of(canonical_path(path)));
}

bool has_sprx_extension(std::string_view name) noexcept {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string_view::npos)
    return false;
  const std::string_view suffix = name.substr(dot);
  if (suffix.size() != 5 && suffix.size() != 4)
    return false;
  const char *expected = suffix.size() == 5 ? ".sprx" : ".prx";
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(suffix[i])) != expected[i])
      return false;
  }
  return true;
}

bool is_regular_file(const std::string &path) noexcept {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string unique_discovered_id(std::string_view base,
                                 const std::set<std::string> &used) {
  std::string candidate(base);
  if (!used.contains(candidate))
    return candidate;
  for (int suffix = 2; suffix < 1000; ++suffix) {
    const std::string tag = "-" + std::to_string(suffix);
    const size_t max_base = tag.size() < 31 ? 31 - tag.size() : 1;
    std::string trimmed = candidate.substr(0, std::min(candidate.size(), max_base));
    while (!trimmed.empty() && (trimmed.back() == '-' || trimmed.back() == '.'))
      trimmed.pop_back();
    if (trimmed.empty())
      trimmed = "m";
    std::string next = trimmed + tag;
    if (!used.contains(next))
      return next;
  }
  return candidate;
}

} // namespace

SprxPluginManager::SprxPluginManager(IRemoteSprxRuntime &runtime,
                                     ITargetAccessPolicy &access_policy,
                                     ISprxTargetProvider &target_provider) noexcept
    : runtime_(runtime), access_policy_(access_policy),
      target_provider_(target_provider) {}

void SprxPluginManager::set_scan_directory(std::string_view dir) {
  std::lock_guard<std::mutex> lock(mutex_);
  scan_dir_.assign(dir);
  while (scan_dir_.size() > 1 && scan_dir_.back() == '/')
    scan_dir_.pop_back();
}

bool SprxPluginManager::load_catalog(
    std::string_view path, std::vector<SprxCatalogIssue> *issues) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (issues)
    issues->clear();
  return catalog_store_.load(path, issues);
}

SprxOperationResult SprxPluginManager::reload_catalog(
    std::vector<SprxCatalogIssue> *issues) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (issues)
    issues->clear();
  const std::string path = catalog_store_.path();
  if (path.empty())
    return {false, "catalog path is not configured"};
  struct stat st {};
  if (stat(path.c_str(), &st) != 0 && errno == ENOENT) {
    (void)catalog_store_.reload(nullptr);
    return {true, {}};
  }
  std::vector<SprxCatalogIssue> local_issues;
  if (!catalog_store_.reload(&local_issues)) {
    append_issues(issues, local_issues);
    const std::string message =
        local_issues.empty() ? "cannot reload SPRX catalog"
                             : local_issues.front().message;
    return {false, message};
  }
  return {true, {}};
}

SprxStartupReport SprxPluginManager::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  return start_locked();
}

SprxStartupReport SprxPluginManager::reconcile() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!catalog_store_.path().empty()) {
    std::vector<SprxCatalogIssue> issues;
    if (!catalog_store_.reload(&issues)) {
      return {false, std::move(issues), {}};
    }
  }
  return start_locked();
}

SprxStartupReport SprxPluginManager::start_locked() {
  SprxStartupReport report;
  report.catalog_loaded = catalog_store_.loaded();
  if (!report.catalog_loaded)
    return report;

  SprxTarget target;
  if (!target_provider_.current(&target)) {
    LOG_DEBUG("[sprx-manager] no current game target");
    return report;
  }

  const SprxCatalog catalog = catalog_store_.snapshot();
  std::vector<SprxCatalogIssue> order_issues;
  const auto order = catalog.startup_order(target.title_id, &order_issues);
  append_issues(&report.issues, order_issues);
  if (order.empty())
    return report;

  TitleIdAllowlist allowlist;
  if (!allowlist.add_exact(target.title_id)) {
    report.issues.push_back({0, {}, "invalid current target Title ID"});
    return report;
  }
  SprxLoader loader(runtime_, access_policy_, allowlist);
  std::set<std::string> failed;
  for (const SprxManifestEntry *entry : order) {
    SprxPluginResult item;
    item.id = entry->id;
    for (const std::string &dependency : entry->dependencies) {
      if (failed.contains(dependency)) {
        item.skipped_dependency = true;
        failed.insert(entry->id);
        break;
      }
    }
    if (!item.skipped_dependency) {
      LoadRequest request;
      request.pid = target.pid;
      request.title_id = target.title_id;
      request.path = entry->path;
      item.load = loader.load(request);
      if (!item.load.succeeded())
        failed.insert(entry->id);
    }
    report.results.push_back(std::move(item));
  }
  return report;
}

std::vector<SprxPluginManager::DiscoveredFile>
SprxPluginManager::discover_unconfigured_locked(
    const SprxCatalog &catalog) const {
  std::vector<DiscoveredFile> discovered;
  if (scan_dir_.empty())
    return discovered;

  DIR *dir = opendir(scan_dir_.c_str());
  if (!dir)
    return discovered;

  std::set<std::string> configured_paths;
  std::set<std::string> used_ids;
  for (const SprxManifestEntry &entry : catalog.entries()) {
    configured_paths.insert(entry.path);
    configured_paths.insert(canonical_path(entry.path));
    used_ids.insert(entry.id);
  }

  std::vector<std::string> filenames;
  while (struct dirent *de = readdir(dir)) {
    const std::string_view name(de->d_name);
    if (name.empty() || name.front() == '.' || !has_sprx_extension(name))
      continue;
    filenames.emplace_back(name);
  }
  closedir(dir);

  std::sort(filenames.begin(), filenames.end());
  for (const std::string &name : filenames) {
    const std::string full = scan_dir_ + "/" + name;
    if (!is_regular_file(full))
      continue;
    if (configured_paths.contains(full) ||
        configured_paths.contains(canonical_path(full)))
      continue;
    const std::string base_id = SprxCatalog::make_id_from_filename(name);
    const std::string id = unique_discovered_id(base_id, used_ids);
    used_ids.insert(id);
    discovered.push_back({id, full});
  }
  return discovered;
}

std::vector<SprxInventoryEntry> SprxPluginManager::inventory() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SprxInventoryEntry> entries = catalog_store_.inventory();
  const SprxCatalog catalog = catalog_store_.snapshot();
  const auto discovered = discover_unconfigured_locked(catalog);
  for (const DiscoveredFile &file : discovered) {
    SprxInventoryEntry item;
    item.id = file.id;
    item.path = file.path;
    item.enabled = false;
    item.auto_start = false;
    item.priority = 0;
    item.configured = false;
    entries.push_back(std::move(item));
  }

  SprxTarget target;
  if (!target_provider_.current(&target))
    return entries;

  for (SprxInventoryEntry &item : entries) {
    item.current_target_title_id = target.title_id;
    const SprxManifestEntry *entry =
        item.configured ? catalog.find(item.id) : nullptr;
    if (entry && catalog.matches(*entry, target.title_id))
      item.matches_current_target = true;
    if (!item.configured || item.matches_current_target) {
      ModuleInfo module;
      const std::string module_name = canonical_module_name(item.path);
      item.loaded_for_current_target =
          !module_name.empty() &&
          runtime_.find_loaded(target.pid, module_name, &module);
    }
  }
  return entries;
}

SprxOperationResult SprxPluginManager::load_now(std::string_view id) {
  std::lock_guard<std::mutex> lock(mutex_);
  SprxTarget target;
  if (!target_provider_.current(&target))
    return {false, "no running game"};

  TitleIdAllowlist allowlist;
  if (!allowlist.add_exact(target.title_id))
    return {false, "invalid current target Title ID"};
  SprxLoader loader(runtime_, access_policy_, allowlist);

  const SprxCatalog catalog = catalog_store_.snapshot();
  if (catalog.find(id) != nullptr) {
    std::vector<SprxCatalogIssue> issues;
    const auto order = catalog.load_order_for(id, target.title_id, &issues);
    if (order.empty()) {
      const std::string message =
          issues.empty() ? "cannot resolve SPRX load order"
                         : issues.front().message;
      return {false, message};
    }
    for (const SprxManifestEntry *entry : order) {
      ModuleInfo module;
      const std::string module_name = canonical_module_name(entry->path);
      if (!module_name.empty() &&
          runtime_.find_loaded(target.pid, module_name, &module))
        continue;
      LoadRequest request;
      request.pid = target.pid;
      request.title_id = target.title_id;
      request.path = entry->path;
      const LoadResult result = loader.load(request);
      if (!result.succeeded()) {
        return {false, entry->id + ": " + format_load_error(result)};
      }
    }
    return {true, {}};
  }

  const auto discovered = discover_unconfigured_locked(catalog);
  const auto it = std::find_if(discovered.begin(), discovered.end(),
                               [&](const DiscoveredFile &file) {
                                 return file.id == id;
                               });
  if (it == discovered.end())
    return {false, "SPRX module not found"};

  ModuleInfo module;
  const std::string module_name = canonical_module_name(it->path);
  if (!module_name.empty() &&
      runtime_.find_loaded(target.pid, module_name, &module))
    return {true, {}};

  LoadRequest request;
  request.pid = target.pid;
  request.title_id = target.title_id;
  request.path = it->path;
  const LoadResult result = loader.load(request);
  if (!result.succeeded())
    return {false, format_load_error(result)};
  return {true, {}};
}

SprxOperationResult SprxPluginManager::set_enabled(std::string_view id,
                                                    bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  return catalog_store_.set_enabled(id, enabled);
}

SprxOperationResult SprxPluginManager::set_auto_start(std::string_view id,
                                                      bool auto_start) {
  std::lock_guard<std::mutex> lock(mutex_);
  return catalog_store_.set_auto_start(id, auto_start);
}

SprxOperationResult SprxPluginManager::bind_target(std::string_view id,
                                                   SprxTargetScope scope) {
  std::lock_guard<std::mutex> lock(mutex_);
  const SprxCatalog catalog = catalog_store_.snapshot();
  SprxManifestEntry entry;
  if (const SprxManifestEntry *existing = catalog.find(id)) {
    entry = *existing;
  } else {
    const auto discovered = discover_unconfigured_locked(catalog);
    const auto it = std::find_if(discovered.begin(), discovered.end(),
                                 [&](const DiscoveredFile &file) {
                                   return file.id == id;
                                 });
    if (it == discovered.end())
      return {false, "SPRX module not found"};
    entry.id = it->id;
    entry.path = it->path;
    entry.priority = 0;
  }

  if (scope == SprxTargetScope::CurrentGame) {
    SprxTarget target;
    if (!target_provider_.current(&target))
      return {false, "no running game"};
    entry.exact_title_ids = {target.title_id};
    entry.title_id_prefixes.clear();
  } else {
    entry.exact_title_ids.clear();
    entry.title_id_prefixes = {"PPSA", "CUSA"};
  }
  entry.enabled = true;
  entry.auto_start = true;
  return catalog_store_.upsert(entry);
}

SprxOperationResult SprxPluginManager::remove(std::string_view id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const SprxCatalog catalog = catalog_store_.snapshot();
  const SprxManifestEntry *entry = catalog.find(id);
  if (!entry)
    return {false, "SPRX plugin is not declared"};
  SprxTarget target;
  if (target_provider_.current(&target) &&
      catalog.matches(*entry, target.title_id)) {
    ModuleInfo module;
    const std::string module_name = canonical_module_name(entry->path);
    if (!module_name.empty() && runtime_.find_loaded(target.pid, module_name,
                                                     &module)) {
      return {false, "SPRX plugin is loaded in the current game"};
    }
  }
  return catalog_store_.remove(id);
}

void SprxPluginManager::stop() noexcept {
  /* SPRX modules belong to the target process; unload is intentionally absent. */
}

} // namespace onion::sprx
