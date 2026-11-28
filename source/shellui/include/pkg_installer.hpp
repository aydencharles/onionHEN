/* Copyright (C) 2025 OnionHEN / LightningMods
 * PKG path validation, ShellUI view mapping, and AppInstUtil submission.
 */
#pragma once

#include <string>

struct PkgInstallerResult {
  bool started = false;
  int error_code = 0;
  std::string name;
};

std::string normalize_pkg_search_path(const std::string &path);
bool pkg_search_path_is_valid(const std::string &path);
std::string pkg_search_path_shellui_view(const std::string &system_path);
bool pkg_search_path_exists(const std::string &system_path);
PkgInstallerResult install_pkg_file(const std::string &system_path,
                                    const std::string &shellui_path,
                                    const std::string &name);
