#include "usb_storage.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include <onion/platform.h>

#include <cerrno>
#include <utility>

namespace onion::shellui::usb_storage {
namespace {

constexpr unsigned kDeviceLimit = 8;

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

// Keep the directory string pinned until the matching unmap completes.
struct ScopedAccess {
  Device id;
  MonoString *directory;
  ~ScopedAccess() {
    void *arguments[] = {&id, directory};
    invoke(managed.unmap, arguments, "requestUsbUnmap");
  }
};

} // namespace

int list_devices(std::vector<Device> &devices) {
  devices.clear();
  if (!bind_managed_api())
    return -ENOSYS;
  MonoArray *array = mono_array_new(Root_Domain, managed.uint32_class, kDeviceLimit);
  Pinned pinned(reinterpret_cast<MonoObject *>(array));
  if (!pinned.get())
    return -ENOMEM;
  int count = 0;
  void *arguments[] = {array, &count};
  const int result = invoke(managed.devices, arguments, "getUsbDeviceList");
  if (result < 0)
    return result;
  if (count < 0 || count > static_cast<int>(kDeviceLimit))
    return -EOVERFLOW;
  for (int i = 0; i < count; ++i) {
    const char *element = mono_array_addr_with_size(array, sizeof(Device), i);
    if (!element) {
      devices.clear();
      return -EIO;
    }
    Device id;
    __builtin_memcpy(&id, element, sizeof(id));
    devices.push_back(id);
  }
  return 0;
}

void with_device(Device id, const std::function<void(const Mapping &)> &read) {
  if (!read || !bind_managed_api())
    return;
  MonoString *directory = mono_string_new(Root_Domain, "/");
  Pinned pinned(reinterpret_cast<MonoObject *>(directory));
  if (!pinned.get())
    return;
  int lock = 1;
  std::uint64_t options = 0, features = 0;
  MonoString *mapped = nullptr;
  void *map_args[] = {&id, directory, &lock, &options, &mapped, &features};
  if (invoke(managed.map, map_args, "requestUsbMap") < 0)
    return;
  ScopedAccess access{id, directory};
  Pinned mapped_root(reinterpret_cast<MonoObject *>(mapped));
  if (!mapped_root.get())
    return;
  std::string path = Mono_to_String(mapped);
  if (path.size() <= 1 || path.size() >= 128 || path.front() != '/' ||
      path.back() == '/' || path.find('\0') != std::string::npos ||
      path.find("//") != std::string::npos || path.find("/../") != std::string::npos ||
      path.find("/./") != std::string::npos || path.ends_with("/..") || path.ends_with("/."))
    return;
  MonoString *global = nullptr;
  void *point_args[] = {&id, &global};
  const int result = invoke(managed.mount_point, point_args, "getUsbMountPointOfShellCore");
  Pinned global_root(reinterpret_cast<MonoObject *>(global));
  std::string system_path = result >= 0 && global_root.get() ? Mono_to_String(global) : "";
  if (system_path.empty())
    system_path = "/mnt" + path;
  if (system_path.size() != 9 || system_path.compare(0, 8, "/mnt/usb") != 0 ||
      system_path.back() < '0' || system_path.back() > '7')
    return;
  read({static_cast<unsigned>(system_path.back() - '0'), std::move(path)});
}

} // namespace onion::shellui::usb_storage
