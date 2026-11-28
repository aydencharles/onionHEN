/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Pure, host-testable core for the lightweight ShellUI file browser.
 */
#include "file_browser.hpp"

#include "toolbox_i18n.hpp"
#include "usb_payload_importer.hpp"

#include <onion/fs.h>
#include <onion/plugin_manager.hpp>
#include <onion/tree.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <span>
#include <sys/stat.h>
#include <utility>

namespace onion::shellui::file_browser {
namespace {

constexpr std::size_t kMaxPatchImportBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxSprxImportBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxPluginInspectBytes = 16ULL * 1024ULL * 1024ULL;

bool ascii_iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const unsigned char ca = static_cast<unsigned char>(
        std::tolower(static_cast<unsigned char>(a[i])));
    const unsigned char cb = static_cast<unsigned char>(
        std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb)
      return false;
  }
  return true;
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

std::string normalized_payload_name(const std::string &name) {
  if (name.size() > 4 && ascii_iequals(name.substr(name.size() - 4), ".elf"))
    return name.substr(0, name.size() - 4) + ".elf";
  return name;
}

const std::string *destination_dir_for(FileKind kind,
                                       const DestinationDirs &destinations) {
  switch (kind) {
  case FileKind::Payload:
    return &destinations.payloads;
  case FileKind::Plugin:
    return &destinations.plugins;
  case FileKind::Patch:
    return &destinations.cheats;
  case FileKind::Sprx:
    return &destinations.sprx;
  case FileKind::Directory:
  case FileKind::Pkg:
  case FileKind::Other:
    return nullptr;
  }
  return nullptr;
}

std::string destination_path_for(const std::string &filename, FileKind kind,
                                 const DestinationDirs &destinations) {
  const std::string *dir = destination_dir_for(kind, destinations);
  if (!dir || dir->empty())
    return {};
  const bool is_elf_target =
      kind == FileKind::Payload || kind == FileKind::Plugin;
  const std::string target_name =
      is_elf_target ? normalized_payload_name(filename) : filename;
  return join_path(*dir, target_name);
}

bool same_inode(const std::string &lhs, const std::string &rhs) {
  struct stat a {};
  struct stat b {};
  if (::stat(lhs.c_str(), &a) != 0 || ::stat(rhs.c_str(), &b) != 0)
    return false;
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

std::string display_system_path_for(const std::string &shellui_path) {
  if (shellui_path.rfind("/user/data/", 0) == 0)
    return shellui_path.substr(5);
  if (shellui_path == "/user/data")
    return "/data";
  return shellui_path;
}

bool inspect_plugin_elf(const std::string &shellui_path,
                        std::string &plugin_id_out) {
  std::uint8_t *buffer = nullptr;
  std::size_t size = 0;
  if (!read_file_alloc(shellui_path.c_str(), kMaxPluginInspectBytes, &buffer,
                       &size))
    return false;
  const onion::plugin::Inspection inspection = onion::plugin::inspect_elf(
      std::span<const std::uint8_t>(buffer, size));
  std::free(buffer);
  if (!inspection)
    return false;
  plugin_id_out = inspection.descriptor.plugin_id;
  return true;
}

const char *action_hint_key(FileKind kind, bool already_installed) {
  if (already_installed && kind != FileKind::Pkg &&
      kind != FileKind::Directory && kind != FileKind::Other) {
    return "file_browser.action.already_imported";
  }
  switch (kind) {
  case FileKind::Pkg:
    return "file_browser.action.install_pkg";
  case FileKind::Payload:
    return "file_browser.action.import_payload";
  case FileKind::Plugin:
    return "file_browser.action.import_plugin";
  case FileKind::Patch:
    return "file_browser.action.import_patch";
  case FileKind::Sprx:
    return "file_browser.action.import_sprx";
  case FileKind::Directory:
  case FileKind::Other:
    return nullptr;
  }
  return nullptr;
}

std::string format_file_subtitle(const BrowserEntry &entry) {
  std::string size = format_byte_size(entry.size_bytes);
  const char *action_key = action_hint_key(entry.kind, entry.already_installed);
  if (!action_key)
    return size;
  return size + " · " + toolbox_i18n::tr(action_key);
}

struct ConfirmSpec {
  std::optional<std::string> message;
  std::optional<std::string> phrase;
};

ConfirmSpec confirm_spec_for(const BrowserEntry &entry) {
  const char *msg_key = nullptr;
  const char *phrase_key = nullptr;
  switch (entry.kind) {
  case FileKind::Pkg:
    msg_key = "file_browser.confirm.install_pkg_fmt";
    phrase_key = "file_browser.confirm_phrase.install";
    break;
  case FileKind::Payload:
    msg_key = entry.already_installed
                  ? "file_browser.confirm.replace_payload_fmt"
                  : "file_browser.confirm.import_payload_fmt";
    phrase_key = entry.already_installed
                     ? "file_browser.confirm_phrase.replace"
                     : "file_browser.confirm_phrase.import";
    break;
  case FileKind::Plugin:
    msg_key = entry.already_installed
                  ? "file_browser.confirm.replace_plugin_fmt"
                  : "file_browser.confirm.import_plugin_fmt";
    phrase_key = entry.already_installed
                     ? "file_browser.confirm_phrase.replace"
                     : "file_browser.confirm_phrase.import";
    break;
  case FileKind::Sprx:
    msg_key = entry.already_installed ? "file_browser.confirm.replace_sprx_fmt"
                                      : "file_browser.confirm.import_sprx_fmt";
    phrase_key = entry.already_installed
                     ? "file_browser.confirm_phrase.replace"
                     : "file_browser.confirm_phrase.import";
    break;
  case FileKind::Patch:
    msg_key = entry.already_installed
                  ? "file_browser.confirm.replace_patch_fmt"
                  : "file_browser.confirm.import_patch_fmt";
    phrase_key = entry.already_installed
                     ? "file_browser.confirm_phrase.replace"
                     : "file_browser.confirm_phrase.import";
    break;
  case FileKind::Directory:
  case FileKind::Other:
    return {};
  }
  return {toolbox_i18n::format(msg_key, entry.name.c_str()),
          std::string(toolbox_i18n::tr(phrase_key))};
}

} // namespace

bool valid_entry_name(std::string_view name) {
  if (name.empty() || name.front() == '.' || name.size() > 255)
    return false;
  return name.find('\0') == std::string_view::npos &&
         name.find('/') == std::string_view::npos &&
         name.find('\\') == std::string_view::npos;
}

FileKind classify_file(std::string_view name) {
  if (!valid_entry_name(name))
    return FileKind::Other;
  const std::size_t dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size())
    return FileKind::Other;
  const std::string_view ext = name.substr(dot + 1);
  if (ascii_iequals(ext, "pkg"))
    return FileKind::Pkg;
  if (ascii_iequals(ext, "elf"))
    return FileKind::Payload;
  if (ascii_iequals(ext, "json") || ascii_iequals(ext, "shn") ||
      ascii_iequals(ext, "mc4") || ascii_iequals(ext, "shnext") ||
      ascii_iequals(ext, "xml"))
    return FileKind::Patch;
  if (ascii_iequals(ext, "prx") || ascii_iequals(ext, "sprx"))
    return FileKind::Sprx;
  return FileKind::Other;
}

bool matches_filter(FileKind kind, FilterMode mode) {
  if (kind == FileKind::Directory)
    return true;
  switch (mode) {
  case FilterMode::All:
    return true;
  case FilterMode::Pkg:
    return kind == FileKind::Pkg;
  case FilterMode::Payload:
    return kind == FileKind::Payload || kind == FileKind::Plugin;
  case FilterMode::Patch:
    return kind == FileKind::Patch;
  }
  return false;
}

FilterMode filter_mode_from_key(std::string_view key) {
  if (key == "pkg")
    return FilterMode::Pkg;
  if (key == "payload")
    return FilterMode::Payload;
  if (key == "patch")
    return FilterMode::Patch;
  return FilterMode::All;
}

std::string format_byte_size(std::uint64_t bytes) {
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

std::string join_path(std::string_view base, std::string_view child) {
  if (base.empty())
    return std::string(child);
  if (child.empty())
    return std::string(base);
  std::string out(base);
  while (out.size() > 1 && out.back() == '/')
    out.pop_back();
  while (!child.empty() && child.front() == '/')
    child.remove_prefix(1);
  if (child.empty())
    return out;
  if (out.back() != '/')
    out.push_back('/');
  out.append(child.data(), child.size());
  return out;
}

BrowserListing list_directory(const std::string &shellui_dir, FilterMode mode,
                              const DestinationDirs &destinations) {
  BrowserListing result{};
  if (shellui_dir.empty())
    return result;
  DIR *dir = ::opendir(shellui_dir.c_str());
  if (!dir)
    return result;
  result.accessible = true;

  std::size_t examined = 0;
  std::vector<BrowserEntry> all_entries;
  while (const struct dirent *ent = ::readdir(dir)) {
    if (examined >= kMaxExaminedEntries) {
      result.truncated = true;
      break;
    }
    ++examined;
    const std::string_view name(ent->d_name);
    if (!valid_entry_name(name))
      continue;
    const std::string full_path = join_path(shellui_dir, name);
    struct stat st {};
    if (::lstat(full_path.c_str(), &st) != 0)
      continue;

    BrowserEntry item;
    item.name.assign(name.data(), name.size());
    if (S_ISDIR(st.st_mode)) {
      item.kind = FileKind::Directory;
    } else if (S_ISREG(st.st_mode)) {
      item.kind = classify_file(name);
      if (item.kind == FileKind::Payload &&
          inspect_plugin_elf(full_path, item.plugin_id)) {
        item.kind = FileKind::Plugin;
      }
      if (!matches_filter(item.kind, mode))
        continue;
      item.size_bytes =
          st.st_size > 0 ? static_cast<std::uint64_t>(st.st_size) : 0;
      const std::string dst =
          destination_path_for(item.name, item.kind, destinations);
      item.already_installed = !dst.empty() && if_exists(dst.c_str());
    } else {
      continue;
    }
    all_entries.push_back(std::move(item));
  }
  ::closedir(dir);

  std::sort(all_entries.begin(), all_entries.end(),
            [](const BrowserEntry &lhs, const BrowserEntry &rhs) {
              const bool lhs_dir = lhs.kind == FileKind::Directory;
              const bool rhs_dir = rhs.kind == FileKind::Directory;
              if (lhs_dir != rhs_dir)
                return lhs_dir;
              return icase_less(lhs.name, rhs.name);
            });

  if (all_entries.size() > kMaxBrowserEntries) {
    all_entries.resize(kMaxBrowserEntries);
    result.truncated = true;
  }
  result.entries = std::move(all_entries);
  return result;
}

ImportStatus import_file_to_destination(const std::string &source_shellui_path,
                                        const std::string &filename,
                                        FileKind kind,
                                        const DestinationDirs &destinations,
                                        bool replace,
                                        std::string *dest_system_path_out) {
  if (!valid_entry_name(filename) || source_shellui_path.empty())
    return ImportStatus::InvalidName;
  const std::string *dest_dir = destination_dir_for(kind, destinations);
  if (!dest_dir || dest_dir->empty())
    return ImportStatus::UnsupportedKind;

  const std::string dest_path =
      destination_path_for(filename, kind, destinations);
  if (dest_system_path_out)
    *dest_system_path_out = display_system_path_for(dest_path);

  if (same_inode(source_shellui_path, dest_path))
    return ImportStatus::AlreadySameFile;

  const bool existed = if_exists(dest_path.c_str());
  if (existed && !replace)
    return ImportStatus::AlreadyExists;

  if (!mkdir_tree(dest_dir->c_str()))
    return ImportStatus::WriteFailed;

  if (kind == FileKind::Payload || kind == FileKind::Plugin) {
    usb_payloads::Entry entry;
    entry.name = filename;
    entry.source_path = source_shellui_path;
    entry.usb_index = 0;
    const auto rc = usb_payloads::import_payload(entry, *dest_dir, replace);
    switch (rc) {
    case usb_payloads::ImportResult::Completed:
      return existed ? ImportStatus::Replaced : ImportStatus::Completed;
    case usb_payloads::ImportResult::InvalidName:
      return ImportStatus::InvalidName;
    case usb_payloads::ImportResult::DestinationAlreadyExists:
      return ImportStatus::AlreadyExists;
    case usb_payloads::ImportResult::CannotOpenSource:
    case usb_payloads::ImportResult::ReadError:
      return ImportStatus::ReadFailed;
    case usb_payloads::ImportResult::CannotCreateDestination:
    case usb_payloads::ImportResult::WriteError:
    case usb_payloads::ImportResult::RenameError:
      return ImportStatus::WriteFailed;
    }
    return ImportStatus::WriteFailed;
  }

  const std::size_t max_bytes =
      kind == FileKind::Patch ? kMaxPatchImportBytes : kMaxSprxImportBytes;
  struct stat st {};
  if (::stat(source_shellui_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
      st.st_size <= 0)
    return ImportStatus::ReadFailed;
  if (static_cast<std::uint64_t>(st.st_size) > max_bytes)
    return ImportStatus::TooLarge;

  std::uint8_t *buffer = nullptr;
  std::size_t size = 0;
  if (!read_file_alloc(source_shellui_path.c_str(), max_bytes, &buffer, &size))
    return ImportStatus::ReadFailed;
  const bool wrote = write_file_atomic(dest_path.c_str(), buffer, size);
  std::free(buffer);
  if (!wrote)
    return ImportStatus::WriteFailed;
  return existed ? ImportStatus::Replaced : ImportStatus::Completed;
}

void fill_file_browser_root_page(ps5ui::Page &page,
                                 const std::vector<RootLocation> &roots,
                                 const ChildLinkBuilder &link_for_root) {
  if (roots.empty()) {
    page.label("id_fb_empty", toolbox_i18n::tr("file_browser.no_roots"));
    return;
  }
  for (std::size_t i = 0; i < roots.size(); ++i) {
    const RootLocation &root = roots[i];
    const std::string target_xml =
        link_for_root ? link_for_root(i, root.system_path) : std::string();
    page.link(std::string(kRootPrefix) + std::to_string(i), root.label,
              target_xml, root.system_path);
  }
}

void fill_file_browser_dir_page(ps5ui::Page &page, std::uint64_t page_token,
                                const BrowserListing &listing,
                                const ChildLinkBuilder &link_for_dir) {
  if (!listing.accessible) {
    page.label("id_fb_inaccessible",
               toolbox_i18n::tr("file_browser.inaccessible"));
    return;
  }
  if (listing.entries.empty()) {
    page.label("id_fb_empty", toolbox_i18n::tr("file_browser.empty"));
    return;
  }

  for (std::size_t i = 0; i < listing.entries.size(); ++i) {
    const BrowserEntry &entry = listing.entries[i];
    if (entry.kind == FileKind::Directory) {
      const std::string target_xml =
          link_for_dir ? link_for_dir(i, entry.name) : std::string();
      page.link(std::string(kDirPrefix) + std::to_string(page_token) + "_" +
                    std::to_string(i),
                entry.name + "/", target_xml,
                toolbox_i18n::tr("file_browser.folder_tag"));
    } else {
      const ConfirmSpec confirm = confirm_spec_for(entry);
      page.button(
          std::string(kFilePrefix) + std::to_string(page_token) + "_" +
              std::to_string(i),
          entry.name, format_file_subtitle(entry), std::nullopt, std::nullopt,
          ps5ui::Style::None, confirm.message, confirm.phrase);
    }
  }
  if (listing.truncated) {
    page.label("id_fb_truncated", toolbox_i18n::tr("file_browser.truncated"));
  }
}

} // namespace onion::shellui::file_browser
