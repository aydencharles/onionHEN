/* Copyright (C) 2025 OnionHEN / LightningMods */
#include "onpress.hpp"
#include "pkg_installer.hpp"
#include "pkg_installer_pages.hpp"
#include "settings_page_refresh.hpp"

#include <cerrno>
#include <cstdlib>
#include <string>

namespace {

bool parse_index(const std::string &id, std::string_view prefix,
                 std::size_t *out) {
  if (!id.starts_with(prefix) || id.size() == prefix.size())
    return false;
  char *end = nullptr;
  const unsigned long value =
      std::strtoul(id.c_str() + prefix.size(), &end, 10);
  if (!end || *end != '\0' || value > 100000)
    return false;
  *out = static_cast<std::size_t>(value);
  return true;
}

bool append_search_path(const std::string &raw_path, std::string &added_path) {
  const std::string path = normalize_pkg_search_path(raw_path);
  if (!pkg_search_path_is_valid(path)) {
    notify("notify.pkg.path_invalid");
    return false;
  }
  for (std::size_t i = 0; i < g_settings.custom_pkg_search_path_count; ++i) {
    if (normalize_pkg_search_path(g_settings.custom_pkg_search_paths[i]) ==
        path) {
      notify("notify.pkg.path_duplicate");
      return false;
    }
  }
  if (g_settings.custom_pkg_search_path_count >=
      onion::kMaxCustomPkgSearchPaths) {
    notify("notify.pkg.path_limit");
    return false;
  }
  g_settings.custom_pkg_search_paths[g_settings.custom_pkg_search_path_count++] =
      path;
  added_path = path;
  return true;
}

OnPressResult rescan_pkgs(OnPressContext &ctx) {
  ctx.dirty = false;
  onion::shellui::settings::refresh(ctx.instance);
  notify("notify.pkg.rescanned");
  return OnPressResult::Consumed;
}

OnPressResult path_input(OnPressContext &ctx) {
  ctx.dirty = false;
  const std::string normalized = normalize_pkg_search_path(ctx.value);
  if (normalized.empty()) {
    set_pkg_search_path_draft({});
    return OnPressResult::Consumed;
  }
  std::string added;
  if (!append_search_path(normalized, added)) {
    set_pkg_search_path_draft(normalized);
    return OnPressResult::Consumed;
  }
  set_pkg_search_path_draft({});
  ctx.dirty = true;
  notify("notify.pkg.path_added_fmt", added.c_str());
  return OnPressResult::Consumed;
}

OnPressResult quick_add_path(OnPressContext &ctx) {
  ctx.dirty = false;
  std::size_t idx = 0;
  const std::size_t quick_count = std::min(
      g_settings.custom_pkg_quick_path_count, onion::kMaxCustomPkgQuickPaths);
  if (!parse_index(ctx.id, onion::shellui::pkg_pages::kPkgQuickAddPrefix,
                   &idx) ||
      idx >= quick_count) {
    return OnPressResult::Consumed;
  }
  std::string added;
  if (!append_search_path(g_settings.custom_pkg_quick_paths[idx], added))
    return OnPressResult::Consumed;
  ctx.dirty = true;
  onion::shellui::settings::refresh(ctx.instance);
  notify("notify.pkg.path_added_fmt", added.c_str());
  return OnPressResult::Consumed;
}

OnPressResult remove_path(OnPressContext &ctx) {
  std::size_t index = 0;
  if (!parse_index(ctx.id, onion::shellui::pkg_pages::kPkgRemovePrefix,
                   &index) ||
      index >= g_settings.custom_pkg_search_path_count) {
    ctx.dirty = false;
    return OnPressResult::Consumed;
  }
  const std::string removed = g_settings.custom_pkg_search_paths[index];
  for (std::size_t i = index + 1;
       i < g_settings.custom_pkg_search_path_count; ++i)
    g_settings.custom_pkg_search_paths[i - 1] =
        std::move(g_settings.custom_pkg_search_paths[i]);
  --g_settings.custom_pkg_search_path_count;
  g_settings.custom_pkg_search_paths[g_settings.custom_pkg_search_path_count]
      .clear();
  ctx.dirty = true;
  onion::shellui::settings::refresh(ctx.instance);
  notify("notify.pkg.path_removed_fmt", removed.c_str());
  return OnPressResult::Consumed;
}

OnPressResult install_pkg(OnPressContext &ctx) {
  ctx.dirty = false;
  std::size_t index = 0;
  if (!parse_index(ctx.id, onion::shellui::pkg_pages::kPkgInstallPrefix,
                   &index)) {
    notify("notify.pkg.install_failed", -EINVAL);
    return OnPressResult::Consumed;
  }
  const PkgInstallerResult result = install_scanned_pkg(
      index, g_settings.custom_pkg_search_paths.data(),
      g_settings.custom_pkg_search_path_count);
  if (result.started) {
    if (!result.name.empty())
      notify("notify.pkg.install_started_fmt", result.name.c_str());
    else
      notify("notify.pkg.install_started");
  } else {
    notify("notify.pkg.install_failed", result.error_code);
  }
  return OnPressResult::Consumed;
}

const OnPressExactEntry kExact[] = {
    {"id_pkg_rescan", rescan_pkgs},
    {"id_pkg_path_input", path_input},
};

const OnPressPrefixEntry kPrefix[] = {
    {"id_pkg_quick_add_", quick_add_path},
    {"id_pkg_remove_", remove_path},
    {"id_pkg_install_", install_pkg},
};

} // namespace

const OnPressExactEntry *onpress_pkg_installer_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}

const OnPressPrefixEntry *onpress_pkg_installer_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
