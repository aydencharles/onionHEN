#include "test_harness.h"
#include "test_support.h"
#include <onion/payload_config.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_config_roundtrip(void) {
  char path[256], json_path[272], marker[272];
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(".elf", "ELF", path, sizeof(path)));
  snprintf(json_path, sizeof(json_path), "%s.json", path);
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  OnionPayloadConfig config = {123, 45};
  TEST_ASSERT_TRUE(onion_payload_config_load(path, &config));
  TEST_ASSERT_EQ_INT(0, config.priority);
  TEST_ASSERT_EQ_INT(0, config.delay_seconds);
  config = (OnionPayloadConfig){999, 300};
  TEST_ASSERT_TRUE(onion_payload_config_save(path, &config));
  config = (OnionPayloadConfig){0};
  TEST_ASSERT_TRUE(onion_payload_config_load(path, &config));
  TEST_ASSERT_EQ_INT(999, config.priority);
  TEST_ASSERT_EQ_INT(300, config.delay_seconds);

  FILE *file = fopen(marker, "wb");
  TEST_ASSERT_TRUE(file != NULL);
  fclose(file);
  config.priority = 10;
  TEST_ASSERT_TRUE(onion_payload_config_save(path, &config));
  TEST_ASSERT_EQ_INT(0, access(marker, F_OK));
  unlink(marker);
  TEST_ASSERT_TRUE(onion_payload_config_load(path, &config));
  TEST_ASSERT_EQ_INT(10, config.priority);
  TEST_ASSERT_EQ_INT(300, config.delay_seconds);

  config.priority = 1000;
  TEST_ASSERT_TRUE(!onion_payload_config_save(path, &config));
  TEST_ASSERT_TRUE(onion_payload_config_load(path, &config));
  TEST_ASSERT_EQ_INT(10, config.priority);
  config.delay_seconds = -1;
  TEST_ASSERT_TRUE(!onion_payload_config_save(path, &config));
  config.delay_seconds = 301;
  TEST_ASSERT_TRUE(!onion_payload_config_save(path, &config));
  config.delay_seconds = 0;
  TEST_ASSERT_TRUE(!onion_payload_config_save("/missing/onion-payload-test/a.elf", &config));
  unlink(json_path);
  // A rename failure must not replace an existing destination.
  TEST_ASSERT_EQ_INT(0, mkdir(json_path, 0700));
  TEST_ASSERT_TRUE(!onion_payload_config_save(path, &config));
  TEST_ASSERT_EQ_INT(0, rmdir(json_path));
  unlink(path);
  return 0;
}

static int test_config_invalid(void) {
  static const char *const invalid[] = {
      "", "{", "[]", "null", "{}", "{\"version\":2}",
      "{\"version\":1,\"priority\":-1}",
      "{\"version\":1,\"priority\":1000}",
      "{\"version\":1,\"priority\":1.5}",
      "{\"version\":1,\"priority\":\"1\"}",
      "{\"version\":1,\"priority\":true}",
      "{\"version\":1,\"priority\":10,\"delay_seconds\":301}",
      "{\"version\":1,\"delay_seconds\":1e100}",
      "{\"version\":1,\"delay_seconds\":null}",
      "{\"version\":1} trailing", "{\"version\":1} {}",
  };
  char path[256], json_path[272];
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(".elf", "ELF", path, sizeof(path)));
  snprintf(json_path, sizeof(json_path), "%s.json", path);
  OnionPayloadConfig config;
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    FILE *file = fopen(json_path, "wb");
    TEST_ASSERT_TRUE(file != NULL);
    fputs(invalid[i], file);
    fclose(file);
    config = (OnionPayloadConfig){10, 10};
    TEST_ASSERT_TRUE(!onion_payload_config_load(path, &config));
    TEST_ASSERT_EQ_INT(0, config.priority);
    TEST_ASSERT_EQ_INT(0, config.delay_seconds);
  }
  FILE *file = fopen(json_path, "wb");
  TEST_ASSERT_TRUE(file != NULL);
  fputs("{\"version\":1,\"priority\":42}\n", file);
  fclose(file);
  TEST_ASSERT_TRUE(onion_payload_config_load(path, &config));
  TEST_ASSERT_EQ_INT(42, config.priority);
  TEST_ASSERT_EQ_INT(0, config.delay_seconds);
  file = fopen(json_path, "wb");
  TEST_ASSERT_TRUE(file != NULL);
  for (size_t i = 0; i < 4096; ++i)
    fputc(' ', file);
  fclose(file);
  TEST_ASSERT_TRUE(!onion_payload_config_load(path, &config));
  TEST_ASSERT_TRUE(!onion_payload_config_load(NULL, &config));
  TEST_ASSERT_TRUE(!onion_payload_config_load(path, NULL));
  unlink(json_path);
  unlink(path);
  return 0;
}

static int test_payload_order_and_aliases(void) {
  const char *primary = "/data/OnionHEN/payloads/test.elf";
  const char *sandbox = "/user/data/OnionHEN/payloads/test.elf";
  const char *usb = "/mnt/usb0/OnionHEN/payloads/test.elf";
  char a[ONION_PAYLOAD_IDENTITY_SIZE], b[ONION_PAYLOAD_IDENTITY_SIZE];
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(primary, a, sizeof(a)));
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(sandbox, b, sizeof(b)));
  TEST_ASSERT_STREQ(a, b);
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(usb, a, sizeof(a)));
  TEST_ASSERT_TRUE(onion_payload_identity_from_path(
      "/usb0/OnionHEN/payloads/test.elf", b, sizeof(b)));
  TEST_ASSERT_STREQ(a, b);
  TEST_ASSERT_TRUE(onion_payload_compare(100, usb, 0, primary) < 0);
  TEST_ASSERT_TRUE(onion_payload_compare(0, usb, 100, primary) > 0);
  TEST_ASSERT_TRUE(onion_payload_compare(0, "/data/z.elf", 0, "/mnt/a.elf") > 0);
  TEST_ASSERT_TRUE(onion_payload_compare(0, primary, 0, usb) < 0);
  TEST_ASSERT_EQ_INT(0, onion_payload_compare(0, primary, 0, sandbox));
  char path[ONION_PAYLOAD_PATH_SIZE];
  TEST_ASSERT_TRUE(onion_payload_canonical_path("/usb10/a.elf", path, sizeof(path)));
  TEST_ASSERT_STREQ("/usb10/a.elf", path);
  TEST_ASSERT_TRUE(onion_payload_canonical_path("/userdata/a.elf", path, sizeof(path)));
  TEST_ASSERT_STREQ("/userdata/a.elf", path);
  TEST_ASSERT_TRUE(!onion_payload_canonical_path(primary, path, 4));
  return 0;
}

int test_payload_config_suite(void) {
  int failures = 0;
  failures += onion_test_run("payload.config_roundtrip", test_config_roundtrip);
  failures += onion_test_run("payload.config_invalid", test_config_invalid);
  failures += onion_test_run("payload.order_and_aliases", test_payload_order_and_aliases);
  return failures;
}
