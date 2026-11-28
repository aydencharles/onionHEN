/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Pure, host-testable core for the lightweight ShellUI file browser.
 */
#include "file_browser.hpp"

#include "toolbox_i18n.hpp"
#include "usb_payload_importer.hpp"

#include <onion/fs.h>
#include <onion/tree.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <utility>

namespace onion::shellui::file_browser {
namespace {

constexpr std::size_t kMaxPatchImportBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxSprxImportBytes = 64ULL * 1024ULL * 1024ULL;

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
  const std::string target_name =
      kind == FileKind::Payload ? normalized_payload_name(filename) : filename;
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

const char *filter_label_key(FilterMode mode) {
  switch (mode) {
  case FilterMode::All:
    return "file_browser.filter.all";
  case FilterMode::Pkg:
    return "file_browser.filter.pkg";
  case FilterMode::Payload:
    return "file_browser.filter.payload";
  case FilterMode::Patch:
    return "file_browser.filter.patch";
  }
  return "file_browser.filter.all";
}

const char *action_hint_key(FileKind kind, bool already_installed) {
  switch (kind) {
  case FileKind::Pkg:
    return "file_browser.action.install_pkg";
  case FileKind::Payload:
    return already_installed ? "file_browser.action.replace_payload"
                             : "file_browser.action.import_payload";
  case FileKind::Patch:
    return already_installed ? "file_browser.action.replace_patch"
                             : "file_browser.action.import_patch";
  case FileKind::Sprx:
    return already_installed ? "file_browser.action.replace_sprx"
                             : "file_browser.action.import_sprx";
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

std::optional<std::string> confirm_prompt_for(const BrowserEntry &entry) {
  if (!entry.already_installed)
    return std::nullopt;
  switch (entry.kind) {
  case FileKind::Payload:
  case FileKind::Patch:
  case FileKind::Sprx:
    return toolbox_i18n::format("file_browser.replace_confirm_fmt",
                                entry.name.c_str());
  case FileKind::Directory:
  case FileKind::Pkg:
  case FileKind::Other:
    return std::nullopt;
  }
  return std::nullopt;
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
    return kind == FileKind::Payload;
  case FilterMode::Patch:
    return kind == FileKind::Patch;
  }
  return false;
}

FilterMode next_filter_mode(FilterMode mode) {
  switch (mode) {
  case FilterMode::All:
    return FilterMode::Pkg;
  case FilterMode::Pkg:
    return FilterMode::Payload;
  case FilterMode::Payload:
    return FilterMode::Patch;
  case FilterMode::Patch:
    return FilterMode::All;
  }
  return FilterMode::All;
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

std::string BrowserState::current_system_path() const {
  if (!active_root)
    return {};
  return join_path(active_root->system_path, rel_path);
}

std::string BrowserState::current_shellui_path(
    std::string_view mounted_root_override) const {
  if (!active_root)
    return {};
  const std::string_view base = mounted_root_override.empty()
                                    ? std::string_view(active_root->shellui_path)
                                    : mounted_root_override;
  return join_path(base, rel_path);
}

std::string BrowserState::parent_system_path() const {
  if (!active_root || rel_path.empty())
    return {};
  const std::size_t slash = rel_path.rfind('/');
  if (slash == std::string::npos)
    return active_root->system_path;
  return join_path(active_root->system_path,
                   std::string_view(rel_path).substr(0, slash));
}

void BrowserState::reset(FilterMode initial_mode, bool lock_filter) {
  mode = initial_mode;
  filter_locked = lock_filter;
  active_root.reset();
  rel_path.clear();
}

bool BrowserState::enter_root(const RootLocation &root) {
  if (root.system_path.empty())
    return false;
  active_root = root;
  rel_path.clear();
  return true;
}

bool BrowserState::enter_subdir(std::string_view name) {
  if (!active_root || !valid_entry_name(name))
    return false;
  if (rel_path.empty())
    rel_path.assign(name.data(), name.size());
  else {
    rel_path.push_back('/');
    rel_path.append(name.data(), name.size());
  }
  return true;
}

bool BrowserState::navigate_up() {
  if (!active_root)
    return false;
  if (rel_path.empty()) {
    active_root.reset();
    return true;
  }
  const std::size_t slash = rel_path.rfind('/');
  if (slash == std::string::npos)
    rel_path.clear();
  else
    rel_path.erase(slash);
  return true;
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
    if (::stat(full_path.c_str(), &st) != 0)
      continue;

    BrowserEntry item;
    item.name.assign(name.data(), name.size());
    if (S_ISDIR(st.st_mode)) {
      item.kind = FileKind::Directory;
    } else if (S_ISREG(st.st_mode)) {
      item.kind = classify_file(name);
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

  if (kind == FileKind::Payload) {
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

void fill_file_browser_page(ps5ui::Page &page, const BrowserState &state,
                            const std::vector<RootLocation> &roots,
                            const BrowserListing &listing) {
  if (!state.filter_locked) {
    page.button(std::string(kFilterButtonId),
                toolbox_i18n::format("file_browser.filter_fmt",
                                     toolbox_i18n::tr(filter_label_key(state.mode))),
                toolbox_i18n::tr("file_browser.filter_sub"));
  }

  if (!state.in_directory()) {
    if (roots.empty()) {
      page.label("id_fb_empty", toolbox_i18n::tr("file_browser.no_roots"));
    } else {
      for (std::size_t i = 0; i < roots.size(); ++i) {
        const RootLocation &root = roots[i];
        page.button(std::string(kRootPrefix) + std::to_string(i), root.label,
                    root.system_path,
                    toolbox_i18n::tr("file_browser.open_folder_help"));
      }
    }
    page.button(std::string(kRefreshButtonId),
                toolbox_i18n::tr("file_browser.refresh"),
                toolbox_i18n::tr("file_browser.refresh_sub"));
    return;
  }

  const std::string parent = state.parent_system_path();
  page.button(std::string(kUpButtonId), toolbox_i18n::tr("file_browser.up"),
              parent.empty() ? std::string(toolbox_i18n::tr("file_browser.roots_title"))
                             : parent,
              toolbox_i18n::tr("file_browser.up_help"));

  if (!listing.accessible) {
    page.label("id_fb_inaccessible",
               toolbox_i18n::tr("file_browser.inaccessible"));
  } else if (listing.entries.empty()) {
    page.label("id_fb_empty", toolbox_i18n::tr("file_browser.empty"));
  } else {
    for (std::size_t i = 0; i < listing.entries.size(); ++i) {
      const BrowserEntry &entry = listing.entries[i];
      if (entry.kind == FileKind::Directory) {
        page.button(std::string(kDirPrefix) + std::to_string(i),
                    entry.name + "/",
                    toolbox_i18n::tr("file_browser.folder_tag"),
                    toolbox_i18n::tr("file_browser.open_folder_help"));
      } else {
        const auto confirm = confirm_prompt_for(entry);
        page.button(
            std::string(kFilePrefix) + std::to_string(i), entry.name,
            format_file_subtitle(entry),
            toolbox_i18n::tr("file_browser.file_help"), std::nullopt,
            ps5ui::Style::None, confirm,
            confirm ? std::optional<std::string>(
                          toolbox_i18n::tr("file_browser.confirm_phrase"))
                    : std::nullopt);
      }
    }
    if (listing.truncated) {
      page.label("id_fb_truncated", toolbox_i18n::tr("file_browser.truncated"));
    }
  }

  page.button(std::string(kRefreshButtonId),
              toolbox_i18n::tr("file_browser.refresh"),
              toolbox_i18n::tr("file_browser.refresh_sub"));
}

} // namespace onion::shellui::file_browser
