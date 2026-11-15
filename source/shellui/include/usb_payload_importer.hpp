#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace onion::shellui::usb_payloads {

struct Entry {
  std::string name;
  std::string source_path;
  std::uint64_t size = 0;
  unsigned usb_index = 0;
  bool installed = false;
};

enum class ImportResult {
  Completed,
  InvalidName,
  CannotOpenSource,
  CannotCreateDestination,
  DestinationAlreadyExists,
  ReadError,
  WriteError,
  RenameError,
};

// ShellUI path for installed payloads.
inline constexpr const char *kDestination = "/user/data/OnionHEN/payloads";

// Scan only the directory returned by the native USB access request.
std::vector<Entry> scan_directory(const std::string &directory,
                                  const std::string &destination,
                                  unsigned usb_index, bool *opened = nullptr);
ImportResult import_payload(const Entry &entry, const std::string &destination,
                            bool replace);

} // namespace onion::shellui::usb_payloads
