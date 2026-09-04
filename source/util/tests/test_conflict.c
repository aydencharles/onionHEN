/* Host tests for process-name conflict strategies. */
#include "test_harness.h"

#include <onion/conflict.h>

#include <string.h>

static const char *g_live[8];
static size_t g_live_count;

static void live_reset(void) { g_live_count = 0; }

static void live_add(const char *name) {
  if (g_live_count < sizeof(g_live) / sizeof(g_live[0]))
    g_live[g_live_count++] = name;
}

static pid_t fake_find_pid(const char *name) {
  if (!name)
    return -1;
  for (size_t i = 0; i < g_live_count; ++i) {
    if (strcmp(g_live[i], name) == 0)
      return (pid_t)1000 + (pid_t)i;
  }
  return -1;
}

static int test_etahen_hit(void) {
  live_reset();
  live_add("etaHEN.elf");
  TEST_ASSERT_STREQ("etaHEN", onion_conflict_detect_with(fake_find_pid));
  return 0;
}

static int test_generic_daemon_is_not_etahen(void) {
  live_reset();
  live_add("daemon.elf");
  live_add("util.elf");
  live_add("kstuff.elf");
  TEST_ASSERT_TRUE(onion_conflict_detect_with(fake_find_pid) == NULL);
  return 0;
}

static int test_no_live_processes(void) {
  live_reset();
  TEST_ASSERT_TRUE(onion_conflict_detect_with(fake_find_pid) == NULL);
  TEST_ASSERT_TRUE(onion_conflict_detect_with(NULL) == NULL);
  TEST_ASSERT_TRUE(onion_conflict_detect() == NULL);
  return 0;
}

static int test_scan_first_family_wins(void) {
  static const char *alpha_names[] = {"alpha.elf", NULL};
  static const char *beta_names[] = {"beta.elf", NULL};
  static const OnionConflictStrategy table[] = {
      {"alpha", alpha_names},
      {"beta", beta_names},
  };

  live_reset();
  live_add("beta.elf");
  live_add("alpha.elf");
  TEST_ASSERT_STREQ("alpha", onion_conflict_scan(table, 2, fake_find_pid));

  live_reset();
  live_add("beta.elf");
  TEST_ASSERT_STREQ("beta", onion_conflict_scan(table, 2, fake_find_pid));
  return 0;
}

static int test_builtin_table_has_etahen(void) {
  size_t count = 0;
  const OnionConflictStrategy *table = onion_conflict_strategies(&count);
  TEST_ASSERT_TRUE(table != NULL);
  TEST_ASSERT_TRUE(count >= 1);

  int found = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!table[i].family || strcmp(table[i].family, "etaHEN") != 0)
      continue;
    found = 1;
    TEST_ASSERT_TRUE(table[i].proc_names != NULL);
    TEST_ASSERT_STREQ("etaHEN.elf", table[i].proc_names[0]);
    TEST_ASSERT_TRUE(table[i].proc_names[1] == NULL);
  }
  TEST_ASSERT_TRUE(found);
  return 0;
}

int test_conflict_suite(void) {
  int failures = 0;
  failures += onion_test_run("conflict_etahen_hit", test_etahen_hit);
  failures += onion_test_run("conflict_generic_names_ignored",
                             test_generic_daemon_is_not_etahen);
  failures += onion_test_run("conflict_no_live_processes",
                             test_no_live_processes);
  failures += onion_test_run("conflict_scan_first_family_wins",
                             test_scan_first_family_wins);
  failures += onion_test_run("conflict_builtin_table_etahen",
                             test_builtin_table_has_etahen);
  return failures;
}
