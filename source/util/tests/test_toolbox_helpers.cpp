/* Host tests for toolbox_helpers (display path + payload name filter). */
#include "test_harness.h"

#include "toolbox_helpers.hpp"

#include <string>
#include <cstdlib>
#include <unistd.h>

using namespace toolbox;

static int test_display_strip_user(void) {
  std::string a = display_path_for_ui("/user/data/OnionHEN/payloads/x.elf");
  std::string b = display_path_for_ui("/user");
  TEST_ASSERT_STREQ("/data/OnionHEN/payloads/x.elf", a.c_str());
  TEST_ASSERT_STREQ("", b.c_str());
  return 0;
}

static int test_display_map_usb(void) {
  std::string a = display_path_for_ui("/usb0/onionhen/payloads/a.elf");
  std::string b = display_path_for_ui("/usb3/x");
  TEST_ASSERT_STREQ("/mnt/usb0/onionhen/payloads/a.elf", a.c_str());
  TEST_ASSERT_STREQ("/mnt/usb3/x", b.c_str());
  return 0;
}

static int test_display_passthrough(void) {
  std::string a = display_path_for_ui("/data/OnionHEN/x");
  std::string b = display_path_for_ui("relative/path");
  std::string c = display_path_for_ui("");
  std::string d = display_path_for_ui("/userdata/x");
  TEST_ASSERT_STREQ("/data/OnionHEN/x", a.c_str());
  TEST_ASSERT_STREQ("relative/path", b.c_str());
  TEST_ASSERT_STREQ("", c.c_str());
  TEST_ASSERT_STREQ("/userdata/x", d.c_str());
  return 0;
}

static int test_payload_name_accept(void) {
  TEST_ASSERT_TRUE(is_payload_elf_name("bar.elf"));
  TEST_ASSERT_TRUE(is_payload_elf_name("CUSA12345.elf"));
  TEST_ASSERT_TRUE(is_payload_elf_name("x.elf"));
  TEST_ASSERT_TRUE(is_payload_elf_name("ftpsrv.elf"));
  TEST_ASSERT_TRUE(is_payload_elf_name("FTPSRV-PS5.elf"));
  return 0;
}

static int test_payload_name_reject(void) {
  TEST_ASSERT_TRUE(!is_payload_elf_name(nullptr));
  TEST_ASSERT_TRUE(!is_payload_elf_name(""));
  TEST_ASSERT_TRUE(!is_payload_elf_name("readme.txt"));
  TEST_ASSERT_TRUE(!is_payload_elf_name("foo.plugin"));
  TEST_ASSERT_TRUE(!is_payload_elf_name("bar.elf.auto_start"));
  TEST_ASSERT_TRUE(!is_payload_elf_name(".elf"));
  TEST_ASSERT_TRUE(!is_payload_elf_name("."));
  TEST_ASSERT_TRUE(!is_payload_elf_name(".."));
  TEST_ASSERT_TRUE(!is_payload_elf_name("note.elf.txt"));
  return 0;
}

static int test_installed_payload_paths(void) {
  for (const char *path : {"/data/OnionHEN/payloads/tool.elf",
                           "/user/data/OnionHEN/payloads/tool.elf",
                           "/data/onionhen/payloads/tool.elf",
                           "/user/data/onionhen/payloads/tool.elf"})
    TEST_ASSERT_TRUE(is_installed_payload_path(path));
  for (const char *path : {"/usb0/OnionHEN/payloads/tool.elf",
                           "/mnt/usb1/OnionHEN/payloads/tool.elf",
                           "/data/OnionHEN/plugins/tool.elf",
                           "/data/OnionHEN/payloads/../tool.elf",
                           "/data/OnionHEN/payloads/sub/tool.elf",
                           "/data/OnionHEN/payloads/tool.elf.json",
                           "/userdata/OnionHEN/payloads/tool.elf",
                           "/data/OnionHEN/payloads/.elf", "tool.elf", ""})
    TEST_ASSERT_TRUE(!is_installed_payload_path(path));
  return 0;
}

static int test_legacy_payload_staging_name(void) {
  TEST_ASSERT_TRUE(is_legacy_payload_staging_name("p7d26c949e1b9045d.elf"));
  TEST_ASSERT_TRUE(!is_legacy_payload_staging_name("p7D26c949e1b9045d.elf"));
  TEST_ASSERT_TRUE(!is_legacy_payload_staging_name("ftpsrv.elf"));
  TEST_ASSERT_TRUE(!is_legacy_payload_staging_name("p7d26c949e1b9045d.elf.bak"));
  return 0;
}

static int test_payload_directory_rejects_symlinks(void) {
  char root[] = "/tmp/onion-payload-directory-XXXXXX";
  TEST_ASSERT_TRUE(mkdtemp(root) != nullptr);
  const std::string app = std::string(root) + "/OnionHEN";
  const std::string payloads = app + "/payloads";
  const std::string usb = std::string(root) + "/usb";
  const std::string usb_payloads = usb + "/payloads";
  const std::string path = payloads + "/tool.elf";
  TEST_ASSERT_TRUE(!has_real_payload_directory(path));
  TEST_ASSERT_EQ_INT(0, mkdir(app.c_str(), 0700));
  TEST_ASSERT_EQ_INT(0, mkdir(payloads.c_str(), 0700));
  TEST_ASSERT_EQ_INT(0, mkdir(usb.c_str(), 0700));
  TEST_ASSERT_EQ_INT(0, mkdir(usb_payloads.c_str(), 0700));
  TEST_ASSERT_TRUE(has_real_payload_directory(path));

  TEST_ASSERT_EQ_INT(0, rmdir(payloads.c_str()));
  TEST_ASSERT_EQ_INT(0, symlink(usb_payloads.c_str(), payloads.c_str()));
  TEST_ASSERT_TRUE(!has_real_payload_directory(path));
  TEST_ASSERT_EQ_INT(0, unlink(payloads.c_str()));
  TEST_ASSERT_EQ_INT(0, rmdir(app.c_str()));
  TEST_ASSERT_EQ_INT(0, symlink(usb.c_str(), app.c_str()));
  TEST_ASSERT_TRUE(!has_real_payload_directory(path));

  TEST_ASSERT_EQ_INT(0, unlink(app.c_str()));
  TEST_ASSERT_EQ_INT(0, rmdir(usb_payloads.c_str()));
  TEST_ASSERT_EQ_INT(0, rmdir(usb.c_str()));
  TEST_ASSERT_EQ_INT(0, rmdir(root));
  return 0;
}

static int test_elf_key_from_name(void) {
  char key[64];
  TEST_ASSERT_TRUE(elf_key_from_name("payload.elf", key, sizeof(key)));
  TEST_ASSERT_STREQ("payload", key);
  TEST_ASSERT_TRUE(
      elf_key_from_name("/user/data/OnionHEN/payloads/x.elf", key, sizeof(key)));
  TEST_ASSERT_STREQ("x", key);
  TEST_ASSERT_TRUE(!elf_key_from_name(".elf", key, sizeof(key)));
  return 0;
}

extern "C" int test_toolbox_helpers_suite(void) {
  int fails = 0;
  fails += onion_test_run("toolbox.display_strip_user", test_display_strip_user);
  fails += onion_test_run("toolbox.display_map_usb", test_display_map_usb);
  fails += onion_test_run("toolbox.display_passthrough", test_display_passthrough);
  fails += onion_test_run("toolbox.payload_name_accept", test_payload_name_accept);
  fails += onion_test_run("toolbox.payload_name_reject", test_payload_name_reject);
  fails += onion_test_run("toolbox.installed_payload_paths", test_installed_payload_paths);
  fails += onion_test_run("toolbox.payload_directory_symlinks",
                          test_payload_directory_rejects_symlinks);
  fails += onion_test_run("toolbox.payload_legacy_staging_name",
                          test_legacy_payload_staging_name);
  fails += onion_test_run("toolbox.elf_key_from_name", test_elf_key_from_name);
  return fails;
}
