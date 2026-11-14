#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace onion::shellui::usb_storage {

struct Device {
  std::uint32_t id;
};

struct Mapping {
  Device device;
  unsigned usb_index;
  std::string system_mount;
  std::string path;
};

// The same operations used by the native Package Installer search job.
struct Api {
  std::function<int(std::uint32_t *, int *)> devices;
  std::function<int(std::uint32_t, const char *, int, std::uint64_t,
                    std::string &, std::uint64_t &)> map;
  std::function<int(std::uint32_t, std::string &)> mount_point;
  std::function<int(std::uint32_t, const char *)> unmap;
};

class Reader {
public:
  explicit Reader(Api api);
  int list_devices(std::vector<Device> &devices) const;
  int with_device(const Device &device,
                  const std::function<void(const Mapping &)> &read) const;

private:
  Api api_;
};

// Every operation obtains a fresh device list or a scoped native mapping.
int list_devices(std::vector<Device> &devices);
int with_device(const Device &device,
                const std::function<void(const Mapping &)> &read);

} // namespace onion::shellui::usb_storage
