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

  bool load_catalog(std::string_view path,
                    std::vector<SprxCatalogIssue> *issues = nullptr);
  SprxStartupReport start();
  SprxStartupReport reconcile();
  void stop() noexcept;

  const SprxCatalog &catalog() const noexcept { return catalog_; }

private:
  SprxStartupReport start_locked();

  IRemoteSprxRuntime &runtime_;
  ITargetAccessPolicy &access_policy_;
  ISprxTargetProvider &target_provider_;
  SprxCatalog catalog_;
  std::string catalog_path_;
  bool catalog_loaded_ = false;
  mutable std::mutex mutex_;
};

} // namespace onion::sprx
