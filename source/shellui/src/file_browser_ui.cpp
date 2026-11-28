/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Thin PS5 ShellUI adapter for the lightweight file browser.
 * Uses native Settings page stacking (link -> press Circle to go back) so each
 * screen contains only storage devices or directory entries with zero clutter.
 */
#include "file_browser_ui.hpp"

#include "file_browser.hpp"
#include "pkg_installer.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"
#include "usb_storage.hpp"

#include <onion/ipc_client.hpp>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace onion::shellui::file_browser {
namespace {

struct DirTarget {
  FilterMode mode = FilterMode::All;
  RootLocation root;
  std::string rel_path;
};

struct ActiveDirPage {
  DirTarget target;
  BrowserListing listing;
  std::map<std::string, std::uint64_t> child_tokens;
};

FilterMode g_session_mode = FilterMode::All;
std::uint64_t g_next_token = 1;
std::vector<std::uint64_t> g_page_stack; // 0 = root storage list, >0 = dir token
std::map<std::string, std::uint64_t> g_root_tokens;
std::map<std::uint64_t, DirTarget> g_targets;
std::map<std::uint64_t, ActiveDirPage> g_active_dirs;
std::map<unsigned, usb_storage::Device> g_usb_devices;
const DestinationDirs g_destinations{};

std::string node_xml_name(std::uint64_t token) {
  return toolbox::file_browser_node_xml(token);
}

std::string root_page_title(FilterMode mode) {
  return toolbox_i18n::tr(mode == FilterMode::Pkg ? "pkg.installer.custom"
                                                  : "file_browser.title");
}

std::vector<RootLocation> discover_storage_roots() {
  std::vector<RootLocation> roots;
  g_usb_devices.clear();

  if (pkg_search_path_exists("/data")) {
    roots.push_back({toolbox_i18n::tr("file_browser.root.internal"), "/data",
                     pkg_search_path_shellui_view("/data"), false, 0});
  }

  std::vector<usb_storage::Device> devices;
  if (usb_storage::list_devices(devices) >= 0) {
    for (const auto device : devices) {
      usb_storage::with_device(device, [&](const usb_storage::Mapping &view) {
        if (g_usb_devices.contains(view.usb_index))
          return;
        g_usb_devices.emplace(view.usb_index, device);
        roots.push_back({toolbox_i18n::format("file_browser.usb_root_fmt",
                                              static_cast<int>(view.usb_index)),
                         "/mnt/usb" + std::to_string(view.usb_index),
                         view.path, true, view.usb_index});
      });
    }
  }

  if (pkg_search_path_exists("/mnt/ext0")) {
    roots.push_back({toolbox_i18n::tr("file_browser.root.external"),
                     "/mnt/ext0", pkg_search_path_shellui_view("/mnt/ext0"),
                     false, 0});
  }

  return roots;
}

std::uint64_t ensure_root_token(const RootLocation &root) {
  auto it = g_root_tokens.find(root.system_path);
  if (it != g_root_tokens.end()) {
    g_targets[it->second] = {g_session_mode, root, {}};
    return it->second;
  }
  const std::uint64_t token = g_next_token++;
  g_root_tokens.emplace(root.system_path, token);
  g_targets[token] = {g_session_mode, root, {}};
  return token;
}

std::uint64_t ensure_child_token(ActiveDirPage &page,
                                 const std::string &child_name) {
  auto it = page.child_tokens.find(child_name);
  if (it != page.child_tokens.end())
    return it->second;
  const std::uint64_t token = g_next_token++;
  page.child_tokens.emplace(child_name, token);
  DirTarget child = page.target;
  child.rel_path = join_path(page.target.rel_path, child_name);
  g_targets[token] = std::move(child);
  return token;
}

BrowserListing scan_target(const DirTarget &target) {
  if (target.root.is_usb) {
    const auto it = g_usb_devices.find(target.root.usb_index);
    if (it == g_usb_devices.end())
      return {};
    BrowserListing listing{};
    usb_storage::with_device(it->second, [&](const usb_storage::Mapping &view) {
      if (view.usb_index != target.root.usb_index)
        return;
      listing = list_directory(join_path(view.path, target.rel_path),
                               target.mode, g_destinations);
    });
    return listing;
  }
  return list_directory(join_path(target.root.shellui_path, target.rel_path),
                        target.mode, g_destinations);
}

bool build_root_model(ps5ui::Node &model) {
  const std::vector<RootLocation> roots = discover_storage_roots();
  ps5ui::Page page("id_fb_page_root", root_page_title(g_session_mode));
  fill_file_browser_root_page(
      page, roots, [&](std::size_t index, const std::string &) {
        return node_xml_name(ensure_root_token(roots[index]));
      });
  model = page.root();
  return true;
}

bool build_dir_model(std::uint64_t token, ps5ui::Node &model) {
  auto page_it = g_active_dirs.find(token);
  if (page_it == g_active_dirs.end())
    return false;
  ActiveDirPage &active = page_it->second;
  active.listing = scan_target(active.target);
  const std::string title =
      join_path(active.target.root.system_path, active.target.rel_path);
  ps5ui::Page page("id_fb_page_" + std::to_string(token), title);
  fill_file_browser_dir_page(
      page, token, active.listing,
      [&](std::size_t, const std::string &dir_name) {
        return node_xml_name(ensure_child_token(active, dir_name));
      });
  model = page.root();
  return true;
}

bool parse_file_button_id(std::string_view id, std::uint64_t *token_out,
                          std::size_t *index_out) {
  if (!id.starts_with(kFilePrefix))
    return false;
  const std::string_view tail = id.substr(kFilePrefix.size());
  const std::size_t sep = tail.find('_');
  if (sep == std::string_view::npos || sep == 0 || sep + 1 >= tail.size())
    return false;
  const std::string token_str(tail.substr(0, sep));
  const std::string index_str(tail.substr(sep + 1));
  char *end1 = nullptr;
  char *end2 = nullptr;
  const unsigned long long token = std::strtoull(token_str.c_str(), &end1, 10);
  const unsigned long index = std::strtoul(index_str.c_str(), &end2, 10);
  if (!end1 || *end1 != '\0' || !end2 || *end2 != '\0' || token == 0 ||
      index > 100000)
    return false;
  *token_out = static_cast<std::uint64_t>(token);
  *index_out = static_cast<std::size_t>(index);
  return true;
}

void notify_import_status(ImportStatus status, const std::string &name,
                          const std::string &dest_path) {
  switch (status) {
  case ImportStatus::Completed:
    notify("notify.file_browser.imported_fmt", name.c_str(), dest_path.c_str());
    break;
  case ImportStatus::Replaced:
    notify("notify.file_browser.replaced_fmt", name.c_str(), dest_path.c_str());
    break;
  case ImportStatus::AlreadySameFile:
    notify("notify.file_browser.already_in_place_fmt", name.c_str());
    break;
  case ImportStatus::AlreadyExists:
    notify("notify.file_browser.exists_fmt", name.c_str());
    break;
  case ImportStatus::TooLarge:
    notify("notify.file_browser.too_large_fmt", name.c_str());
    break;
  case ImportStatus::UnsupportedKind:
  case ImportStatus::InvalidName:
  case ImportStatus::ReadFailed:
  case ImportStatus::WriteFailed:
    notify("notify.file_browser.import_failed_fmt", name.c_str());
    break;
  }
}

void handle_file_action(const DirTarget &target, const BrowserEntry &entry,
                        MonoObject *page_instance) {
  const std::string system_dir =
      join_path(target.root.system_path, target.rel_path);
  const std::string system_file = join_path(system_dir, entry.name);

  if (entry.kind == FileKind::Other) {
    const std::string size = format_byte_size(entry.size_bytes);
    notify("notify.file_browser.file_info_fmt", entry.name.c_str(),
           size.c_str(), system_file.c_str());
    return;
  }

  auto execute_with_shellui_file = [&](const std::string &shellui_file) {
    if (entry.kind == FileKind::Pkg) {
      const PkgInstallerResult result =
          install_pkg_file(system_file, shellui_file, entry.name);
      if (result.started) {
        if (!result.name.empty())
          notify("notify.pkg.install_started_fmt", result.name.c_str());
        else
          notify("notify.pkg.install_started");
      } else {
        notify("notify.pkg.install_failed", result.error_code);
      }
      return;
    }

    std::string dest_path;
    const ImportStatus status = import_file_to_destination(
        shellui_file, entry.name, entry.kind, g_destinations,
        entry.already_installed, &dest_path);
    if (entry.kind == FileKind::Sprx &&
        (status == ImportStatus::Completed ||
         status == ImportStatus::Replaced)) {
      std::string reload_err;
      (void)IPC_Client::getInstance(false).ReloadSprx(&reload_err);
    }
    notify_import_status(status, entry.name, dest_path);
    settings::refresh(page_instance);
  };

  if (target.root.is_usb) {
    const auto it = g_usb_devices.find(target.root.usb_index);
    if (it == g_usb_devices.end()) {
      notify("notify.file_browser.import_failed_fmt", entry.name.c_str());
      return;
    }
    bool ran = false;
    usb_storage::with_device(it->second, [&](const usb_storage::Mapping &view) {
      if (view.usb_index != target.root.usb_index)
        return;
      ran = true;
      execute_with_shellui_file(
          join_path(join_path(view.path, target.rel_path), entry.name));
    });
    if (!ran)
      notify("notify.file_browser.import_failed_fmt", entry.name.c_str());
    return;
  }

  execute_with_shellui_file(
      join_path(join_path(target.root.shellui_path, target.rel_path),
                entry.name));
}

} // namespace

void generate_root_xml(std::string &xml_buffer, std::string_view mode_key) {
  g_session_mode = filter_mode_from_key(mode_key);
  g_page_stack.clear();
  g_root_tokens.clear();
  g_targets.clear();
  g_active_dirs.clear();
  g_page_stack.push_back(0);

  ps5ui::Node model;
  build_root_model(model);
  xml_buffer = settings::publish(model, build_root_model);
}

void generate_node_xml(std::string &xml_buffer, std::uint64_t token) {
  const auto it = g_targets.find(token);
  if (it == g_targets.end()) {
    ps5ui::Page page("id_fb_page_" + std::to_string(token),
                     toolbox_i18n::tr("file_browser.title"));
    page.label("id_fb_inaccessible",
               toolbox_i18n::tr("file_browser.inaccessible"));
    xml_buffer = page.build();
    return;
  }

  ActiveDirPage active;
  active.target = it->second;
  g_active_dirs[token] = std::move(active);
  g_page_stack.push_back(token);

  ps5ui::Node model;
  build_dir_model(token, model);
  xml_buffer = settings::publish(
      model, [token](ps5ui::Node &next) { return build_dir_model(token, next); });
}

bool leave_active_page() {
  if (g_page_stack.empty())
    return false;
  const std::uint64_t leaving = g_page_stack.back();
  g_page_stack.pop_back();
  if (leaving != 0)
    g_active_dirs.erase(leaving);
  return !g_page_stack.empty();
}

OnPressResult onpress(OnPressContext &ctx) {
  ctx.dirty = false;

  std::uint64_t token = 0;
  std::size_t index = 0;
  if (!parse_file_button_id(ctx.id, &token, &index))
    return OnPressResult::NotMine;

  const auto it = g_active_dirs.find(token);
  if (it == g_active_dirs.end() || index >= it->second.listing.entries.size())
    return OnPressResult::Consumed;

  const DirTarget target = it->second.target;
  const BrowserEntry entry = it->second.listing.entries[index];
  if (entry.kind != FileKind::Directory)
    handle_file_action(target, entry, ctx.instance);
  return OnPressResult::Consumed;
}

} // namespace onion::shellui::file_browser
