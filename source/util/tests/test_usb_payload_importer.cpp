#include "test_harness.h"
#include "usb_payload_importer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace onion::shellui::usb_payloads;

namespace {

struct Fixture {
  std::string root;
  std::string usb_prefix;
  std::string destination;

  Fixture() {
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
    if (!root.empty())
      fs::remove_all(root);
  }
};

std::string contents(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
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

  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, false) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "new payload");
  TEST_ASSERT_TRUE(!fs::exists(f.destination + "/payload.elf.installing"));
  entries = scan(f.usb_prefix, f.destination, &found);
  TEST_ASSERT_TRUE(entries.size() == 1 && entries[0].installed);
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, false) ==
                   ImportResult::DestinationAlreadyExists);

  std::ofstream(f.usb_prefix + "1/payload.ELF", std::ios::binary)
      << "replacement";
  std::ofstream(f.destination + "/payload.elf.installing") << "stale";
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, true) ==
                   ImportResult::CannotCreateDestination);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "new payload");
  fs::remove(f.destination + "/payload.elf.installing");
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, true) ==
                   ImportResult::Completed);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "replacement");
  TEST_ASSERT_TRUE(!fs::exists(f.destination + "/payload.elf.installing"));

  fs::remove(f.usb_prefix + "1/payload.ELF");
  TEST_ASSERT_TRUE(import_payload(entries[0], f.destination, true) ==
                   ImportResult::CannotOpenSource);
  TEST_ASSERT_TRUE(contents(f.destination + "/payload.elf") == "replacement");

  Entry invalid = entries[0];
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

} // namespace

extern "C" int test_usb_payload_importer_suite(void) {
  return onion_test_run("usb_payloads.scan_import_replace",
                        test_scan_and_import);
}
