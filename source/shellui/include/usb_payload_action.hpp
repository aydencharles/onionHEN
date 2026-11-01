#pragma once

#include "usb_payload_importer.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::usb_payloads {

inline constexpr std::string_view kImportActionPrefix =
    "id_usb_payload_import_";
inline constexpr std::string_view kReplaceActionPrefix =
    "id_usb_payload_replace_";

// XML attribute normalization changes raw control whitespace in button IDs.
inline std::string encode_action_name(std::string_view name) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(name.size());
  for (unsigned char byte : name) {
    if (byte < 0x20 || byte == '%') {
      encoded += '%';
      encoded += hex[byte >> 4];
      encoded += hex[byte & 0x0f];
    } else {
      encoded += static_cast<char>(byte);
    }
  }
  return encoded;
}

inline std::optional<std::string> decode_action_name(std::string_view encoded) {
  const auto hex_value = [](char digit) -> unsigned {
    if (digit >= '0' && digit <= '9')
      return static_cast<unsigned>(digit - '0');
    if (digit >= 'A' && digit <= 'F')
      return static_cast<unsigned>(digit - 'A' + 10);
    return 16;
  };
  std::string name;
  name.reserve(encoded.size());
  for (size_t i = 0; i < encoded.size(); ++i) {
    const unsigned char byte = encoded[i];
    if (byte == '%') {
      if (i + 2 >= encoded.size())
        return std::nullopt;
      const unsigned high = hex_value(encoded[i + 1]);
      const unsigned low = hex_value(encoded[i + 2]);
      if (high == 16 || low == 16)
        return std::nullopt;
      const unsigned decoded = (high << 4) | low;
      if (decoded >= 0x20 && decoded != '%')
        return std::nullopt;
      name += static_cast<char>(decoded);
      i += 2;
    } else {
      if (byte < 0x20)
        return std::nullopt;
      name += static_cast<char>(byte);
    }
  }
  return name;
}

inline std::string action_id(const Entry &entry, bool replace) {
  return std::string(replace ? kReplaceActionPrefix : kImportActionPrefix) +
         std::to_string(entry.usb_index) + "_" +
         encode_action_name(entry.name);
}

inline const Entry *find_action_entry(const std::vector<Entry> &entries,
                                      std::string_view id, bool replace) {
  const std::string_view prefix =
      replace ? kReplaceActionPrefix : kImportActionPrefix;
  if (!id.starts_with(prefix))
    return nullptr;
  const std::string_view identity = id.substr(prefix.size());
  if (identity.size() < 3 || identity[0] < '0' || identity[0] > '7' ||
      identity[1] != '_')
    return nullptr;

  const unsigned usb_index = static_cast<unsigned>(identity[0] - '0');
  const auto name = decode_action_name(identity.substr(2));
  if (!name || name->empty())
    return nullptr;
  const auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const Entry &entry) {
                                    return entry.usb_index == usb_index &&
                                           entry.name == *name;
                                  });
  return found == entries.end() ? nullptr : &*found;
}

} // namespace onion::shellui::usb_payloads
