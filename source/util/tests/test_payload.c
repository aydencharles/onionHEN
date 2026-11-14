/* Host unit tests for libonion_payload payload helpers (no elfldr socket). */
#include "test_harness.h"
#include "test_support.h"

#include <onion/payload.h>
#include <onion/elf_name.h>
#include <elfldr_remote.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static int write_marker(const char *path) {
  FILE *file = fopen(path, "wb");
  if (!file)
    return -1;
  const int written = fputs("marker", file);
  return fclose(file) == 0 && written >= 0 ? 0 : -1;
}

static int test_remove_stopped_payload(void) {
  char path[256], marker[300], config[300], neighbor[300], pid_path[256];
  char identity[ONION_PAYLOAD_IDENTITY_SIZE];
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".elf", "\x7f" "ELF", 4,
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  snprintf(neighbor, sizeof(neighbor), "%s.neighbor.elf", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, write_marker(config));
  TEST_ASSERT_EQ_INT(0, write_marker(neighbor));
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(path, identity, sizeof(identity)));
  onion_payload_pid_path(pid_path, sizeof(pid_path), identity);
  onion_payload_write_pid_file(pid_path, 4242);
  onion_test_elfldr_reset();

  TEST_ASSERT_TRUE(onion_payload_remove(path));
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && access(marker, F_OK) != 0 &&
                   access(config, F_OK) != 0 && access(pid_path, F_OK) != 0);
  TEST_ASSERT_EQ_INT(0, access(neighbor, F_OK));
  TEST_ASSERT_TRUE(onion_payload_remove(neighbor)); /* Missing sidecars are fine. */
  return 0;
}

static int test_remove_long_basename(void) {
  char directory[] = "/tmp/onion-payload-remove-XXXXXX";
  char name[256], path[512];
  TEST_ASSERT_TRUE(mkdtemp(directory) != NULL);
  memset(name, 'x', 251);
  memcpy(name + 251, ".elf", 5);
  snprintf(path, sizeof(path), "%s/%s", directory, name);
  TEST_ASSERT_EQ_INT(0, write_marker(path));
  TEST_ASSERT_TRUE(onion_payload_remove(path));
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && errno == ENOENT);
  TEST_ASSERT_EQ_INT(0, rmdir(directory));
  return 0;
}

static int test_remove_rejects_invalid_targets(void) {
  char original[256], path[300], marker[320];
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".elf", "ELF", 3,
                                                  original, sizeof(original)));
  const char *suffixes[] = {".link.elf", ".directory.elf", ".missing.elf", ".txt"};
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
    snprintf(path, sizeof(path), "%s%s", original, suffixes[i]);
    snprintf(marker, sizeof(marker), "%s.auto_start", path);
    if (i == 0)
      TEST_ASSERT_EQ_INT(0, symlink(original, path));
    if (i == 1)
      TEST_ASSERT_EQ_INT(0, mkdir(path, 0700));
    if (i == 3)
      TEST_ASSERT_EQ_INT(0, write_marker(path));
    TEST_ASSERT_EQ_INT(0, write_marker(marker));
    TEST_ASSERT_TRUE(!onion_payload_remove(path));
    TEST_ASSERT_EQ_INT(0, access(original, F_OK));
    TEST_ASSERT_EQ_INT(0, access(marker, F_OK));
    if (i != 2)
      TEST_ASSERT_EQ_INT(0, access(path, F_OK));
    unlink(marker);
    if (i == 1)
      rmdir(path);
    else
      unlink(path);
  }
  TEST_ASSERT_TRUE(!onion_payload_remove(NULL));
  TEST_ASSERT_TRUE(!onion_payload_remove("relative.elf"));
  TEST_ASSERT_TRUE(!onion_payload_remove("/tmp/.elf"));
  snprintf(path, sizeof(path), "%s/../outside.elf", original);
  TEST_ASSERT_TRUE(!onion_payload_remove(path));
  TEST_ASSERT_EQ_INT(EINVAL, errno);
  unlink(original);
  return 0;
}

static int test_remove_preserves_running_payload(void) {
  char path[256], marker[300], config[300], pid_path[256];
  char identity[ONION_PAYLOAD_IDENTITY_SIZE];
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".elf", "ELF", 3,
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, write_marker(config));
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(path, identity, sizeof(identity)));
  onion_payload_pid_path(pid_path, sizeof(pid_path), identity);
  onion_payload_write_pid_file(pid_path, 6262);
  onion_test_live_pid(6262);

  TEST_ASSERT_TRUE(onion_payload_path_running(path));
  TEST_ASSERT_TRUE(!onion_payload_remove(path));
  TEST_ASSERT_EQ_INT(EBUSY, errno);
  TEST_ASSERT_EQ_INT(0, access(path, F_OK));
  TEST_ASSERT_EQ_INT(0, access(marker, F_OK));
  TEST_ASSERT_EQ_INT(0, access(config, F_OK));
  TEST_ASSERT_EQ_INT(6262, onion_payload_read_pid_file(pid_path));
  onion_test_elfldr_reset();
  TEST_ASSERT_TRUE(onion_payload_remove(path));
  return 0;
}

static int test_remove_after_default_launch(void) {
  char path[256], marker[300], config[300], legacy_pid[256], hashed_pid[256];
  char key[64], identity[ONION_PAYLOAD_IDENTITY_SIZE];
  const unsigned char elf[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0};
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".elf", elf, sizeof(elf),
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, write_marker(config));
  TEST_ASSERT_TRUE(onion_payload_elf_key_from_name(path, key, sizeof(key)));
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(path, identity, sizeof(identity)));
  onion_payload_pid_path(legacy_pid, sizeof(legacy_pid), key);
  onion_payload_pid_path(hashed_pid, sizeof(hashed_pid), identity);

  onion_test_elfldr_reset();
  onion_test_elfldr_configure(true, 5151);
  TEST_ASSERT_TRUE(onion_payload_load(path, NULL));
  onion_payload_write_pid_file(hashed_pid, 4242);
  onion_test_live_pid(5151);
  TEST_ASSERT_TRUE(onion_payload_path_running(path));
  TEST_ASSERT_TRUE(!onion_payload_remove(path));
  TEST_ASSERT_EQ_INT(EBUSY, errno);
  TEST_ASSERT_EQ_INT(0, access(path, F_OK));
  TEST_ASSERT_EQ_INT(0, access(marker, F_OK));
  TEST_ASSERT_EQ_INT(0, access(config, F_OK));
  TEST_ASSERT_EQ_INT(5151, onion_payload_read_pid_file(legacy_pid));
  TEST_ASSERT_EQ_INT(4242, onion_payload_read_pid_file(hashed_pid));

  onion_test_elfldr_reset();
  TEST_ASSERT_TRUE(!onion_payload_path_running(path));
  TEST_ASSERT_EQ_INT(5151, onion_payload_read_pid_file(legacy_pid));
  TEST_ASSERT_EQ_INT(4242, onion_payload_read_pid_file(hashed_pid));
  TEST_ASSERT_TRUE(onion_payload_remove(path));
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && access(marker, F_OK) != 0 &&
                   access(config, F_OK) != 0 && access(legacy_pid, F_OK) != 0 &&
                   access(hashed_pid, F_OK) != 0);
  return 0;
}

static int test_remove_reports_sidecar_failure(void) {
  char path[256], marker[300], config[300];
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".elf", "ELF", 3,
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, mkdir(config, 0700));
  TEST_ASSERT_TRUE(!onion_payload_remove(path));
  TEST_ASSERT_TRUE(errno != 0 && errno != ENOENT);
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && access(marker, F_OK) != 0);
  struct stat st;
  TEST_ASSERT_TRUE(lstat(config, &st) == 0 && S_ISDIR(st.st_mode));
  rmdir(config);
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
  failures += onion_test_run("payload.remove_stopped", test_remove_stopped_payload);
  failures += onion_test_run("payload.remove_long_basename", test_remove_long_basename);
  failures += onion_test_run("payload.remove_invalid_targets", test_remove_rejects_invalid_targets);
  failures += onion_test_run("payload.remove_running", test_remove_preserves_running_payload);
  failures += onion_test_run("payload.remove_after_default_launch", test_remove_after_default_launch);
  failures += onion_test_run("payload.remove_sidecar_failure", test_remove_reports_sidecar_failure);
  return failures;
}
