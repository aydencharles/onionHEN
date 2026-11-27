/* Copyright (C) 2025 OnionHEN / LightningMods
 * Custom PKG search-path scanning and AppInstUtil submission.
 */
#include "pkg_installer.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <dirent.h>
#include <string_view>
#include <sys/stat.h>
#include <utility>

namespace {

constexpr std::size_t kMaxPkgEntries = 64;
constexpr std::size_t kMaxExaminedPkgDirEntries = 4096;
constexpr std::size_t kMaxPathLength = 512;
constexpr std::size_t kNumLanguages = 30;
constexpr std::size_t kNumPlayGoIds = 64;
constexpr std::size_t kContentIdSize = 0x30;
constexpr std::size_t kLanguageSize = 8;
constexpr std::size_t kScenarioIdSize = 3;

struct MetaInfo {
  const char *uri;
  const char *ex_uri;
  const char *playgo_scenario_id;
  const char *content_id;
  const char *content_name;
  const char *icon_url;
};

struct AppInstallPkgInfo {
  char content_id[kContentIdSize];
  int content_type;
  int content_platform;
};

struct PlayGoInfo {
  char languages[kNumLanguages][kLanguageSize];
  char playgo_scenario_ids[kNumPlayGoIds][kScenarioIdSize];
  char content_ids[kNumPlayGoIds][kContentIdSize];
  unsigned char unknown[6480];
};

using InitializeFn = int (*)();
using InstallByPackageFn = int (*)(MetaInfo *, AppInstallPkgInfo *, PlayGoInfo *);

struct AppInstApi {
  int module_id = -1;
  InitializeFn initialize = nullptr;
  InstallByPackageFn install_by_package = nullptr;
  bool initialized = false;
};

AppInstApi g_appinst;
std::vector<std::string> g_scanned_system_paths;
std::vector<PkgInstallerEntry> g_scanned_entries;
std::string g_path_draft;

#ifndef ONION_HOST_TEST
extern "C" int sceKernelLoadStartModule(const char *name, std::size_t argc,
                                        const void *argv, std::uint32_t flags,
                                        std::uint32_t opt, int *result);
extern "C" int sceKernelDlsym(int handle, const char *symbol, void **addrp);
#endif

bool has_prefix(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

bool has_pkg_suffix(std::string_view name) {
  if (name.size() < 5 || name.front() == '.')
    return false;
  const std::size_t start = name.size() - 4;
  return std::tolower(static_cast<unsigned char>(name[start])) == '.' &&
         std::tolower(static_cast<unsigned char>(name[start + 1])) == 'p' &&
         std::tolower(static_cast<unsigned char>(name[start + 2])) == 'k' &&
         std::tolower(static_cast<unsigned char>(name[start + 3])) == 'g';
}

bool icase_less(std::string_view a, std::string_view b) {
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char ca = static_cast<unsigned char>(
        std::tolower(static_cast<unsigned char>(a[i])));
    const unsigned char cb = static_cast<unsigned char>(
        std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb)
      return ca < cb;
  }
  return a.size() < b.size();
}

int resolve_appinst_api() {
#ifdef ONION_HOST_TEST
  return -ENOSYS;
#else
  if (g_appinst.initialized)
    return 0;
  if (g_appinst.module_id < 0) {
    g_appinst.module_id = sceKernelLoadStartModule(
        "/system/common/lib/libSceAppInstUtil.sprx", 0, nullptr, 0, 0,
        nullptr);
    if (g_appinst.module_id < 0)
      return g_appinst.module_id;
  }

  void *initialize = nullptr;
  int rc = sceKernelDlsym(g_appinst.module_id, "sceAppInstUtilInitialize",
                          &initialize);
  if (rc != 0 || !initialize)
    return rc != 0 ? rc : -ENOENT;
  void *install = nullptr;
  rc = sceKernelDlsym(g_appinst.module_id, "sceAppInstUtilInstallByPackage",
                      &install);
  if (rc != 0 || !install)
    return rc != 0 ? rc : -ENOENT;

  g_appinst.initialize = reinterpret_cast<InitializeFn>(initialize);
  g_appinst.install_by_package = reinterpret_cast<InstallByPackageFn>(install);
  rc = g_appinst.initialize();
  if (rc != 0) {
    g_appinst.initialize = nullptr;
    g_appinst.install_by_package = nullptr;
    return rc;
  }
  g_appinst.initialized = true;
  return 0;
#endif
}

} // namespace

std::string normalize_pkg_search_path(const std::string &path) {
  const auto first = std::find_if_not(path.begin(), path.end(), [](char c) {
    return std::isspace(static_cast<unsigned char>(c));
  });
  const auto last = std::find_if_not(path.rbegin(), path.rend(), [](char c) {
    return std::isspace(static_cast<unsigned char>(c));
  }).base();
  std::string out = first < last ? std::string(first, last) : std::string();
  while (out.size() > 1 && out.back() == '/')
    out.pop_back();
  return out;
}

bool pkg_search_path_is_valid(const std::string &path) {
  if (path.size() < 5 || path.size() > kMaxPathLength || path.front() != '/' ||
      path.find_first_of(",\\\r\n") != std::string::npos)
    return false;
  if (path == "/data")
    return true;
  std::size_t start = std::string::npos;
  if (path.rfind("/data/", 0) == 0)
    start = 6;
  else if (path.rfind("/mnt/", 0) == 0)
    start = 5;
  else
    return false;
  while (start <= path.size()) {
    const std::size_t slash = path.find('/', start);
    const std::string_view part(path.data() + start,
                                (slash == std::string::npos ? path.size()
                                                            : slash) - start);
    if (part.empty() && slash == std::string::npos && start == path.size())
      return true;
    if (part.empty() || part == "." || part == "..")
      return false;
    if (slash == std::string::npos)
      break;
    start = slash + 1;
  }
  return true;
}

std::string pkg_search_path_shellui_view(const std::string &system_path) {
  if (has_prefix(system_path, "/data/"))
    return "/user" + system_path;
  if (system_path == "/data")
    return "/user/data";
  if (has_prefix(system_path, "/mnt/"))
    return "/" + system_path.substr(5);
  return {};
}

bool pkg_search_path_exists(const std::string &system_path) {
  if (!pkg_search_path_is_valid(system_path))
    return false;
  const std::string shellui_path = pkg_search_path_shellui_view(system_path);
  if (shellui_path.empty())
    return false;
  DIR *dir = ::opendir(shellui_path.c_str());
  if (!dir)
    return false;
  ::closedir(dir);
  return true;
}

std::vector<std::size_t>
available_quick_pkg_folder_indices(const std::string *quick_paths,
                                   std::size_t quick_count,
                                   const std::string *configured,
                                   std::size_t configured_count) {
  std::vector<std::size_t> result;
  if (!quick_paths)
    return result;
  const std::size_t limit =
      std::min(quick_count, onion::kMaxCustomPkgQuickPaths);
  for (std::size_t i = 0; i < limit; ++i) {
    const std::string candidate = normalize_pkg_search_path(quick_paths[i]);
    if (!pkg_search_path_is_valid(candidate))
      continue;
    bool already = false;
    for (std::size_t j = 0; configured && j < configured_count; ++j) {
      if (normalize_pkg_search_path(configured[j]) == candidate) {
        already = true;
        break;
      }
    }
    if (!already && pkg_search_path_exists(candidate))
      result.push_back(i);
  }
  return result;
}

PkgInstallerScan scan_pkg_search_paths(const std::string *system_paths,
                                      std::size_t path_count) {
  PkgInstallerScan result{};
  clear_pkg_search_results();
  if (!system_paths || path_count > onion::kMaxCustomPkgSearchPaths)
    return result;

  g_scanned_system_paths.assign(system_paths, system_paths + path_count);
  result.folders.reserve(path_count);
  bool pkg_entry_limit_reached = false;
  for (const std::string &system_path : g_scanned_system_paths) {
    PkgFolderStatus folder_status{system_path, false, 0};
    if (!pkg_search_path_is_valid(system_path)) {
      ++result.inaccessible_paths;
      result.folders.push_back(std::move(folder_status));
      continue;
    }
    const std::string shellui_path = pkg_search_path_shellui_view(system_path);
    DIR *directory = shellui_path.empty() ? nullptr
                                           : ::opendir(shellui_path.c_str());
    if (!directory) {
      ++result.inaccessible_paths;
      result.folders.push_back(std::move(folder_status));
      continue;
    }
    result.accessible = true;
    folder_status.accessible = true;
    std::size_t examined_entries = 0;
    while (const struct dirent *entry = ::readdir(directory)) {
      if (examined_entries >= kMaxExaminedPkgDirEntries) {
        result.truncated = true;
        break;
      }
      ++examined_entries;
      if (!has_pkg_suffix(entry->d_name))
        continue;
      std::string shellui_file = shellui_path;
      if (shellui_file.back() != '/')
        shellui_file.push_back('/');
      shellui_file += entry->d_name;
      struct stat st {};
      if (::stat(shellui_file.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        continue;
      ++folder_status.pkg_count;
      if (pkg_entry_limit_reached || result.entries.size() == kMaxPkgEntries) {
        result.truncated = true;
        pkg_entry_limit_reached = true;
        continue;
      }
      std::string system_file = system_path;
      if (system_file.back() != '/')
        system_file.push_back('/');
      system_file += entry->d_name;
      const std::uint64_t file_size =
          st.st_size > 0 ? static_cast<std::uint64_t>(st.st_size) : 0;
      result.entries.push_back({entry->d_name, std::move(shellui_file),
                                std::move(system_file), system_path,
                                file_size});
    }
    ::closedir(directory);
    result.folders.push_back(std::move(folder_status));
  }

  std::sort(result.entries.begin(), result.entries.end(),
            [](const PkgInstallerEntry &left, const PkgInstallerEntry &right) {
              if (icase_less(left.name, right.name))
                return true;
              if (icase_less(right.name, left.name))
                return false;
              return left.search_path < right.search_path;
            });
  g_scanned_entries = result.entries;
  return result;
}

void set_pkg_search_path_draft(const std::string &path) { g_path_draft = path; }

const std::string &pkg_search_path_draft() { return g_path_draft; }

void clear_pkg_search_results() {
  g_scanned_system_paths.clear();
  g_scanned_entries.clear();
}

PkgInstallerResult install_scanned_pkg(std::size_t index,
                                       const std::string *system_paths,
                                       std::size_t path_count) {
  if (!system_paths || path_count != g_scanned_system_paths.size() ||
      !std::equal(system_paths, system_paths + path_count,
                  g_scanned_system_paths.begin()) ||
      index >= g_scanned_entries.size())
    return {false, -ENOENT, {}};
  const PkgInstallerEntry &entry = g_scanned_entries[index];
  if (!has_pkg_suffix(entry.name) || !pkg_search_path_is_valid(entry.search_path))
    return {false, -EINVAL, entry.name};
  struct stat st {};
  if (::stat(entry.shellui_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return {false, errno ? -errno : -ENOENT, entry.name};

  const int init_rc = resolve_appinst_api();
  if (init_rc != 0)
    return {false, init_rc, entry.name};

  AppInstallPkgInfo pkg_info{};
  PlayGoInfo playgo{};
  MetaInfo meta{entry.system_path.c_str(), "", "", "", entry.name.c_str(),
                ""};
  const int rc = g_appinst.install_by_package(&meta, &pkg_info, &playgo);
  return {rc == 0, rc, entry.name};
}
