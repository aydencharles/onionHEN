#include "test_harness.h"
#include "usb_storage.hpp"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <vector>

using namespace onion::shellui::usb_storage;

namespace {

struct FakeStorage {
  std::vector<std::uint32_t> ids{0x12345678u};
  std::vector<std::string> events;
  std::string mapped_path = "/usb1";
  std::string global_path = "/mnt/usb1";
  std::string map_directory;
  std::string unmap_directory;
  std::uint32_t mapped_id = 0;
  std::uint32_t unmapped_id = 0;
  std::uint64_t options = ~std::uint64_t{0};
  int lock = -1;
  int list_result = 0;
  int map_result = 0;
  int point_result = 0;
  int unmap_result = 0;
  int count_override = -100;
  bool initial_count_zero = true;
  bool active = false;

  Api api() {
    return {
      [this](std::uint32_t *output, int *count) {
        events.push_back("list");
        initial_count_zero = initial_count_zero && *count == 0;
        std::copy_n(ids.begin(), std::min<std::size_t>(ids.size(), 8), output);
        *count = count_override == -100 ? static_cast<int>(ids.size()) : count_override;
        return list_result;
      },
      [this](std::uint32_t id, const char *directory, int requested_lock,
             std::uint64_t requested_options, std::string &path,
             std::uint64_t &features) {
        events.push_back("map");
        mapped_id = id;
        map_directory = directory;
        lock = requested_lock;
        options = requested_options;
        if (map_result >= 0) {
          path = mapped_path;
          features = 0x800;
          active = true;
        }
        return map_result;
      },
      [this](std::uint32_t, std::string &path) {
        events.push_back("point");
        path = global_path;
        return point_result;
      },
      [this](std::uint32_t id, const char *directory) {
        events.push_back("unmap");
        unmapped_id = id;
        unmap_directory = directory;
        if (unmap_result >= 0)
          active = false;
        return unmap_result;
      }
    };
  }
};

int test_native_read_lifecycle() {
  FakeStorage fake;
  Reader reader(fake.api());
  bool readable = false;
  Mapping observed {};
  const int result = reader.with_device({0x12345678u}, [&](const Mapping &mapping) {
    fake.events.push_back("read");
    readable = fake.active;
    observed = mapping;
  });
  TEST_ASSERT_EQ_INT(0, result);
  TEST_ASSERT_TRUE(readable && !fake.active);
  TEST_ASSERT_TRUE(fake.events == std::vector<std::string>({"map", "point", "read", "unmap"}));
  TEST_ASSERT_EQ_U64(0x12345678u, fake.mapped_id);
  TEST_ASSERT_EQ_U64(fake.mapped_id, fake.unmapped_id);
  TEST_ASSERT_STREQ("/", fake.map_directory.c_str());
  TEST_ASSERT_STREQ("/", fake.unmap_directory.c_str());
  TEST_ASSERT_EQ_INT(1, fake.lock);
  TEST_ASSERT_EQ_U64(0, fake.options);
  TEST_ASSERT_EQ_INT(1, observed.usb_index);
  TEST_ASSERT_STREQ("/usb1", observed.path.c_str());
  TEST_ASSERT_STREQ("/mnt/usb1", observed.system_mount.c_str());
  TEST_ASSERT_EQ_U64(fake.mapped_id, observed.device.id);
  return 0;
}

int test_devices_refreshed_and_positive_results() {
  FakeStorage fake;
  fake.list_result = 2;
  Reader reader(fake.api());
  std::vector<Device> devices;
  TEST_ASSERT_EQ_INT(0, reader.list_devices(devices));
  TEST_ASSERT_EQ_INT(1, devices.size());
  TEST_ASSERT_EQ_U64(0x12345678u, devices[0].id);
  fake.ids = {77, 99};
  TEST_ASSERT_EQ_INT(0, reader.list_devices(devices));
  TEST_ASSERT_EQ_INT(2, devices.size());
  TEST_ASSERT_EQ_U64(77, devices[0].id);
  TEST_ASSERT_EQ_U64(99, devices[1].id);
  TEST_ASSERT_TRUE(fake.initial_count_zero);
  TEST_ASSERT_TRUE(fake.events == std::vector<std::string>({"list", "list"}));
  fake.map_result = 3;
  fake.point_result = 4;
  fake.unmap_result = 5;
  bool read = false;
  TEST_ASSERT_EQ_INT(0, reader.with_device({77}, [&](const Mapping &) { read = true; }));
  TEST_ASSERT_TRUE(read && !fake.active);
  return 0;
}

int test_list_errors_clear_old_devices() {
  FakeStorage fake;
  Reader reader(fake.api());
  std::vector<Device> devices{{10}};
  fake.list_result = -ENODEV;
  TEST_ASSERT_EQ_INT(-ENODEV, reader.list_devices(devices));
  TEST_ASSERT_TRUE(devices.empty());
  fake.list_result = 0;
  fake.count_override = 9;
  TEST_ASSERT_EQ_INT(-EOVERFLOW, reader.list_devices(devices));
  TEST_ASSERT_TRUE(devices.empty());
  fake.count_override = -1;
  TEST_ASSERT_EQ_INT(-EOVERFLOW, reader.list_devices(devices));
  TEST_ASSERT_TRUE(devices.empty());
  return 0;
}

int test_failed_map_does_not_release_others() {
  FakeStorage fake;
  fake.map_result = -EBUSY;
  Reader reader(fake.api());
  bool read = false;
  TEST_ASSERT_EQ_INT(-EBUSY, reader.with_device({12}, [&](const Mapping &) { read = true; }));
  TEST_ASSERT_TRUE(!read);
  TEST_ASSERT_TRUE(fake.events == std::vector<std::string>({"map"}));
  fake.map_result = 0;
  TEST_ASSERT_EQ_INT(0, reader.with_device({12}, [&](const Mapping &) { read = true; }));
  TEST_ASSERT_TRUE(read && !fake.active);
  TEST_ASSERT_TRUE(fake.events == std::vector<std::string>({"map", "map", "point", "unmap"}));
  return 0;
}

int test_fresh_scope_after_installer() {
  FakeStorage fake;
  Reader reader(fake.api());
  int reads = 0;
  auto read = [&](const Mapping &) { if (fake.active) ++reads; };
  TEST_ASSERT_EQ_INT(0, reader.with_device({55}, read));
  TEST_ASSERT_TRUE(!fake.active);
  // A later menu visit cannot reuse access released by the previous reader.
  fake.mapped_path = "/usb7";
  fake.global_path = "/mnt/usb7";
  TEST_ASSERT_EQ_INT(0, reader.with_device({55}, read));
  TEST_ASSERT_EQ_INT(2, reads);
  TEST_ASSERT_TRUE(!fake.active);
  TEST_ASSERT_TRUE(fake.events == std::vector<std::string>({
      "map", "point", "unmap", "map", "point", "unmap"}));
  return 0;
}

int test_native_global_path_fallback() {
  FakeStorage fake;
  Reader reader(fake.api());
  fake.global_path.clear();
  Mapping observed {};
  TEST_ASSERT_EQ_INT(0, reader.with_device({42}, [&](const Mapping &m) { observed = m; }));
  TEST_ASSERT_STREQ("/mnt/usb1", observed.system_mount.c_str());
  fake.point_result = -ENOENT;
  TEST_ASSERT_EQ_INT(0, reader.with_device({42}, [&](const Mapping &m) { observed = m; }));
  TEST_ASSERT_EQ_INT(1, observed.usb_index);
  TEST_ASSERT_TRUE(!fake.active);
  return 0;
}

int test_bad_paths_release_owned_mapping() {
  const std::vector<std::string> bad_globals = {
      "/mnt/usb8", "/mnt/usb10", "/mnt/usb1/other", "/mnt/usb1/", "/mnt/usb-1", "usb1"};
  for (const auto &path : bad_globals) {
    FakeStorage fake;
    fake.global_path = path;
    Reader reader(fake.api());
    bool read = false;
    TEST_ASSERT_EQ_INT(-EINVAL, reader.with_device({42}, [&](const Mapping &) { read = true; }));
    TEST_ASSERT_TRUE(!read && !fake.active);
    TEST_ASSERT_EQ_INT(1, std::count(fake.events.begin(), fake.events.end(), "unmap"));
  }
  const std::vector<std::string> bad_mappings = {"", "/", "usb1", "/usb1/../user", "/usb1/", "/usb1//x"};
  for (const auto &path : bad_mappings) {
    FakeStorage fake;
    fake.mapped_path = path;
    Reader reader(fake.api());
    bool read = false;
    TEST_ASSERT_EQ_INT(-EINVAL, reader.with_device({42}, [&](const Mapping &) { read = true; }));
    TEST_ASSERT_TRUE(!read && !fake.active);
    TEST_ASSERT_TRUE(fake.events == std::vector<std::string>({"map", "unmap"}));
  }
  return 0;
}

int test_release_failure_reported_once() {
  FakeStorage fake;
  fake.unmap_result = -EIO;
  Reader reader(fake.api());
  bool read = false;
  TEST_ASSERT_EQ_INT(-EIO, reader.with_device({42}, [&](const Mapping &) { read = true; }));
  TEST_ASSERT_TRUE(read);
  TEST_ASSERT_EQ_INT(1, std::count(fake.events.begin(), fake.events.end(), "unmap"));
  return 0;
}

int test_callback_exception_releases_mapping() {
  FakeStorage fake;
  Reader reader(fake.api());
  bool caught = false;
  try {
    reader.with_device({42}, [](const Mapping &) { throw std::runtime_error("read failed"); });
  } catch (const std::runtime_error &) {
    caught = true;
  }
  TEST_ASSERT_TRUE(caught && !fake.active);
  TEST_ASSERT_EQ_INT(1, std::count(fake.events.begin(), fake.events.end(), "unmap"));
  return 0;
}

} // namespace

extern "C" int test_usb_storage_suite(void) {
  int failures = 0;
  failures += onion_test_run("usb_storage.native_read_lifecycle", test_native_read_lifecycle);
  failures += onion_test_run("usb_storage.devices_refreshed_positive_results", test_devices_refreshed_and_positive_results);
  failures += onion_test_run("usb_storage.list_errors_clear_old_devices", test_list_errors_clear_old_devices);
  failures += onion_test_run("usb_storage.failed_map_preserves_others", test_failed_map_does_not_release_others);
  failures += onion_test_run("usb_storage.fresh_scope_after_installer", test_fresh_scope_after_installer);
  failures += onion_test_run("usb_storage.native_global_path_fallback", test_native_global_path_fallback);
  failures += onion_test_run("usb_storage.bad_paths_release_owned_mapping", test_bad_paths_release_owned_mapping);
  failures += onion_test_run("usb_storage.release_failure_reported_once", test_release_failure_reported_once);
  failures += onion_test_run("usb_storage.callback_exception_releases_mapping", test_callback_exception_releases_mapping);
  return failures;
}
