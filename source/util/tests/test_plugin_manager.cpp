#include "test_harness.h"

#include <onion/plugin_manager.hpp>
#include <onion/fs.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

void put_u16(std::vector<uint8_t> &image, size_t offset, uint16_t value) {
  image[offset] = static_cast<uint8_t>(value);
  image[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(std::vector<uint8_t> &image, size_t offset, uint32_t value) {
  for (unsigned index = 0; index < 4; ++index)
    image[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void put_u64(std::vector<uint8_t> &image, size_t offset, uint64_t value) {
  for (unsigned index = 0; index < 8; ++index)
    image[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void put_text(std::vector<uint8_t> &image, size_t offset, size_t capacity,
              std::string_view value) {
  std::fill_n(image.begin() + static_cast<ptrdiff_t>(offset), capacity, 0);
  std::copy(value.begin(), value.end(),
            image.begin() + static_cast<ptrdiff_t>(offset));
}

std::vector<uint8_t> make_plugin(std::string_view plugin_id, uint32_t flags,
                                 uint8_t content_marker) {
  constexpr size_t kSectionTable = 64;
  constexpr size_t kStringsOffset = 256;
  constexpr size_t kDescriptorOffset = 281;
  constexpr size_t kDescriptorSize = 128;
  const std::string section_names("\0.shstrtab\0.onion_plugin\0", 25);
  std::vector<uint8_t> image(kDescriptorOffset + kDescriptorSize + 1, 0);
  image[0] = 0x7f;
  image[1] = 'E';
  image[2] = 'L';
  image[3] = 'F';
  image[4] = 2;
  image[5] = 1;
  image[6] = 1;
  put_u16(image, 16, 3);
  put_u16(image, 18, 62);
  put_u64(image, 40, kSectionTable);
  put_u16(image, 54, 56);
  put_u16(image, 58, 64);
  put_u16(image, 60, 3);
  put_u16(image, 62, 1);

  const size_t strings_header = kSectionTable + 64;
  put_u32(image, strings_header, 1);
  put_u64(image, strings_header + 24, kStringsOffset);
  put_u64(image, strings_header + 32, section_names.size());
  const size_t descriptor_header = kSectionTable + 128;
  put_u32(image, descriptor_header, 11);
  put_u64(image, descriptor_header + 24, kDescriptorOffset);
  put_u64(image, descriptor_header + 32, kDescriptorSize);
  std::copy(section_names.begin(), section_names.end(),
            image.begin() + static_cast<ptrdiff_t>(kStringsOffset));

  put_u32(image, kDescriptorOffset, kDescriptorSize);
  put_u32(image, kDescriptorOffset + 4, onion::plugin::kAbiVersion);
  put_u32(image, kDescriptorOffset + 12, flags);
  put_text(image, kDescriptorOffset + 16, onion::plugin::kIdCapacity,
           plugin_id);
  put_text(image, kDescriptorOffset + 48, onion::plugin::kVersionCapacity,
           "1.00");
  put_text(image, kDescriptorOffset + 64, onion::plugin::kNameCapacity,
           plugin_id == "AUTO00001" ? "Auto Plugin" : "Manual Plugin");
  image.back() = content_marker;
  return image;
}

bool write_plugin(const std::string &path, const std::vector<uint8_t> &image) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(image.data()),
               static_cast<std::streamsize>(image.size()));
  return output.good();
}

class FakeRuntime final : public onion::plugin::ProcessRuntime {
public:
  pid_t recover(std::string_view) override { return -1; }

  pid_t launch(const onion::plugin::PluginFile &plugin) override {
    const pid_t pid = next_pid++;
    alive_pids[pid] = true;
    last_pid = pid;
    launched.push_back(plugin.fingerprint);
    return pid;
  }

  bool alive(pid_t pid) override { return alive_pids[pid]; }
  void persist(std::string_view, pid_t) override {}

  void stop(pid_t pid, uint32_t flags) override {
    alive_pids[pid] = false;
    last_stop_flags = flags;
    ++stop_count;
  }

  pid_t next_pid = 100;
  pid_t last_pid = -1;
  int stop_count = 0;
  uint32_t last_stop_flags = 0;
  std::map<pid_t, bool> alive_pids;
  std::vector<uint64_t> launched;
};

const onion::plugin::InventoryEntry *find_entry(
    const std::vector<onion::plugin::InventoryEntry> &inventory,
    std::string_view plugin_id) {
  const auto found = std::find_if(
      inventory.begin(), inventory.end(), [&](const auto &entry) {
        return entry.descriptor.plugin_id == plugin_id;
      });
  return found == inventory.end() ? nullptr : &*found;
}

int test_fingerprint_changes_with_content(void) {
  const auto first = make_plugin("AUTO00001", 0, 1);
  const auto second = make_plugin("AUTO00001", 0, 2);
  TEST_ASSERT_TRUE(onion::plugin::fingerprint_elf(first) !=
                   onion::plugin::fingerprint_elf(second));
  return 0;
}

int test_manager_lifecycle(void) {
  const std::string root = onion::plugin::kInstallRoot;
  (void)mkdir(ONION_DATA_ROOT, 0777);
  (void)mkdir(root.c_str(), 0777);
  const std::string auto_path = root + "/auto.elf";
  const std::string auto_marker = auto_path + ".auto_start";
  const std::string manual_path = root + "/manual.elf";
  unlink(auto_path.c_str());
  unlink(auto_marker.c_str());
  unlink(manual_path.c_str());
  TEST_ASSERT_TRUE(write_plugin(
      auto_path,
      make_plugin("AUTO00001", 0, 1)));
  TEST_ASSERT_TRUE(touch_file(auto_marker.c_str()));
  TEST_ASSERT_TRUE(
      write_plugin(manual_path, make_plugin("MANU00001", 0, 1)));

  FakeRuntime runtime;
  onion::plugin::Manager manager(onion::plugin::Repository(root), runtime);
  onion::plugin::ReconcileReport report = manager.reconcile();
  TEST_ASSERT_EQ_INT(2, static_cast<int>(report.discovered));
  TEST_ASSERT_EQ_INT(1, static_cast<int>(report.started));
  auto inventory = manager.inventory();
  TEST_ASSERT_EQ_INT(2, static_cast<int>(inventory.size()));
  TEST_ASSERT_TRUE(find_entry(inventory, "AUTO00001")->running());
  TEST_ASSERT_TRUE(find_entry(inventory, "AUTO00001")->auto_start());
  TEST_ASSERT_TRUE(!find_entry(inventory, "MANU00001")->running());
  TEST_ASSERT_TRUE(manager.set_auto_start("MANU00001", true));
  TEST_ASSERT_TRUE(find_entry(manager.inventory(), "MANU00001")->auto_start());
  TEST_ASSERT_TRUE(manager.set_auto_start("MANU00001", false));
  TEST_ASSERT_TRUE(!find_entry(manager.inventory(), "MANU00001")->auto_start());

  TEST_ASSERT_TRUE(manager.start("MANU00001"));
  TEST_ASSERT_TRUE(manager.stop("AUTO00001"));
  const size_t launches_after_stop = runtime.launched.size();
  (void)manager.reconcile();
  TEST_ASSERT_EQ_U64(launches_after_stop, runtime.launched.size());
  TEST_ASSERT_TRUE(!find_entry(manager.inventory(), "AUTO00001")->running());

  unlink(auto_path.c_str());
  unlink(auto_marker.c_str());
  (void)manager.reconcile();
  TEST_ASSERT_TRUE(write_plugin(
      auto_path,
      make_plugin("AUTO00001", 0, 1)));
  TEST_ASSERT_TRUE(touch_file(auto_marker.c_str()));
  report = manager.reconcile();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(report.started));
  const size_t launches_before_replace = runtime.launched.size();
  const int stops_before_replace = runtime.stop_count;
  TEST_ASSERT_TRUE(write_plugin(
      auto_path,
      make_plugin("AUTO00001", 0, 2)));
  report = manager.reconcile();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(report.started));
  TEST_ASSERT_EQ_U64(launches_before_replace + 1, runtime.launched.size());
  TEST_ASSERT_EQ_INT(stops_before_replace + 1, runtime.stop_count);

  const size_t launches_before_reload = runtime.launched.size();
  TEST_ASSERT_TRUE(manager.reload("MANU00001"));
  TEST_ASSERT_EQ_U64(launches_before_reload + 1, runtime.launched.size());
  TEST_ASSERT_TRUE(!manager.start("MISS00001"));

  const std::string manual_marker = manual_path + ".auto_start";
  TEST_ASSERT_TRUE(manager.set_auto_start("MANU00001", true));
  TEST_ASSERT_TRUE(access(manual_marker.c_str(), F_OK) == 0);
  TEST_ASSERT_TRUE(manager.remove("MANU00001"));
  TEST_ASSERT_TRUE(access(manual_path.c_str(), F_OK) != 0);
  TEST_ASSERT_TRUE(access(manual_marker.c_str(), F_OK) != 0);
  TEST_ASSERT_TRUE(find_entry(manager.inventory(), "MANU00001") == nullptr);

  unlink(auto_path.c_str());
  unlink(auto_marker.c_str());
  const int stops_before_remove = runtime.stop_count;
  report = manager.reconcile();
  TEST_ASSERT_EQ_INT(stops_before_remove + 1, runtime.stop_count);
  TEST_ASSERT_EQ_INT(0, static_cast<int>(report.discovered));
  TEST_ASSERT_TRUE(manager.inventory().empty());
  return 0;
}

int test_manager_flags(void) {
  const std::string root = onion::plugin::kInstallRoot;
  (void)mkdir(ONION_DATA_ROOT, 0777);
  (void)mkdir(root.c_str(), 0777);
  const std::string long_path = root + "/long.elf";
  const std::string stop_path = root + "/stop.elf";
  unlink(long_path.c_str());
  unlink(stop_path.c_str());

  FakeRuntime runtime;
  onion::plugin::Manager manager(onion::plugin::Repository(root), runtime);

  TEST_ASSERT_TRUE(write_plugin(
      long_path,
      make_plugin("LONG00001", onion::plugin::kFlagLongRunning, 1)));
  onion::plugin::ReconcileReport report = manager.reconcile();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(report.discovered));
  TEST_ASSERT_EQ_INT(0, static_cast<int>(report.started));
  auto inventory = manager.inventory();
  const auto *long_entry = find_entry(inventory, "LONG00001");
  TEST_ASSERT_TRUE(long_entry != nullptr);
  TEST_ASSERT_TRUE(long_entry->long_running());
  TEST_ASSERT_TRUE(!long_entry->auto_start());
  TEST_ASSERT_TRUE(!long_entry->running());

  TEST_ASSERT_TRUE(manager.start("LONG00001"));
  inventory = manager.inventory();
  TEST_ASSERT_TRUE(find_entry(inventory, "LONG00001")->running());

  /* Simulate an unexpected crash: a long-running plugin is relaunched. */
  runtime.alive_pids[runtime.last_pid] = false;
  const size_t launches_before_crash = runtime.launched.size();
  report = manager.reconcile();
  TEST_ASSERT_EQ_U64(launches_before_crash + 1, runtime.launched.size());
  TEST_ASSERT_EQ_INT(1, static_cast<int>(report.started));
  inventory = manager.inventory();
  TEST_ASSERT_TRUE(find_entry(inventory, "LONG00001")->running());

  /* STOP_SUPPORTED reaches the runtime stop call with its flag set. */
  TEST_ASSERT_TRUE(write_plugin(
      stop_path,
      make_plugin("STOP00001", onion::plugin::kFlagStopSupported, 1)));
  (void)manager.reconcile();
  TEST_ASSERT_TRUE(manager.start("STOP00001"));
  inventory = manager.inventory();
  const auto *stop_entry = find_entry(inventory, "STOP00001");
  TEST_ASSERT_TRUE(stop_entry != nullptr && stop_entry->stop_supported());
  runtime.last_stop_flags = 0;
  TEST_ASSERT_TRUE(manager.stop("STOP00001"));
  TEST_ASSERT_EQ_U64(onion::plugin::kFlagStopSupported,
                     runtime.last_stop_flags);

  /* A non-stop-supported plugin stops without the graceful flag. */
  runtime.last_stop_flags = 0;
  TEST_ASSERT_TRUE(manager.stop("LONG00001"));
  TEST_ASSERT_EQ_U64(onion::plugin::kFlagLongRunning, runtime.last_stop_flags);

  unlink(long_path.c_str());
  unlink(stop_path.c_str());
  (void)manager.reconcile();
  return 0;
}

} // namespace

extern "C" int test_plugin_manager_suite(void) {
  int failures = 0;
  failures += onion_test_run("plugin_manager.fingerprint",
                             test_fingerprint_changes_with_content);
  failures += onion_test_run("plugin_manager.lifecycle",
                             test_manager_lifecycle);
  failures += onion_test_run("plugin_manager.flags",
                             test_manager_flags);
  return failures;
}
