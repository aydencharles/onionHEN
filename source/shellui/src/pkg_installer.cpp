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

extern "C" int sceKernelLoadStartModule(const char *name, std::size_t argc,
                                        const void *argv, std::uint32_t flags,
                                        std::uint32_t opt, int *result);
extern "C" int sceKernelDlsym(int handle, const char *symbol, void **addrp);

bool has_prefix(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

bool has_pkg_suffix(std::string_view name) {
  if (name.size() < 4)
    return false;
  const std::size_t start = name.size() - 4;
  return std::tolower(static_cast<unsigned char>(name[start])) == '.' &&
         std::tolower(static_cast<unsigned char>(name[start + 1])) == 'p' &&
         std::tolower(static_cast<unsigned char>(name[start + 2])) == 'k' &&
         std::tolower(static_cast<unsigned char>(name[start + 3])) == 'g';
}

int resolve_appinst_api() {
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
}

} // namespace

bool pkg_search_path_is_valid(const std::string &path) {
  if (path.size() < 7 || path.size() > kMaxPathLength || path.front() != '/' ||
      path.find_first_of("\\\r\n") != std::string::npos)
    return false;
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

PkgInstallerScan scan_pkg_search_paths(const std::string *system_paths,
                                      std::size_t path_count) {
  PkgInstallerScan result{};
  clear_pkg_search_results();
  if (!system_paths || path_count > onion::kMaxCustomPkgSearchPaths)
    return result;

  g_scanned_system_paths.assign(system_paths, system_paths + path_count);
  for (const std::string &system_path : g_scanned_system_paths) {
    if (!pkg_search_path_is_valid(system_path)) {
      ++result.inaccessible_paths;
      continue;
    }
    const std::string shellui_path = pkg_search_path_shellui_view(system_path);
    DIR *directory = shellui_path.empty() ? nullptr
                                           : ::opendir(shellui_path.c_str());
    if (!directory) {
      ++result.inaccessible_paths;
      continue;
    }
    result.accessible = true;
    std::size_t examined_entries = 0;
    bool pkg_entry_limit_reached = false;
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
      if (result.entries.size() == kMaxPkgEntries) {
        result.truncated = true;
        pkg_entry_limit_reached = true;
        break;
      }
      std::string system_file = system_path;
      if (system_file.back() != '/')
        system_file.push_back('/');
      system_file += entry->d_name;
      result.entries.push_back({entry->d_name, std::move(shellui_file),
                                std::move(system_file), system_path});
    }
    ::closedir(directory);
    if (pkg_entry_limit_reached)
      break;
  }

  std::sort(result.entries.begin(), result.entries.end(),
            [](const PkgInstallerEntry &left, const PkgInstallerEntry &right) {
              if (left.search_path != right.search_path)
                return left.search_path < right.search_path;
              return left.name < right.name;
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
    return {false, -ENOENT};
  const PkgInstallerEntry &entry = g_scanned_entries[index];
  if (!has_pkg_suffix(entry.name) || !pkg_search_path_is_valid(entry.search_path))
    return {false, -EINVAL};
  struct stat st {};
  if (::stat(entry.shellui_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return {false, errno ? -errno : -ENOENT};

  const int init_rc = resolve_appinst_api();
  if (init_rc != 0)
    return {false, init_rc};

  AppInstallPkgInfo pkg_info{};
  PlayGoInfo playgo{};
  MetaInfo meta{entry.system_path.c_str(), "", "", "", entry.name.c_str(),
                ""};
  const int rc = g_appinst.install_by_package(&meta, &pkg_info, &playgo);
  return {rc == 0, rc};
}
