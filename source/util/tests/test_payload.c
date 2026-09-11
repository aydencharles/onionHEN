/* Host unit tests for libonion_payload payload helpers (no elfldr socket). */
#include "test_harness.h"
#include "test_support.h"

#include <onion/payload.h>
#include <onion/elf_name.h>
#include <elfldr_remote.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void onion_test_elfldr_reset(void);
void onion_test_elfldr_configure(bool available, pid_t launch_pid);
int onion_test_elfldr_launch_calls(void);
uint16_t onion_test_elfldr_last_port(void);
const char *onion_test_elfldr_last_path(void);
void onion_test_live_pid(pid_t pid);

static int test_is_elf(void) {
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};
  const unsigned char junk[4] = {0, 0, 0, 0};

  TEST_ASSERT_TRUE(onion_payload_is_elf(elf, sizeof(elf)));
  TEST_ASSERT_TRUE(!onion_payload_is_elf(junk, sizeof(junk)));
  TEST_ASSERT_TRUE(!onion_payload_is_elf(elf, 3));
  TEST_ASSERT_TRUE(!onion_payload_is_elf(NULL, 16));
  return 0;
}

static int test_pid_path(void) {
  char path[128];
  onion_payload_pid_path(path, sizeof(path), "mytool");
  TEST_ASSERT_STREQ("/tmp/onionhen/pid/mytool.PID", path);
  return 0;
}

static int test_elf_name_stem(void) {
  TEST_ASSERT_EQ_INT(3, (int)onion_elf_name_stem_n("foo.elf", 7));
  TEST_ASSERT_EQ_INT(3, (int)onion_elf_name_stem_n("foo", 3));
  TEST_ASSERT_EQ_INT(0, (int)onion_elf_name_stem_n(".elf", 4));
  TEST_ASSERT_TRUE(onion_elf_name_has_suffix("foo.elf", 7));
  TEST_ASSERT_TRUE(!onion_elf_name_has_suffix("foo", 3));
  TEST_ASSERT_TRUE(!onion_elf_name_has_suffix(".elf.bak", 8));
  return 0;
}

static int test_elf_key_from_name(void) {
  char key[64];
  TEST_ASSERT_TRUE(onion_payload_elf_key_from_name("foo.elf", key, sizeof(key)));
  TEST_ASSERT_STREQ("foo", key);
  TEST_ASSERT_TRUE(
      onion_payload_elf_key_from_name("/data/OnionHEN/payloads/bar.elf", key,
                                     sizeof(key)));
  TEST_ASSERT_STREQ("bar", key);
  TEST_ASSERT_TRUE(onion_payload_elf_key_from_name("noext", key, sizeof(key)));
  TEST_ASSERT_STREQ("noext", key);
  TEST_ASSERT_TRUE(!onion_payload_elf_key_from_name(".elf", key, sizeof(key)));
  TEST_ASSERT_TRUE(!onion_payload_elf_key_from_name(NULL, key, sizeof(key)));
  TEST_ASSERT_TRUE(!onion_payload_elf_key_from_name("", key, sizeof(key)));
  return 0;
}

static int test_path_identity(void) {
  char first[ONION_PAYLOAD_IDENTITY_SIZE];
  char repeated[ONION_PAYLOAD_IDENTITY_SIZE];
  char second[ONION_PAYLOAD_IDENTITY_SIZE];

  TEST_ASSERT_TRUE(onion_payload_identity_from_path(
      "/data/OnionHEN/payloads/tool.elf", first, sizeof(first)));
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(
      "/data/OnionHEN/payloads/tool.elf", repeated, sizeof(repeated)));
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(
      "/mnt/usb0/OnionHEN/payloads/tool.elf", second, sizeof(second)));
  TEST_ASSERT_STREQ(first, repeated);
  TEST_ASSERT_TRUE(strcmp(first, second) != 0);
  TEST_ASSERT_EQ_INT(17, (int)strlen(first));
  TEST_ASSERT_TRUE(first[0] == 'p');
  TEST_ASSERT_TRUE(!onion_payload_identity_from_path("", first, sizeof(first)));
  TEST_ASSERT_TRUE(!onion_payload_identity_from_path(
      "/data/OnionHEN/payloads/tool.elf", first, sizeof(first) - 1));
  return 0;
}

static int test_pid_file_roundtrip(void) {
  char path[256];
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".PID", "", 0, path,
                                                   sizeof(path)));

  onion_payload_write_pid_file(path, 4242);
  TEST_ASSERT_EQ_INT(4242, (int)onion_payload_read_pid_file(path));

  onion_payload_write_pid_file(path, -1);
  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_read_pid_file(path));

  onion_payload_write_pid_file(path, 1);
  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_read_pid_file(path));
  onion_payload_write_pid_file(path, 0);
  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_read_pid_file(path));

  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_read_pid_file(
                             "/tmp/onion-payload-pid-missing-xyz.PID"));
  onion_test_remove_file(path);
  return 0;
}

static int test_read_file(void) {
  char path[256];
  const char payload[] = "hello-payload";
  size_t sz = 0;
  uint8_t *buf = NULL;

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".bin", payload,
                                                   sizeof(payload) - 1, path,
                                                   sizeof(path)));
  buf = onion_payload_read_file(path, &sz);
  TEST_ASSERT_TRUE(buf != NULL);
  TEST_ASSERT_EQ_U64(sizeof(payload) - 1, sz);
  TEST_ASSERT_MEMEQ(payload, buf, sizeof(payload) - 1);
  free(buf);

  TEST_ASSERT_TRUE(onion_payload_read_file(NULL, &sz) == NULL);
  TEST_ASSERT_TRUE(onion_payload_read_file(path, NULL) == NULL);
  TEST_ASSERT_TRUE(onion_payload_read_file("/tmp/onion-missing-payload-xyz",
                                          &sz) == NULL);

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".bin", "", 0, path,
                                                   sizeof(path)));
  TEST_ASSERT_TRUE(onion_payload_read_file(path, &sz) == NULL);
  onion_test_remove_file(path);
  return 0;
}

static int test_strict_private_loader_policy(void) {
  const char *path = "/data/OnionHEN/payloads/strict-test.elf";

  onion_test_elfldr_reset();
  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_launch_elfldr(
                             "strict-test", path));
  TEST_ASSERT_EQ_INT(0, onion_test_elfldr_launch_calls());

  onion_test_elfldr_configure(true, 4242);
  TEST_ASSERT_EQ_INT(4242, (int)onion_payload_launch_elfldr(
                               "strict-test", path));
  TEST_ASSERT_EQ_INT(1, onion_test_elfldr_launch_calls());
  TEST_ASSERT_EQ_INT(ONION_ELFLDR_PORT, onion_test_elfldr_last_port());
  TEST_ASSERT_STREQ(path, onion_test_elfldr_last_path());

  onion_test_elfldr_configure(true, 0);
  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_launch_elfldr(
                             "strict-test", path));
  TEST_ASSERT_EQ_INT(2, onion_test_elfldr_launch_calls());

  onion_test_elfldr_configure(true, -1);
  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_launch_elfldr(
                             "strict-test", path));
  TEST_ASSERT_EQ_INT(3, onion_test_elfldr_launch_calls());
  return 0;
}

static int test_load_requires_real_pid(void) {
  char path[256];
  char pid_path[256];
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));

  onion_test_elfldr_reset();
  onion_test_elfldr_configure(true, 5151);
  TEST_ASSERT_TRUE(onion_payload_load(path, NULL));
  TEST_ASSERT_STREQ(path, onion_test_elfldr_last_path());

  char path_copy[256];
  snprintf(path_copy, sizeof(path_copy), "%s", path);
  const char *base = strrchr(path_copy, '/');
  base = base ? base + 1 : path_copy;
  char key[64];
  TEST_ASSERT_TRUE(onion_payload_elf_key_from_name(base, key, sizeof(key)));
  onion_payload_pid_path(pid_path, sizeof(pid_path), key);
  TEST_ASSERT_EQ_INT(5151, (int)onion_payload_read_pid_file(pid_path));

  onion_test_elfldr_configure(true, 0);
  TEST_ASSERT_TRUE(!onion_payload_load(path, NULL));
  TEST_ASSERT_EQ_INT(-1, (int)onion_payload_read_pid_file(pid_path));

  onion_test_remove_file(path);
  onion_test_remove_file(pid_path);
  return 0;
}

static int test_payload_running_without_live_pid(void) {
  TEST_ASSERT_TRUE(!onion_payload_running(NULL));
  TEST_ASSERT_TRUE(!onion_payload_running("missing-title"));
  return 0;
}

static int test_load_preserves_running_instance(void) {
  char path[256];
  char pid_path[256];
  char key[64];
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  TEST_ASSERT_TRUE(onion_payload_elf_key_from_name(base, key, sizeof(key)));
  onion_payload_pid_path(pid_path, sizeof(pid_path), key);
  onion_payload_write_pid_file(pid_path, 6262);

  onion_test_elfldr_reset();
  onion_test_live_pid(6262);
  onion_test_elfldr_configure(true, 7373);
  TEST_ASSERT_TRUE(onion_payload_load(path, NULL));
  TEST_ASSERT_EQ_INT(0, onion_test_elfldr_launch_calls());
  TEST_ASSERT_EQ_INT(6262, (int)onion_payload_read_pid_file(pid_path));

  onion_test_elfldr_reset();
  onion_test_remove_file(pid_path);
  onion_test_remove_file(path);
  return 0;
}

static int test_load_with_explicit_key(void) {
  char path[256];
  char pid_path[256];
  const char key[] = "p0123456789abcdef";
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));
  onion_test_elfldr_reset();
  onion_test_elfldr_configure(true, 8181);
  TEST_ASSERT_TRUE(onion_payload_load_with_key(path, NULL, key));
  onion_payload_pid_path(pid_path, sizeof(pid_path), key);
  TEST_ASSERT_EQ_INT(8181, (int)onion_payload_read_pid_file(pid_path));

  onion_test_remove_file(pid_path);
  onion_test_remove_file(path);
  return 0;
}

static int test_builtin_identity_payloads_allowed(void) {
  char path[256];
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));

  onion_test_elfldr_reset();
  onion_test_elfldr_configure(true, 5151);
  TEST_ASSERT_EQ_INT(5151, (int)onion_payload_launch_elfldr(
                             "ftpsrv", path));
  TEST_ASSERT_TRUE(onion_payload_load(path, "ftpsrv.elf"));
  TEST_ASSERT_TRUE(onion_payload_load(path, "ftpsrv-ps5.elf"));
  TEST_ASSERT_TRUE(onion_payload_load(path, "kstuff.elf"));
  TEST_ASSERT_EQ_INT(4, onion_test_elfldr_launch_calls());

  char pid_path[256];
  onion_payload_pid_path(pid_path, sizeof(pid_path), "ftpsrv");
  onion_test_remove_file(pid_path);
  onion_payload_pid_path(pid_path, sizeof(pid_path), "ftpsrv-ps5");
  onion_test_remove_file(pid_path);
  onion_payload_pid_path(pid_path, sizeof(pid_path), "kstuff");
  onion_test_remove_file(pid_path);
  onion_test_remove_file(path);
  return 0;
}

int test_payload_suite(void) {
  int failures = 0;
  failures += onion_test_run("payload.is_elf", test_is_elf);
  failures += onion_test_run("payload.pid_path", test_pid_path);
  failures += onion_test_run("payload.elf_name_stem", test_elf_name_stem);
  failures += onion_test_run("payload.elf_key_from_name", test_elf_key_from_name);
  failures += onion_test_run("payload.path_identity", test_path_identity);
  failures += onion_test_run("payload.pid_file_roundtrip",
                             test_pid_file_roundtrip);
  failures += onion_test_run("payload.read_file", test_read_file);
  failures += onion_test_run("payload.strict_private_loader_policy",
                             test_strict_private_loader_policy);
  failures += onion_test_run("payload.load_requires_real_pid",
                             test_load_requires_real_pid);
  failures += onion_test_run("payload.running_without_live_pid",
                             test_payload_running_without_live_pid);
  failures += onion_test_run("payload.load_preserves_running_instance",
                             test_load_preserves_running_instance);
  failures += onion_test_run("payload.load_with_explicit_key",
                             test_load_with_explicit_key);
  failures += onion_test_run("payload.builtin_identity_payloads_allowed",
                             test_builtin_identity_payloads_allowed);
  return failures;
}
