#include "usb_storage.hpp"

#include <array>
#include <cerrno>
#include <utility>

#ifndef ONION_HOST_TEST
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include <onion/platform.h>
#endif

namespace onion::shellui::usb_storage {
namespace {

constexpr const char *kDirectory = "/";
constexpr int kReadLock = 1;
constexpr std::uint64_t kOptions = 0;
constexpr std::size_t kDeviceLimit = 8;

bool usb_index(const std::string &path, unsigned &index) {
  constexpr std::size_t prefix_length = 8;
  if (path.size() != prefix_length + 1 || path.compare(0, prefix_length, "/mnt/usb") != 0 ||
      path.back() < '0' || path.back() > '7')
    return false;
  index = static_cast<unsigned>(path.back() - '0');
  return true;
}

bool valid_mapped_path(const std::string &path) {
  return path.size() > 1 && path.size() < 128 && path.front() == '/' &&
         path.back() != '/' && path.find('\0') == std::string::npos &&
         path.find("//") == std::string::npos &&
         path.find("/../") == std::string::npos &&
         path.find("/./") == std::string::npos &&
         !path.ends_with("/..") && !path.ends_with("/.");
}

class ScopedUnmap {
public:
  ScopedUnmap(const Api &api, std::uint32_t id) : api_(api), id_(id) {}
  ~ScopedUnmap() { if (active_) (void)api_.unmap(id_, kDirectory); }
  int finish(int result) {
    active_ = false;
    const int released = api_.unmap(id_, kDirectory);
    return result < 0 ? result : (released < 0 ? released : 0);
  }
private:
  const Api &api_;
  std::uint32_t id_;
  bool active_ = true;
};

} // namespace

Reader::Reader(Api api) : api_(std::move(api)) {}

int Reader::list_devices(std::vector<Device> &devices) const {
  devices.clear();
  if (!api_.devices)
    return -ENOSYS;
  std::array<std::uint32_t, kDeviceLimit> ids {};
  int count = 0;
  const int result = api_.devices(ids.data(), &count);
  if (result < 0)
    return result;
  if (count < 0 || count > static_cast<int>(ids.size()))
    return -EOVERFLOW;
  for (int i = 0; i < count; ++i)
    devices.push_back({ids[static_cast<std::size_t>(i)]});
  return 0;
}

int Reader::with_device(const Device &device,
                        const std::function<void(const Mapping &)> &read) const {
  if (!api_.map || !api_.mount_point || !api_.unmap || !read)
    return -EINVAL;
  std::string path;
  std::uint64_t features = 0;
  const int acquired = api_.map(device.id, kDirectory, kReadLock, kOptions,
                                path, features);
  if (acquired < 0)
    return acquired;
  ScopedUnmap scope(api_, device.id);
  if (!valid_mapped_path(path))
    return scope.finish(-EINVAL);
  std::string system_mount;
  const int queried = api_.mount_point(device.id, system_mount);
  if (queried < 0 || system_mount.empty())
    system_mount = "/mnt" + path;
  unsigned index = 0;
  if (!usb_index(system_mount, index))
    return scope.finish(-EINVAL);
  read({device, index, std::move(system_mount), std::move(path)});
  return scope.finish(0);
}

#ifndef ONION_HOST_TEST
namespace {

class Pinned {
public:
  explicit Pinned(MonoObject *object) : object_(object) {
    if (object && mono_gchandle_new && mono_gchandle_free)
      handle_ = mono_gchandle_new(object, 1);
  }
  ~Pinned() { if (handle_) mono_gchandle_free(handle_); }
  Pinned(const Pinned &) = delete;
  Pinned &operator=(const Pinned &) = delete;
  MonoObject *get() const { return handle_ ? object_ : nullptr; }
private:
  MonoObject *object_;
  std::uint32_t handle_ = 0;
};

struct ManagedApi {
  MonoClass *uint32_class = nullptr;
  MonoMethod *devices = nullptr;
  MonoMethod *map = nullptr;
  MonoMethod *mount_point = nullptr;
  MonoMethod *unmap = nullptr;
};

ManagedApi managed;

bool bind_managed_api() {
  if (managed.devices && managed.map && managed.mount_point && managed.unmap)
    return true;
  if (!Root_Domain || !mono_domain_assembly_open || !mono_assembly_get_image ||
      !mono_class_from_name || !mono_class_get_method_from_name ||
      !mono_runtime_invoke || !mono_object_unbox || !mono_array_new ||
      !mono_array_addr_with_size || !mono_string_new || !mono_gchandle_new ||
      !mono_gchandle_free)
    return false;
  MonoImage *image = getDLLimage("Sce.Vsh.AutoMounterWrapper.dll");
  MonoImage *corlib = getDLLimage("mscorlib.dll");
  if (!image || !corlib)
    return false;
  MonoClass *type = mono_class_from_name(image, "Sce.Vsh", "AutoMounterWrapper");
  if (!type)
    return false;
  ManagedApi candidate;
  candidate.uint32_class = mono_class_from_name(corlib, "System", "UInt32");
  // The public name also has an unrelated two-argument object overload.
  if (mono_method_desc_new && mono_method_desc_search_in_class && mono_method_desc_free) {
    MonoMethodDesc *description = mono_method_desc_new(
        "Sce.Vsh.AutoMounterWrapper:getUsbDeviceList(uint[],int&)", 1);
    if (description) {
      candidate.devices = mono_method_desc_search_in_class(description, type);
      mono_method_desc_free(description);
    }
  }
  if (!candidate.devices)
    candidate.devices = mono_class_get_method_from_name(type, "getUsbDeviceListNative", 2);
  candidate.map = mono_class_get_method_from_name(type, "requestUsbMap", 6);
  candidate.mount_point = mono_class_get_method_from_name(type, "getUsbMountPointOfShellCore", 2);
  candidate.unmap = mono_class_get_method_from_name(type, "requestUsbUnmap", 2);
  if (!candidate.uint32_class || !candidate.devices || !candidate.map ||
      !candidate.mount_point || !candidate.unmap)
    return false;
  managed = candidate;
  LOG_INFO("USB storage: native Package Installer Mono API ready");
  return true;
}

int invoke(MonoMethod *method, void **arguments, const char *operation) {
  MonoObject *exception = nullptr;
  MonoObject *boxed = mono_runtime_invoke(method, nullptr, arguments, &exception);
  if (exception || !boxed) {
    LOG_WARN("USB storage: %s managed invocation failed", operation);
    return -EIO;
  }
  const auto *value = static_cast<const std::int32_t *>(mono_object_unbox(boxed));
  if (!value)
    return -EIO;
  const int result = *value;
  if (result < 0)
    LOG_WARN("USB storage: %s failed (0x%x)", operation, static_cast<unsigned>(result));
  return result;
}

int device_list(std::uint32_t *ids, int *count) {
  MonoArray *array = mono_array_new(Root_Domain, managed.uint32_class, kDeviceLimit);
  Pinned pinned(reinterpret_cast<MonoObject *>(array));
  if (!pinned.get())
    return -ENOMEM;
  int actual = 0;
  void *arguments[] = {array, &actual};
  const int result = invoke(managed.devices, arguments, "getUsbDeviceList");
  if (result < 0)
    return result;
  if (actual < 0 || actual > static_cast<int>(kDeviceLimit))
    return -EOVERFLOW;
  for (int i = 0; i < actual; ++i) {
    const char *element = mono_array_addr_with_size(array, sizeof(std::uint32_t), i);
    if (!element)
      return -EIO;
    std::uint32_t id;
    __builtin_memcpy(&id, element, sizeof(id));
    ids[i] = id;
  }
  *count = actual;
  return result;
}

int map_device(std::uint32_t id, const char *directory, int lock,
                std::uint64_t options, std::string &path, std::uint64_t &features) {
  MonoString *text = mono_string_new(Root_Domain, directory);
  Pinned pinned(reinterpret_cast<MonoObject *>(text));
  if (!pinned.get())
    return -ENOMEM;
  MonoString *mapped = nullptr;
  void *arguments[] = {&id, text, &lock, &options, &mapped, &features};
  const int result = invoke(managed.map, arguments, "requestUsbMap");
  if (result >= 0 && mapped) {
    Pinned output(reinterpret_cast<MonoObject *>(mapped));
    if (output.get())
      path = Mono_to_String(mapped);
  }
  // Preserve successful acquisition even if the path could not be copied:
  // Reader must release the mapping it now owns.
  return result;
}

int mount_point(std::uint32_t id, std::string &path) {
  MonoString *value = nullptr;
  void *arguments[] = {&id, &value};
  const int result = invoke(managed.mount_point, arguments, "getUsbMountPointOfShellCore");
  if (result >= 0 && value) {
    Pinned output(reinterpret_cast<MonoObject *>(value));
    if (output.get())
      path = Mono_to_String(value);
  }
  return result;
}

int unmap_device(std::uint32_t id, const char *directory) {
  MonoString *text = mono_string_new(Root_Domain, directory);
  Pinned pinned(reinterpret_cast<MonoObject *>(text));
  if (!pinned.get())
    return -ENOMEM;
  void *arguments[] = {&id, text};
  return invoke(managed.unmap, arguments, "requestUsbUnmap");
}

Api production_api() {
  return {device_list, map_device, mount_point, unmap_device};
}

} // namespace

int list_devices(std::vector<Device> &devices) {
  devices.clear();
  if (!bind_managed_api()) {
    LOG_WARN("USB storage: native Package Installer Mono API unavailable");
    return -ENOSYS;
  }
  return Reader(production_api()).list_devices(devices);
}

int with_device(const Device &device,
                const std::function<void(const Mapping &)> &read) {
  if (!bind_managed_api())
    return -ENOSYS;
  return Reader(production_api()).with_device(device, read);
}
#endif

} // namespace onion::shellui::usb_storage
