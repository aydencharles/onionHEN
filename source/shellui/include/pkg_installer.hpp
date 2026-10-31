/* Copyright (C) 2025 OnionHEN / LightningMods
 * Custom PKG search-path scanning and AppInstUtil submission.
 */
#pragma once

#include <cstddef>
#include <array>
#include <string>
#include <vector>

#include <onion/settings.hpp>

struct PkgInstallerEntry {
  std::string name;
  std::string shellui_path;
  std::string system_path;
  std::string search_path;
};

struct PkgInstallerScan {
  bool accessible = false;
  bool truncated = false;
  std::size_t inaccessible_paths = 0;
  std::vector<PkgInstallerEntry> entries;
};

struct PkgInstallerResult {
  bool started = false;
  int error_code = 0;
};

bool pkg_search_path_is_valid(const std::string &path);
std::string pkg_search_path_shellui_view(const std::string &system_path);
PkgInstallerScan scan_pkg_search_paths(const std::string *system_paths,
                                       std::size_t path_count);
void set_pkg_search_path_draft(const std::string &path);
const std::string &pkg_search_path_draft();
void clear_pkg_search_results();
PkgInstallerResult install_scanned_pkg(std::size_t index,
                                       const std::string *system_paths,
                                       std::size_t path_count);
