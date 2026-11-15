#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace onion::shellui::usb_storage {

using Device = std::uint32_t;
struct Mapping {
  unsigned usb_index;
  std::string path;
};

int list_devices(std::vector<Device> &devices);
void with_device(Device device, const std::function<void(const Mapping &)> &read);

} // namespace onion::shellui::usb_storage
