#include "test_harness.h"
#include "usb_payload_action.hpp"
#include "usb_payload_importer.hpp"
#include "usb_payload_test_io.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace onion::shellui::usb_payloads;

namespace {

struct IoFaults {
  std::string concurrent_destination;
  int link_calls = 0;
  int sync_calls = 0;
  int fail_sync_call = 0;
  bool fail_write_after_staging = false;
  bool short_write_next = false;
  bool fail_next_write = false;
} io_faults;

struct Fixture {
  std::string root;
  std::string usb_prefix;
  std::string destination;

  Fixture() {
    io_faults = {};
    char path[] = "/tmp/onion-usb-payloads-XXXXXX";
    char *created = mkdtemp(path);
    if (created) {
      root = created;
      usb_prefix = root + "/usb";
      destination = root + "/payloads";
      fs::create_directory(destination);
    }
  }
  ~Fixture() {
    io_faults = {};
    if (!root.empty())
      fs::remove_all(root);
  }
};

std::string contents(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

bool no_staging_files(const std::string &directory) {
  for (const auto &file : fs::directory_iterator(directory))
    if (file.path().filename().string().starts_with(".usb-payload-"))
      return false;
  return true;
}

int test_import_without_hard_links() {
  Fixture f;
  TEST_ASSERT_TRUE(!f.root.empty());
  fs::create_directory(f.usb_prefix + "1");
  const Entry entry{"kraken-mount.elf", f.usb_prefix + "1/kraken-mount.elf",
                    0, 1, false};
  std::string payload(4 * 1024 * 1024 + 13, 'K');
  payload.replace(0, 4, "\x7f" "ELF");
  payload[65536] = '\0';
  std::ofstream(entry.source_path, std::ios::binary) << payload;

  TEST_ASSERT_TRUE(import_payload(entry, f.destination, false) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/kraken-mount.elf") == payload);
  TEST_ASSERT_EQ_INT(0, io_faults.link_calls);
  TEST_ASSERT_TRUE(no_staging_files(f.destination));
  return 0;
}

int test_concurrent_destination_preserved() {
  Fixture f;
  TEST_ASSERT_TRUE(!f.root.empty());
  fs::create_directory(f.usb_prefix + "0");
  const Entry entry{"payload.elf", f.usb_prefix + "0/payload.elf", 0, 0, false};
  std::ofstream(entry.source_path) << "USB copy";
  io_faults.concurrent_destination = f.destination + "/payload.elf";

  TEST_ASSERT_TRUE(import_payload(entry, f.destination, false) ==
                   ImportResult::DestinationAlreadyExists);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "other writer");
  TEST_ASSERT_TRUE(no_staging_files(f.destination));
  return 0;
}

int test_failed_install_cleanup() {
  Fixture f;
  TEST_ASSERT_TRUE(!f.root.empty());
  fs::create_directory(f.usb_prefix + "0");
  const Entry entry{"payload.elf", f.usb_prefix + "0/payload.elf", 0, 0, false};
  std::ofstream(entry.source_path) << "USB copy";

  io_faults.fail_write_after_staging = true;
  TEST_ASSERT_TRUE(import_payload(entry, f.destination, false) ==
                   ImportResult::WriteError);
  TEST_ASSERT_TRUE(!fs::exists(f.destination + "/payload.elf"));
  TEST_ASSERT_TRUE(no_staging_files(f.destination));

  io_faults = {};
  io_faults.fail_sync_call = 2;
  TEST_ASSERT_TRUE(import_payload(entry, f.destination, false) ==
                   ImportResult::WriteError);
  TEST_ASSERT_TRUE(!fs::exists(f.destination + "/payload.elf"));
  TEST_ASSERT_TRUE(no_staging_files(f.destination));

  io_faults = {};
  TEST_ASSERT_TRUE(import_payload(entry, f.destination, false) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "USB copy");
  return 0;
}

int test_action_identity_after_reorder() {
  const Entry displayed{"first_name.elf", "/usb1/first_name.elf", 10, 1,
                        true};
  const Entry other{"other_name.elf", "/usb0/other_name.elf", 12, 0,
                    true};
  const std::string displayed_id = action_id(displayed, true);
  const std::vector<Entry> refreshed{other, displayed};

  const Entry *found = find_action_entry(refreshed, displayed_id, true);
  TEST_ASSERT_TRUE(found != nullptr && found->name == displayed.name &&
                   found->usb_index == displayed.usb_index);
  TEST_ASSERT_TRUE(find_action_entry({other}, displayed_id, true) == nullptr);
  TEST_ASSERT_TRUE(find_action_entry(refreshed, displayed_id, false) == nullptr);
  TEST_ASSERT_TRUE(find_action_entry(refreshed, "id_usb_payload_replace_1_",
                                     true) == nullptr);
  TEST_ASSERT_TRUE(find_action_entry(refreshed,
                                     "id_usb_payload_replace_9_first_name.elf",
                                     true) == nullptr);

  const Entry same_name_other_usb{"first_name.elf", "/usb0/first_name.elf",
                                  10, 0, true};
  const std::vector<Entry> duplicates{same_name_other_usb, displayed};
  found = find_action_entry(duplicates, displayed_id, true);
  TEST_ASSERT_TRUE(found != nullptr && found->usb_index == 1);

  Entry newline = displayed;
  newline.name = "line\nbreak.elf";
  Entry literal_percent = displayed;
  literal_percent.name = "line%0Abreak.elf";
  Entry tab_and_return = displayed;
  tab_and_return.name = "tab\treturn\r.elf";
  const std::vector<Entry> unusual{newline, literal_percent, tab_and_return};
  const std::string newline_id = action_id(newline, true);
  TEST_ASSERT_TRUE(newline_id.find('\n') == std::string::npos);
  TEST_ASSERT_TRUE(newline_id != action_id(literal_percent, true));
  found = find_action_entry(unusual, newline_id, true);
  TEST_ASSERT_TRUE(found != nullptr && found->name == newline.name);
  found = find_action_entry(unusual, action_id(literal_percent, true), true);
  TEST_ASSERT_TRUE(found != nullptr && found->name == literal_percent.name);
  const std::string whitespace_id = action_id(tab_and_return, true);
  TEST_ASSERT_TRUE(whitespace_id.find_first_of("\t\r\n") ==
                   std::string::npos);
  found = find_action_entry(unusual, whitespace_id, true);
  TEST_ASSERT_TRUE(found != nullptr && found->name == tab_and_return.name);
  TEST_ASSERT_TRUE(find_action_entry(
                       unusual, "id_usb_payload_replace_1_line%0Xbreak.elf",
                       true) == nullptr);
  return 0;
}

int test_scan_and_import() {
  Fixture f;
  TEST_ASSERT_TRUE(!f.root.empty());
  bool found = true;
  auto entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(!found && entries.empty());

  fs::create_directory(f.usb_prefix + "1");
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(found && entries.empty());
  std::ofstream(f.usb_prefix + "1/payload.ELF", std::ios::binary)
      << "new payload";
  std::ofstream(f.usb_prefix + "1/file.txt") << "text";
  std::ofstream(f.usb_prefix + "1/.hidden.elf") << "hidden";
  fs::create_directory(f.usb_prefix + "1/folder.elf");
  symlink((f.usb_prefix + "1/payload.ELF").c_str(),
          (f.usb_prefix + "1/link.elf").c_str());
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(found && entries.size() == 1);
  TEST_ASSERT_TRUE(entries[0].usb_index == 1);
  TEST_ASSERT_TRUE(entries[0].name == "payload.ELF");
  TEST_ASSERT_TRUE(entries[0].size == 11 && !entries[0].installed);
  const Entry payload_entry = entries[0];

  std::ofstream(f.destination + "/payload.elf.installing") << "stale";
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, false) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "new payload");
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf.installing") ==
                   "stale");
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(entries.size() == 1 && entries[0].installed);
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, false) ==
                   ImportResult::DestinationAlreadyExists);

  std::ofstream(f.usb_prefix + "1/payload.ELF", std::ios::binary)
      << "replacement";
  std::ofstream(f.destination + "/.usb-payload-ABCDEF") << "old staging";
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, true) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "replacement");
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf.installing") ==
                   "stale");
  TEST_ASSERT_TRUE(contents(f.destination + "/.usb-payload-ABCDEF") ==
                   "old staging");
  fs::remove(f.destination + "/payload.elf.installing");
  fs::remove(f.destination + "/.usb-payload-ABCDEF");

  std::ofstream(f.usb_prefix + "1/blocked.elf") << "blocked payload";
  fs::create_directory(f.destination + "/blocked.elf");
  Entry blocked{"blocked.elf", f.usb_prefix + "1/blocked.elf", 15, 1, true};
  TEST_ASSERT_TRUE(import_payload(blocked, f.destination, true) ==
                   ImportResult::RenameError);
  for (const auto &file : fs::directory_iterator(f.destination))
    TEST_ASSERT_TRUE(file.path().filename().string().find(".usb-payload-") !=
                     0);
  fs::remove(f.destination + "/blocked.elf");
  fs::remove(f.usb_prefix + "1/blocked.elf");

  const std::string long_name(251, 'a');
  std::ofstream(f.usb_prefix + "1/" + long_name + ".ELF") << "long payload";
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(entries.size() == 2);
  const Entry long_entry = entries[0].name == long_name + ".ELF"
                               ? entries[0]
                               : entries[1];
  TEST_ASSERT_TRUE(import_payload(long_entry, f.destination, false) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/" + long_name + ".elf") ==
                   "long payload");
  fs::remove(f.usb_prefix + "1/" + long_name + ".ELF");

  fs::remove(f.usb_prefix + "1/payload.ELF");
  TEST_ASSERT_TRUE(import_payload(payload_entry, f.destination, true) ==
                   ImportResult::CannotOpenSource);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "replacement");

  Entry invalid = payload_entry;
  invalid.name = "../outside.elf";
  TEST_ASSERT_TRUE(import_payload(invalid, f.destination, true) ==
                   ImportResult::InvalidName);
  TEST_ASSERT_TRUE(!fs::exists(f.root + "/outside.elf"));

  fs::create_directory(f.usb_prefix + "0");
  std::ofstream(f.usb_prefix + "0/other.elf") << "other";
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(entries.size() == 1 && entries[0].usb_index == 0);
  std::ofstream(f.usb_prefix + "1/second.elf") << "second";
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(entries.size() == 2);
  TEST_ASSERT_TRUE(entries[0].usb_index == 0 && entries[1].usb_index == 1);
  fs::remove_all(f.usb_prefix + "0");
  fs::remove_all(f.usb_prefix + "1");
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(!found && entries.empty());
  return 0;
}

int test_native_access_directory() {
  Fixture f;
  TEST_ASSERT_TRUE(!f.root.empty());
  fs::create_directory(f.usb_prefix + "1");
  const std::string native_view = f.root + "/native-reader-view";
  fs::create_directory(native_view);
  std::ofstream(native_view + "/payload.elf") << "native USB contents";
  bool opened = false;
  const auto stale_view = scan(f.usb_prefix, f.destination, &opened);
  TEST_ASSERT_TRUE(opened && stale_view.empty());
  auto entries = scan_directory(native_view, f.destination, 1, &opened);
  TEST_ASSERT_TRUE(opened && entries.size() == 1);
  TEST_ASSERT_EQ_INT(1, entries[0].usb_index);
  TEST_ASSERT_TRUE(entries[0].source_path == native_view + "/payload.elf");
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, false) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") ==
                   "native USB contents");

  fs::remove_all(native_view);
  entries = scan_directory(native_view, f.destination, 1, &opened);
  TEST_ASSERT_TRUE(!opened && entries.empty());
  entries = scan_directory(f.usb_prefix + "1", f.destination, 1, &opened);
  TEST_ASSERT_TRUE(opened && entries.empty());
  return 0;
}

} // namespace

extern "C" int onion_test_usb_link(const char *, const char *) {
  ++io_faults.link_calls;
  errno = EOPNOTSUPP;
  return -1;
}

extern "C" int onion_test_usb_fsync(int fd) {
  if (++io_faults.sync_calls == io_faults.fail_sync_call) {
    errno = EIO;
    return -1;
  }
  const int result = fsync(fd);
  if (result == 0) {
    if (!io_faults.concurrent_destination.empty()) {
      std::ofstream(io_faults.concurrent_destination) << "other writer";
      io_faults.concurrent_destination.clear();
    }
    if (io_faults.fail_write_after_staging) {
      io_faults.fail_write_after_staging = false;
      io_faults.short_write_next = true;
    }
  }
  return result;
}

extern "C" ssize_t onion_test_usb_write(int fd, const void *buffer, size_t size) {
  if (io_faults.short_write_next) {
    io_faults.short_write_next = false;
    io_faults.fail_next_write = true;
    return write(fd, buffer, size > 1 ? size / 2 : size);
  }
  if (io_faults.fail_next_write) {
    io_faults.fail_next_write = false;
    errno = ENOSPC;
    return -1;
  }
  return write(fd, buffer, size);
}

extern "C" int test_usb_payload_importer_suite(void) {
  int failures = 0;
  failures += onion_test_run("usb_payloads.import_without_hard_links",
                             test_import_without_hard_links);
  failures += onion_test_run("usb_payloads.concurrent_destination_preserved",
                             test_concurrent_destination_preserved);
  failures += onion_test_run("usb_payloads.failed_install_cleanup",
                             test_failed_install_cleanup);
  failures += onion_test_run("usb_payloads.action_identity_after_reorder",
                             test_action_identity_after_reorder);
  failures += onion_test_run("usb_payloads.scan_import_replace",
                             test_scan_and_import);
  failures += onion_test_run("usb_payloads.native_access_directory",
                             test_native_access_directory);
  return failures;
}

#ifdef ONION_USB_PAYLOAD_TEST_MAIN
extern "C" int test_usb_storage_suite(void);
int main() {
  return test_usb_payload_importer_suite() + test_usb_storage_suite();
}
#endif
