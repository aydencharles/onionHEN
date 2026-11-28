/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Thin PS5 ShellUI adapter for the reusable file browser core.
 */
#include "file_browser_ui.hpp"

#include "file_browser.hpp"
#include "pkg_installer.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_i18n.hpp"
#include "usb_storage.hpp"

#include <onion/ipc_client.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace onion::shellui::file_browser {
namespace {

constexpr const char *kPageId = "id_file_browser_page";

BrowserState g_state;
std::vector<RootLocation> g_roots;
BrowserListing g_listing;
std::map<unsigned, usb_storage::Device> g_usb_devices;
const DestinationDirs g_destinations{};

bool parse_index(std::string_view id, std::string_view prefix,
                 std::size_t *out) {
  if (!id.starts_with(prefix) || id.size() == prefix.size())
    return false;
  const std::string tail(id.substr(prefix.size()));
  char *end = nullptr;
  const unsigned long value = std::strtoul(tail.c_str(), &end, 10);
  if (!end || *end != '\0' || value > 100000)
    return false;
  *out = static_cast<std::size_t>(value);
  return true;
}

void add_local_root_if_exists(std::vector<RootLocation> &roots,
                              const std::string &raw_system_path,
                              const std::string &label = {}) {
  const std::string system_path = normalize_pkg_search_path(raw_system_path);
  if (!pkg_search_path_is_valid(system_path) ||
      !pkg_search_path_exists(system_path))
    return;
  for (const RootLocation &existing : roots) {
    if (existing.system_path == system_path)
      return;
  }
  RootLocation root;
  root.label = label.empty() ? system_path : label;
  root.system_path = system_path;
  root.shellui_path = pkg_search_path_shellui_view(system_path);
  root.is_usb = false;
  roots.push_back(std::move(root));
}

void discover_roots() {
  g_roots.clear();
  g_usb_devices.clear();

  static const std::array<const char *, 6> kDefaultLocalRoots = {
      "/data",
      "/data/OnionHEN",
      "/data/OnionHEN/pkgs",
      "/data/pkg",
      "/data/downloads",
      "/mnt/ext0",
  };
  for (const char *path : kDefaultLocalRoots) {
    add_local_root_if_exists(g_roots, path);
  }

  std::vector<usb_storage::Device> devices;
  if (usb_storage::list_devices(devices) >= 0) {
    for (const auto device : devices) {
      usb_storage::with_device(device, [&](const usb_storage::Mapping &view) {
        if (g_usb_devices.contains(view.usb_index))
          return;
        g_usb_devices.emplace(view.usb_index, device);
        RootLocation usb;
        usb.label = toolbox_i18n::format("file_browser.usb_root_fmt",
                                         static_cast<int>(view.usb_index));
        usb.system_path = "/mnt/usb" + std::to_string(view.usb_index);
        usb.shellui_path = view.path;
        usb.is_usb = true;
        usb.usb_index = view.usb_index;
        g_roots.push_back(std::move(usb));
      });
    }
  }
}

void refresh_listing() {
  g_listing = {};
  if (!g_state.in_directory())
    return;

  if (g_state.active_root->is_usb) {
    const auto it = g_usb_devices.find(g_state.active_root->usb_index);
    if (it == g_usb_devices.end())
      return;
    usb_storage::with_device(it->second, [&](const usb_storage::Mapping &view) {
      if (view.usb_index != g_state.active_root->usb_index)
        return;
      g_listing = list_directory(g_state.current_shellui_path(view.path),
                                 g_state.mode, g_destinations);
    });
    return;
  }

  g_listing =
      list_directory(g_state.current_shellui_path(), g_state.mode, g_destinations);
}

std::string page_title() {
  const char *base_key = "file_browser.title";
  if (g_state.filter_locked) {
    switch (g_state.mode) {
    case FilterMode::Pkg:
      base_key = "pkg.installer.custom";
      break;
    case FilterMode::Payload:
      base_key = "file_browser.title.payload";
      break;
    case FilterMode::Patch:
      base_key = "file_browser.title.patch";
      break;
    case FilterMode::All:
      base_key = "file_browser.title";
      break;
    }
  }
  const std::string base = toolbox_i18n::tr(base_key);
  if (!g_state.in_directory())
    return base;
  return base + " · " + g_state.current_system_path();
}

bool build_model(ps5ui::Node &model) {
  if (!g_state.in_directory()) {
    discover_roots();
    g_listing = {};
  } else {
    refresh_listing();
  }
  ps5ui::Page page(kPageId, page_title());
  fill_file_browser_page(page, g_state, g_roots, g_listing);
  model = page.root();
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

void handle_file_action(const BrowserEntry &entry, MonoObject *page_instance) {
  if (!g_state.in_directory())
    return;

  const std::string system_file =
      join_path(g_state.current_system_path(), entry.name);

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

  if (g_state.active_root->is_usb) {
    const auto it = g_usb_devices.find(g_state.active_root->usb_index);
    if (it == g_usb_devices.end()) {
      notify("notify.file_browser.import_failed_fmt", entry.name.c_str());
      return;
    }
    bool ran = false;
    usb_storage::with_device(it->second, [&](const usb_storage::Mapping &view) {
      if (view.usb_index != g_state.active_root->usb_index)
        return;
      ran = true;
      execute_with_shellui_file(
          join_path(g_state.current_shellui_path(view.path), entry.name));
    });
    if (!ran)
      notify("notify.file_browser.import_failed_fmt", entry.name.c_str());
    return;
  }

  execute_with_shellui_file(join_path(g_state.current_shellui_path(), entry.name));
}

} // namespace

void generate_xml(std::string &xml_buffer, std::string_view mode_key) {
  const FilterMode mode = filter_mode_from_key(mode_key);
  g_state.reset(mode, mode != FilterMode::All);
  ps5ui::Node model;
  build_model(model);
  xml_buffer = settings::publish(model, build_model);
}

OnPressResult onpress(OnPressContext &ctx) {
  ctx.dirty = false;

  if (ctx.id == kFilterButtonId) {
    if (!g_state.filter_locked) {
      g_state.mode = next_filter_mode(g_state.mode);
      settings::refresh(ctx.instance);
    }
    return OnPressResult::Consumed;
  }

  if (ctx.id == kUpButtonId) {
    if (g_state.navigate_up())
      settings::refresh(ctx.instance);
    return OnPressResult::Consumed;
  }

  if (ctx.id == kRefreshButtonId) {
    settings::refresh(ctx.instance);
    return OnPressResult::Consumed;
  }

  std::size_t index = 0;
  if (parse_index(ctx.id, kRootPrefix, &index)) {
    if (index < g_roots.size() && g_state.enter_root(g_roots[index]))
      settings::refresh(ctx.instance);
    return OnPressResult::Consumed;
  }

  if (parse_index(ctx.id, kDirPrefix, &index)) {
    if (index < g_listing.entries.size() &&
        g_listing.entries[index].kind == FileKind::Directory &&
        g_state.enter_subdir(g_listing.entries[index].name)) {
      settings::refresh(ctx.instance);
    }
    return OnPressResult::Consumed;
  }

  if (parse_index(ctx.id, kFilePrefix, &index)) {
    if (index < g_listing.entries.size() &&
        g_listing.entries[index].kind != FileKind::Directory) {
      const BrowserEntry selected = g_listing.entries[index];
      handle_file_action(selected, ctx.instance);
    }
    return OnPressResult::Consumed;
  }

  return OnPressResult::NotMine;
}

} // namespace onion::shellui::file_browser
