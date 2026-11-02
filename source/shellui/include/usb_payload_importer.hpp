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

// Access paths are ShellUI mounts. The UI displays /mnt/usbX and /data/… .
inline constexpr const char *kUsbPrefix = "/usb";
inline constexpr const char *kDestination = "/user/data/OnionHEN/payloads";

std::vector<Entry> scan(const std::string &usb_prefix,
                        const std::string &destination, bool *device_found);
ImportResult import_payload(const Entry &entry, const std::string &destination,
                            bool replace);

} // namespace onion::shellui::usb_payloads
