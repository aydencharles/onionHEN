/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Pure, host-testable core for the lightweight ShellUI file browser.
 * Reuses pkg_installer path helpers, usb_payload_importer, and libonion_platform
 * atomic file helpers rather than duplicating parsing or I/O logic.
 */
#pragma once

#include "ps5_settings_ui.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::file_browser {

inline constexpr std::string_view kFilterButtonId = "id_fb_filter";
inline constexpr std::string_view kUpButtonId = "id_fb_up";
inline constexpr std::string_view kRefreshButtonId = "id_fb_refresh";
inline constexpr std::string_view kRootPrefix = "id_fb_root_";
inline constexpr std::string_view kDirPrefix = "id_fb_dir_";
inline constexpr std::string_view kFilePrefix = "id_fb_file_";

inline constexpr std::size_t kMaxBrowserEntries = 128;
inline constexpr std::size_t kMaxExaminedEntries = 4096;

enum class FilterMode : unsigned char {
  All = 0,
  Pkg,
  Payload,
  Patch,
};

enum class FileKind : unsigned char {
  Directory = 0,
  Pkg,
  Payload,
  Patch,
  Sprx,
  Other,
};

struct RootLocation {
  std::string label;
  std::string system_path;
  std::string shellui_path;
  bool is_usb = false;
  unsigned usb_index = 0;
};

struct DestinationDirs {
  std::string payloads = "/user/data/OnionHEN/payloads";
  std::string cheats = "/user/data/OnionHEN/cheats";
  std::string sprx = "/user/data/OnionHEN/sprx";
};

struct BrowserEntry {
  std::string name;
  FileKind kind = FileKind::Other;
  std::uint64_t size_bytes = 0;
  bool already_installed = false;
};

struct BrowserListing {
  bool accessible = false;
  bool truncated = false;
  std::vector<BrowserEntry> entries;
};

enum class ImportStatus : unsigned char {
  Completed = 0,
  Replaced,
  AlreadySameFile,
  AlreadyExists,
  UnsupportedKind,
  InvalidName,
  TooLarge,
  ReadFailed,
  WriteFailed,
};

struct BrowserState {
  FilterMode mode = FilterMode::All;
  bool filter_locked = false;
  std::optional<RootLocation> active_root;
  std::string rel_path;

  bool in_directory() const { return active_root.has_value(); }
  std::string current_system_path() const;
  std::string current_shellui_path(
      std::string_view mounted_root_override = {}) const;
  std::string parent_system_path() const;

  void reset(FilterMode initial_mode, bool lock_filter);
  bool enter_root(const RootLocation &root);
  bool enter_subdir(std::string_view name);
  bool navigate_up();
};

FileKind classify_file(std::string_view name);
bool matches_filter(FileKind kind, FilterMode mode);
FilterMode next_filter_mode(FilterMode mode);
FilterMode filter_mode_from_key(std::string_view key);
bool valid_entry_name(std::string_view name);

std::string format_byte_size(std::uint64_t bytes);
std::string join_path(std::string_view base, std::string_view child);

BrowserListing list_directory(const std::string &shellui_dir, FilterMode mode,
                              const DestinationDirs &destinations);

ImportStatus import_file_to_destination(const std::string &source_shellui_path,
                                        const std::string &filename,
                                        FileKind kind,
                                        const DestinationDirs &destinations,
                                        bool replace,
                                        std::string *dest_system_path_out = nullptr);

void fill_file_browser_page(ps5ui::Page &page, const BrowserState &state,
                            const std::vector<RootLocation> &roots,
                            const BrowserListing &listing);

} // namespace onion::shellui::file_browser
