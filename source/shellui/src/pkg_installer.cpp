/* Copyright (C) 2025 OnionHEN / LightningMods
 * PKG path validation, ShellUI view mapping, and AppInstUtil submission.
 */
#include "pkg_installer.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <dirent.h>
#include <string_view>
#include <sys/stat.h>

namespace {

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

PkgInstallerResult install_pkg_file(const std::string &system_path,
                                    const std::string &shellui_path,
                                    const std::string &name) {
  if (!has_pkg_suffix(name) || !pkg_search_path_is_valid(system_path) ||
      shellui_path.empty())
    return {false, -EINVAL, name};
  struct stat st {};
  if (::stat(shellui_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return {false, errno ? -errno : -ENOENT, name};

  const int init_rc = resolve_appinst_api();
  if (init_rc != 0)
    return {false, init_rc, name};

  AppInstallPkgInfo pkg_info{};
  PlayGoInfo playgo{};
  MetaInfo meta{system_path.c_str(), "", "", "", name.c_str(), ""};
  const int rc = g_appinst.install_by_package(&meta, &pkg_info, &playgo);
  return {rc == 0, rc, name};
}
