#pragma once

#include <onion/sprx_catalog.hpp>
#include <onion/sprx_loader.hpp>

#include <string>
#include <string_view>
#include <mutex>
#include <vector>

namespace onion::sprx {

struct SprxTarget {
  pid_t pid = -1;
  std::string title_id;
};

class ISprxTargetProvider {
public:
  virtual ~ISprxTargetProvider() = default;
  virtual bool current(SprxTarget *out) noexcept = 0;
};

struct SprxPluginResult {
  std::string id;
  LoadResult load;
  bool skipped_dependency = false;
};

struct SprxStartupReport {
  bool catalog_loaded = false;
  std::vector<SprxCatalogIssue> issues;
  std::vector<SprxPluginResult> results;
};

/** Coordinates catalog selection and loading for one current game target. */
class SprxPluginManager final {
public:
  SprxPluginManager(IRemoteSprxRuntime &runtime,
                    ITargetAccessPolicy &access_policy,
                    ISprxTargetProvider &target_provider) noexcept;

  void set_scan_directory(std::string_view dir);
  bool load_catalog(std::string_view path,
                    std::vector<SprxCatalogIssue> *issues = nullptr);
  SprxOperationResult reload_catalog(
      std::vector<SprxCatalogIssue> *issues = nullptr);
  SprxStartupReport start();
  SprxStartupReport reconcile();
  std::vector<SprxInventoryEntry> inventory() const;
  SprxOperationResult load_now(std::string_view id);
  SprxOperationResult set_enabled(std::string_view id, bool enabled);
  SprxOperationResult set_auto_start(std::string_view id, bool auto_start);
  SprxOperationResult bind_target(std::string_view id, SprxTargetScope scope);
  SprxOperationResult remove(std::string_view id);
  void stop() noexcept;

  SprxCatalog catalog() const { return catalog_store_.snapshot(); }

private:
  struct DiscoveredFile {
    std::string id;
    std::string path;
  };

  SprxStartupReport start_locked();
  std::vector<DiscoveredFile> discover_unconfigured_locked(
      const SprxCatalog &catalog) const;

  IRemoteSprxRuntime &runtime_;
  ITargetAccessPolicy &access_policy_;
  ISprxTargetProvider &target_provider_;
  SprxCatalogStore catalog_store_;
  std::string scan_dir_;
  mutable std::mutex mutex_;
};

} // namespace onion::sprx
