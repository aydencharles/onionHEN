#pragma once

#include "usb_payload_importer.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::usb_payloads {

inline constexpr std::string_view kImportActionPrefix =
    "id_usb_payload_import_";
inline constexpr std::string_view kReplaceActionPrefix =
    "id_usb_payload_replace_";

inline std::string action_id(const Entry &entry, bool replace) {
  return std::string(replace ? kReplaceActionPrefix : kImportActionPrefix) +
         std::to_string(entry.usb_index) + "_" + entry.name;
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
  const std::string_view name = identity.substr(2);
  const auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const Entry &entry) {
                                    return entry.usb_index == usb_index &&
                                           std::string_view(entry.name) == name;
                                  });
  return found == entries.end() ? nullptr : &*found;
}

} // namespace onion::shellui::usb_payloads
