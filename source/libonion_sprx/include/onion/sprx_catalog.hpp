/* Copyright (C) 2026 OnionHEN / LightningMods */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace onion::sprx {

struct SprxManifestEntry {
  std::string id;
  std::string path;
  std::vector<std::string> exact_title_ids;
  std::vector<std::string> title_id_prefixes;
  bool enabled = true;
  bool auto_start = false;
  int32_t priority = 0;
  std::vector<std::string> dependencies;
};

struct SprxCatalogIssue {
  size_t line = 0;
  std::string plugin_id;
  std::string message;
};

struct SprxOperationResult {
  bool success = false;
  std::string error;

  explicit operator bool() const { return success; }
};

struct SprxInventoryEntry {
  std::string id;
  std::string path;
  bool enabled = true;
  bool auto_start = false;
  int32_t priority = 0;
  bool matches_current_target = false;
  bool loaded_for_current_target = false;
};

/**
 * Strict, dependency-aware catalog for externally installed SPRX/PRX files.
 *
 * Manifest format:
 *
 *   [plugin.fps-overlay]
 *   path=/data/OnionHEN/sprx/fps-overlay.sprx
 *   exact_title_ids=CUSA12345,CUSA67890
 *   title_id_prefixes=PPSA
 *   auto_start=true
 *   priority=100
 *   dependencies=common-runtime
 */
class SprxCatalog final {
public:
  bool parse(std::string_view text,
             std::vector<SprxCatalogIssue> *issues = nullptr);
  bool load_file(std::string_view path,
                 std::vector<SprxCatalogIssue> *issues = nullptr);

  const std::vector<SprxManifestEntry> &entries() const noexcept {
    return entries_;
  }

  const SprxManifestEntry *find(std::string_view id) const noexcept;

  bool set_enabled(std::string_view id, bool enabled) noexcept;
  bool remove(std::string_view id) noexcept;
  std::string serialize() const;

  bool matches(const SprxManifestEntry &entry,
               std::string_view title_id) const noexcept;

  /**
   * Returns deterministic startup order for a Title ID. Dependencies are
   * included even when they are not marked auto_start. An empty result means
   * no matching auto-start entries or a dependency/cycle validation failure.
   */
  std::vector<const SprxManifestEntry *>
  startup_order(std::string_view title_id,
                std::vector<SprxCatalogIssue> *issues = nullptr) const;

private:
  std::vector<SprxManifestEntry> entries_;
};

/** Serialized catalog persistence. Mutations are atomically written to disk. */
class SprxCatalogStore final {
public:
  bool load(std::string_view path,
            std::vector<SprxCatalogIssue> *issues = nullptr);
  bool reload(std::vector<SprxCatalogIssue> *issues = nullptr);

  std::vector<SprxInventoryEntry> inventory() const;
  /** Returns a consistent copy; callers never observe mutable store state. */
  SprxCatalog snapshot() const;
  SprxOperationResult set_enabled(std::string_view id, bool enabled);
  SprxOperationResult remove(std::string_view id);

  std::string path() const;
  bool loaded() const;

private:
  SprxOperationResult persist_locked();

  mutable std::mutex mutex_;
  SprxCatalog catalog_;
  std::string path_;
  bool loaded_ = false;
};

} // namespace onion::sprx
