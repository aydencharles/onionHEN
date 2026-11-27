/* Copyright (C) 2025 OnionHEN / LightningMods
 * Custom PKG search-path scanning and AppInstUtil submission.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <onion/settings.hpp>

struct PkgInstallerEntry {
  std::string name;
  std::string shellui_path;
  std::string system_path;
  std::string search_path;
  std::uint64_t size_bytes = 0;
};

struct PkgFolderStatus {
  std::string system_path;
  bool accessible = false;
  std::size_t pkg_count = 0;
};

struct PkgInstallerScan {
  bool accessible = false;
  bool truncated = false;
  std::size_t inaccessible_paths = 0;
  std::vector<PkgFolderStatus> folders;
  std::vector<PkgInstallerEntry> entries;
};

struct PkgInstallerResult {
  bool started = false;
  int error_code = 0;
  std::string name;
};

std::string normalize_pkg_search_path(const std::string &path);
bool pkg_search_path_is_valid(const std::string &path);
std::string pkg_search_path_shellui_view(const std::string &system_path);
bool pkg_search_path_exists(const std::string &system_path);
std::vector<std::size_t>
available_quick_pkg_folder_indices(const std::string *quick_paths,
                                   std::size_t quick_count,
                                   const std::string *configured,
                                   std::size_t configured_count);
PkgInstallerScan scan_pkg_search_paths(const std::string *system_paths,
                                       std::size_t path_count);
void set_pkg_search_path_draft(const std::string &path);
const std::string &pkg_search_path_draft();
void clear_pkg_search_results();
PkgInstallerResult install_scanned_pkg(std::size_t index,
                                       const std::string *system_paths,
                                       std::size_t path_count);
