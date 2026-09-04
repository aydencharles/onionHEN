#include <onion/sprx_plugin_manager.hpp>

#include <onion/log.h>

#include <mutex>
#include <set>

namespace onion::sprx {
namespace {

void append_issues(std::vector<SprxCatalogIssue> *dst,
                   const std::vector<SprxCatalogIssue> &src) {
  if (dst)
    dst->insert(dst->end(), src.begin(), src.end());
}

} // namespace

SprxPluginManager::SprxPluginManager(IRemoteSprxRuntime &runtime,
                                     ITargetAccessPolicy &access_policy,
                                     ISprxTargetProvider &target_provider) noexcept
    : runtime_(runtime), access_policy_(access_policy),
      target_provider_(target_provider) {}

bool SprxPluginManager::load_catalog(
    std::string_view path, std::vector<SprxCatalogIssue> *issues) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (issues)
    issues->clear();
  SprxCatalog loaded;
  if (!loaded.load_file(path, issues)) {
    catalog_loaded_ = false;
    return false;
  }
  catalog_ = std::move(loaded);
  catalog_path_ = std::string(path);
  catalog_loaded_ = true;
  return true;
}

SprxStartupReport SprxPluginManager::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  return start_locked();
}

SprxStartupReport SprxPluginManager::reconcile() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!catalog_path_.empty()) {
    SprxCatalog loaded;
    std::vector<SprxCatalogIssue> issues;
    if (!loaded.load_file(catalog_path_, &issues)) {
      catalog_loaded_ = false;
      return {false, std::move(issues), {}};
    }
    catalog_ = std::move(loaded);
    catalog_loaded_ = true;
  }
  return start_locked();
}

SprxStartupReport SprxPluginManager::start_locked() {
  SprxStartupReport report;
  report.catalog_loaded = catalog_loaded_;
  if (!catalog_loaded_)
    return report;

  SprxTarget target;
  if (!target_provider_.current(&target)) {
    LOG_DEBUG("[sprx-manager] no current game target");
    return report;
  }

  std::vector<SprxCatalogIssue> order_issues;
  const auto order = catalog_.startup_order(target.title_id, &order_issues);
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

void SprxPluginManager::stop() noexcept {
  /* SPRX modules belong to the target process; unload is intentionally absent. */
}

} // namespace onion::sprx
