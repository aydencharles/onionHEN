/* Copyright (C) 2025 OnionHEN / LightningMods
 * Pure host-testable page builders for the Custom PKG Installer UI.
 */
#include "pkg_installer_pages.hpp"

#include "toolbox_i18n.hpp"

#include <cstdio>
#include <string>

namespace onion::shellui::pkg_pages {

std::string format_pkg_size(std::uint64_t bytes) {
  char buf[32] = {};
  constexpr double kKiB = 1024.0;
  constexpr double kMiB = kKiB * 1024.0;
  constexpr double kGiB = kMiB * 1024.0;
  if (bytes >= static_cast<std::uint64_t>(kGiB)) {
    std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / kGiB);
  } else if (bytes >= static_cast<std::uint64_t>(kMiB)) {
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / kMiB);
  } else if (bytes >= static_cast<std::uint64_t>(kKiB)) {
    std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / kKiB);
  } else {
    std::snprintf(buf, sizeof(buf), "%llu B",
                  static_cast<unsigned long long>(bytes));
  }
  return buf;
}

std::string format_pkg_subtitle(const PkgInstallerEntry &entry) {
  if (entry.size_bytes == 0)
    return entry.search_path;
  return format_pkg_size(entry.size_bytes) + " · " + entry.search_path;
}

std::string format_folders_summary(const PkgInstallerScan &scan,
                                   std::size_t path_count) {
  if (path_count == 0)
    return toolbox_i18n::tr("pkg.custom.no_paths");
  if (scan.inaccessible_paths > 0) {
    return toolbox_i18n::format(
        "pkg.custom.folders_summary_warn_fmt",
        static_cast<int>(path_count),
        static_cast<int>(scan.inaccessible_paths));
  }
  return toolbox_i18n::format("pkg.custom.folders_summary_fmt",
                              static_cast<int>(path_count),
                              static_cast<int>(scan.entries.size()));
}

std::string format_folder_status(const PkgFolderStatus &folder) {
  if (!folder.accessible)
    return toolbox_i18n::tr("pkg.custom.folder_inaccessible");
  return toolbox_i18n::format("pkg.custom.folder_count_fmt",
                              static_cast<int>(folder.pkg_count));
}

void fill_custom_pkg_installer_page(ps5ui::Page &page,
                                    const PkgInstallerScan &scan,
                                    std::size_t path_count) {
  if (path_count == 0) {
    page.label("id_pkg_no_paths", toolbox_i18n::tr("pkg.custom.no_paths"));
  } else if (!scan.accessible) {
    page.label("id_pkg_inaccessible",
               toolbox_i18n::tr("pkg.custom.inaccessible"));
  } else if (scan.entries.empty() && !scan.truncated) {
    page.label("id_pkg_empty", toolbox_i18n::tr("pkg.custom.empty"));
  } else {
    for (std::size_t i = 0; i < scan.entries.size(); ++i) {
      const PkgInstallerEntry &entry = scan.entries[i];
      page.button(std::string(kPkgInstallPrefix) + std::to_string(i),
                  entry.name, format_pkg_subtitle(entry),
                  toolbox_i18n::tr("pkg.custom.package_help"));
    }
    if (scan.truncated) {
      page.label("id_pkg_truncated", toolbox_i18n::tr("pkg.custom.truncated"));
    }
  }

  page.button(std::string(kPkgRescanId), toolbox_i18n::tr("pkg.custom.rescan"),
              std::nullopt, toolbox_i18n::tr("pkg.custom.rescan_sub"))
      .link(std::string(kPkgFoldersLinkId),
            toolbox_i18n::tr("pkg.custom.manage_folders"),
            "custom_pkg_folders.xml",
            format_folders_summary(scan, path_count));
}

void fill_custom_pkg_folders_page(
    ps5ui::Page &page, const PkgInstallerScan &scan,
    const std::string *quick_paths, std::size_t quick_count,
    const std::vector<std::size_t> &quick_add_indices) {
  if (scan.folders.empty()) {
    page.label("id_pkg_no_paths", toolbox_i18n::tr("pkg.custom.no_paths"));
  } else {
    for (std::size_t i = 0; i < scan.folders.size(); ++i) {
      const PkgFolderStatus &folder = scan.folders[i];
      page.button(std::string(kPkgRemovePrefix) + std::to_string(i),
                  folder.system_path, format_folder_status(folder),
                  toolbox_i18n::tr("pkg.custom.remove"), std::nullopt,
                  ps5ui::Style::None,
                  toolbox_i18n::format("pkg.custom.remove_confirm_fmt",
                                       folder.system_path.c_str()));
    }
  }

  const bool can_add_more =
      scan.folders.size() < onion::kMaxCustomPkgSearchPaths;
  if (can_add_more) {
    for (std::size_t idx : quick_add_indices) {
      if (!quick_paths || idx >= quick_count || quick_paths[idx].empty())
        continue;
      page.button(std::string(kPkgQuickAddPrefix) + std::to_string(idx),
                  toolbox_i18n::format("pkg.custom.quick_add_fmt",
                                       quick_paths[idx].c_str()),
                  std::nullopt, toolbox_i18n::tr("pkg.custom.quick_add_sub"));
    }
    page.link(std::string(kPkgAddCustomLinkId),
              toolbox_i18n::tr("pkg.custom.add_custom"),
              "custom_pkg_add_folder.xml",
              toolbox_i18n::tr("pkg.custom.path_input_hint"));
  }
}

void fill_custom_pkg_add_folder_page(ps5ui::Page &page,
                                     const std::string &draft) {
  page.text_field(std::string(kPkgPathInputId),
                  toolbox_i18n::tr("pkg.custom.path_input"),
                  toolbox_i18n::tr("pkg.custom.path_input_hint"), "url", "1",
                  "512", std::nullopt, std::nullopt, std::nullopt, draft)
      .label("id_pkg_add_help", toolbox_i18n::tr("pkg.custom.add_help"));
}

} // namespace onion::shellui::pkg_pages
