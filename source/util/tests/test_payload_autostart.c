#include "test_harness.h"
#include <onion/payload.h>
#include <onion/payload_config.h>
#include <onion/platform.h>
#include <elfldr_remote.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char fixture[256];
static bool loader_available;
static bool interrupt_wait;
static unsigned int waits[16];
static size_t wait_count, launch_count, event_count;
static char events[32][64];
static int failures_notified, invalid_launches;

static void local_path(const char *path, char *out, size_t size) {
  const char *name = strrchr(path, '/');
  snprintf(out, size, "%s/%s", fixture, name ? name + 1 : path);
}

static DIR *fixture_opendir(const char *path) {
  if (strcmp(path, "/data/OnionHEN/payloads") == 0 ||
      strcmp(path, "/user/data/OnionHEN/payloads") == 0)
    return opendir(fixture);
  return NULL;
}

static bool fixture_exists(const char *path) {
  char local[512];
  local_path(path, local, sizeof(local));
  return access(local, F_OK) == 0;
}

static bool fixture_config(const char *path, OnionPayloadConfig *config) {
  char local[512];
  local_path(path, local, sizeof(local));
  return onion_payload_config_load(local, config);
}

static bool fixture_available(void) { return loader_available; }

static bool fixture_running(const char *key) {
  char running[ONION_PAYLOAD_IDENTITY_SIZE];
  onion_payload_identity_from_path("/data/OnionHEN/payloads/running.elf",
                                   running, sizeof(running));
  return strcmp(key, running) == 0;
}

static unsigned int fixture_sleep(unsigned int seconds) {
  if (wait_count < 16)
    waits[wait_count] = seconds;
  ++wait_count;
  if (event_count < 32)
    snprintf(events[event_count++], sizeof(events[0]), "wait:%u", seconds);
  if (interrupt_wait) {
    interrupt_wait = false;
    return 2;
  }
  return 0;
}

static bool fixture_launch(const char *path, const char *filename, const char *key) {
  char expected[512], identity[ONION_PAYLOAD_IDENTITY_SIZE];
  snprintf(expected, sizeof(expected), "/data/OnionHEN/payloads/%s", filename);
  onion_payload_identity_from_path(expected, identity, sizeof(identity));
  if (strcmp(expected, path) != 0 || !key || strcmp(identity, key) != 0)
    ++invalid_launches;
  ++launch_count;
  if (event_count < 32)
    snprintf(events[event_count++], sizeof(events[0]), "load:%s", filename);
  return strcmp(filename, "a.elf") != 0;
}

static void fixture_notify(const char *key, ...) {
  if (strcmp(key, "notify.payload.load_failed_path") == 0)
    ++failures_notified;
}

/* Run the production scanner and loop against temporary files and a fake clock. */
#define opendir fixture_opendir
#define if_exists fixture_exists
#define onion_payload_config_load fixture_config
#define elfldr_remote_onion_available fixture_available
#define onion_payload_running fixture_running
#define onion_payload_load_with_key fixture_launch
#define bootstrap_notify fixture_notify
#define sleep fixture_sleep
#include "../../bootstrapper/source/payload_autostart.c"
#undef opendir
#undef if_exists
#undef onion_payload_config_load
#undef elfldr_remote_onion_available
#undef onion_payload_running
#undef onion_payload_load_with_key
#undef bootstrap_notify
#undef sleep

static int add_payload(const char *name, int priority, int delay, bool automatic) {
  char path[512], marker[528];
  local_path(name, path, sizeof(path));
  FILE *file = fopen(path, "wb");
  TEST_ASSERT_TRUE(file != NULL);
  fputs("\177ELF", file);
  fclose(file);
  OnionPayloadConfig config = {priority, delay};
  TEST_ASSERT_TRUE(onion_payload_config_save(path, &config));
  if (automatic) {
    snprintf(marker, sizeof(marker), "%s.auto_start", path);
    file = fopen(marker, "wb");
    TEST_ASSERT_TRUE(file != NULL);
    fclose(file);
  }
  return 0;
}

static int test_autostart_schedule(void) {
  snprintf(fixture, sizeof(fixture), "/tmp/onion-autostart-XXXXXX");
  TEST_ASSERT_TRUE(mkdtemp(fixture) != NULL);
  TEST_ASSERT_EQ_INT(0, add_payload("c.elf", 0, 0, true));
  TEST_ASSERT_EQ_INT(0, add_payload("b.elf", 100, 2, true));
  TEST_ASSERT_EQ_INT(0, add_payload("a.elf", 100, 5, true));
  TEST_ASSERT_EQ_INT(0, add_payload("disabled.elf", 999, 300, false));
  TEST_ASSERT_EQ_INT(0, add_payload("elfldr.elf", 999, 300, true));
  TEST_ASSERT_EQ_INT(0, add_payload("running.elf", 999, 300, true));

  loader_available = false;
  wait_count = launch_count = event_count = 0;
  failures_notified = invalid_launches = 0;
  bootstrap_payload_autostart();
  TEST_ASSERT_EQ_INT(0, (int)event_count);

  loader_available = true;
  interrupt_wait = true;
  bootstrap_payload_autostart();
  TEST_ASSERT_EQ_INT(3, (int)launch_count);
  TEST_ASSERT_EQ_INT(3, (int)wait_count);
  TEST_ASSERT_EQ_INT(5, (int)waits[0]);
  TEST_ASSERT_EQ_INT(2, (int)waits[1]);
  TEST_ASSERT_EQ_INT(2, (int)waits[2]);
  TEST_ASSERT_EQ_INT(6, (int)event_count);
  TEST_ASSERT_STREQ("wait:5", events[0]);
  TEST_ASSERT_STREQ("wait:2", events[1]);
  TEST_ASSERT_STREQ("load:a.elf", events[2]);
  TEST_ASSERT_STREQ("wait:2", events[3]);
  TEST_ASSERT_STREQ("load:b.elf", events[4]);
  TEST_ASSERT_STREQ("load:c.elf", events[5]);
  TEST_ASSERT_EQ_INT(1, failures_notified);
  TEST_ASSERT_EQ_INT(0, invalid_launches);

  DIR *dir = opendir(fixture);
  TEST_ASSERT_TRUE(dir != NULL);
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    char path[512];
    local_path(entry->d_name, path, sizeof(path));
    unlink(path);
  }
  closedir(dir);
  TEST_ASSERT_EQ_INT(0, rmdir(fixture));
  return 0;
}

int test_payload_autostart_suite(void) {
  return onion_test_run("payload.autostart_schedule", test_autostart_schedule);
}
