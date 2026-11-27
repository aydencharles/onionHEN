/* Copyright (C) 2025 OnionHEN / LightningMods
 * Pure host-testable page builders for the Custom PKG Installer UI.
 */
#pragma once

#include "pkg_installer.hpp"
#include "ps5_settings_ui.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::pkg_pages {

inline constexpr std::string_view kPkgRescanId = "id_pkg_rescan";
inline constexpr std::string_view kPkgFoldersLinkId = "id_pkg_folders";
inline constexpr std::string_view kPkgAddCustomLinkId = "id_pkg_add_custom";
inline constexpr std::string_view kPkgPathInputId = "id_pkg_path_input";
inline constexpr std::string_view kPkgInstallPrefix = "id_pkg_install_";
inline constexpr std::string_view kPkgRemovePrefix = "id_pkg_remove_";
inline constexpr std::string_view kPkgQuickAddPrefix = "id_pkg_quick_add_";

std::string format_pkg_size(std::uint64_t bytes);
std::string format_pkg_subtitle(const PkgInstallerEntry &entry);
std::string format_folders_summary(const PkgInstallerScan &scan,
                                   std::size_t path_count);
std::string format_folder_status(const PkgFolderStatus &folder);

void fill_custom_pkg_installer_page(ps5ui::Page &page,
                                    const PkgInstallerScan &scan,
                                    std::size_t path_count);

void fill_custom_pkg_folders_page(
    ps5ui::Page &page, const PkgInstallerScan &scan,
    const std::string *quick_paths, std::size_t quick_count,
    const std::vector<std::size_t> &quick_add_indices);

void fill_custom_pkg_add_folder_page(ps5ui::Page &page,
                                     const std::string &draft);

} // namespace onion::shellui::pkg_pages
