/* Host tests for process-name conflict strategies. */
#include "test_harness.h"

#include <onion/conflict.h>
#include <onion/proc_query.h>

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

static int assert_detect(const char *live, const char *family) {
  live_reset();
  if (live)
    live_add(live);
  const char *got = onion_conflict_detect_with(fake_find_pid);
  if (!family) {
    TEST_ASSERT_TRUE(got == NULL);
    return 0;
  }
  TEST_ASSERT_STREQ(family, got);
  return 0;
}

static int test_builtin_hits(void) {
  /* Live names come from kinfo_proc, so they can never be longer than
   * ki_comm (19) or ki_tdname (16). The >19 entries below therefore test the
   * candidate generator, not a state the kernel can actually report; the
   * <=19 ones are what fires on hardware. Both matter: the first catches a
   * dropped candidate, the second catches a broken truncation. */
  static const struct {
    const char *live;
    const char *family;
  } kCases[] = {
      {"etaHEN Utility Daemon", "etaHEN"},        /* generator only */
      {"etaHEN Critical services", "etaHEN"},     /* generator only */
      {"etaHEN Utility Daemon.elf", "etaHEN"},    /* generator only */
      {"etaHEN Utility Daem", "etaHEN"},          /* ki_comm */
      {"etaHEN Critical ser", "etaHEN"},          /* ki_comm */
      {"etaHEN Utility D", "etaHEN"},             /* ki_tdname */
      {"etaHEN Critical ", "etaHEN"},             /* ki_tdname, trailing space */
      {"Yoncore.elf", "Yoncore"},
      {"Yoncore", "Yoncore"},
      {"kylin-core.elf", "kylin-core"},
      {"kylin-core", "kylin-core"},
      {"wmdw-jwm.elf", "wmdw-jwm"},
      {"wmdw-jwm", "wmdw-jwm"},
      {"CheatRunner.elf", "CheatRunner"},
      {"CheatRunner", "CheatRunner"},
      {"daemon.elf", NULL},
      {"util.elf", NULL},
      {"kstuff.elf", NULL},
      {"etaHEN.elf", NULL},
  };

  for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
    const int rc = assert_detect(kCases[i].live, kCases[i].family);
    if (rc != 0)
      return rc;
  }
  return 0;
}

static int test_no_live_processes(void) {
  live_reset();
  TEST_ASSERT_TRUE(onion_conflict_detect_with(fake_find_pid) == NULL);
  TEST_ASSERT_TRUE(onion_conflict_detect_with(NULL) == NULL);
  TEST_ASSERT_TRUE(onion_conflict_detect() == NULL);
  return 0;
}

static int test_elf_suffix_ignored(void) {
  static const char *with_elf[] = {"foo.elf", NULL};
  static const char *without_elf[] = {"bar", NULL};
  static const OnionConflictStrategy table[] = {
      {"with", with_elf},
      {"without", without_elf},
  };

  live_reset();
  live_add("foo");
  TEST_ASSERT_STREQ("with", onion_conflict_scan(table, 2, fake_find_pid));

  live_reset();
  live_add("foo.elf");
  TEST_ASSERT_STREQ("with", onion_conflict_scan(table, 2, fake_find_pid));

  live_reset();
  live_add("bar.elf");
  TEST_ASSERT_STREQ("without", onion_conflict_scan(table, 2, fake_find_pid));

  live_reset();
  live_add("bar");
  TEST_ASSERT_STREQ("without", onion_conflict_scan(table, 2, fake_find_pid));

  live_reset();
  live_add("foo.elf.bak");
  live_add("bar.elfx");
  TEST_ASSERT_TRUE(onion_conflict_scan(table, 2, fake_find_pid) == NULL);
  return 0;
}

static int test_truncation_lengths(void) {
  static const char *names[] = {"etaHEN Utility Daemon", NULL};
  static const OnionConflictStrategy table[] = {{"etaHEN", names}};

  TEST_ASSERT_TRUE(strlen("etaHEN Utility Daemon") > ONION_PROC_KI_COMM_LEN);
  TEST_ASSERT_TRUE(ONION_PROC_KI_TDNAM_LEN < ONION_PROC_KI_COMM_LEN);

  /* ki_comm keeps COMMLEN=19 chars. */
  live_reset();
  live_add("etaHEN Utility Daem");
  TEST_ASSERT_STREQ("etaHEN", onion_conflict_scan(table, 1, fake_find_pid));

  /* ki_tdname keeps only TDNAMLEN=16; a thread-name-only hit must still be
   * detected, otherwise the whole family is missed. */
  live_reset();
  live_add("etaHEN Utility D");
  TEST_ASSERT_STREQ("etaHEN", onion_conflict_scan(table, 1, fake_find_pid));

  /* A shorter truncation than either field is not a hit. */
  live_reset();
  live_add("etaHEN Utilit");
  TEST_ASSERT_TRUE(onion_conflict_scan(table, 1, fake_find_pid) == NULL);
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

static int expect_family_names(const char *family, const char *const *names) {
  size_t count = 0;
  const OnionConflictStrategy *table = onion_conflict_strategies(&count);
  TEST_ASSERT_TRUE(table != NULL);
  TEST_ASSERT_TRUE(count >= 5);

  const OnionConflictStrategy *found = NULL;
  for (size_t i = 0; i < count; ++i) {
    if (table[i].family && strcmp(table[i].family, family) == 0) {
      found = &table[i];
      break;
    }
  }
  TEST_ASSERT_TRUE(found != NULL);
  TEST_ASSERT_TRUE(found->proc_names != NULL);

  for (size_t i = 0; names[i]; ++i) {
    TEST_ASSERT_TRUE(found->proc_names[i] != NULL);
    TEST_ASSERT_STREQ(names[i], found->proc_names[i]);
  }
  TEST_ASSERT_TRUE(found->proc_names[0] != NULL);
  size_t got = 0;
  while (found->proc_names[got])
    ++got;
  size_t want = 0;
  while (names[want])
    ++want;
  TEST_ASSERT_TRUE(got == want);
  return 0;
}

static int test_builtin_table(void) {
  static const char *etahen[] = {
      "etaHEN Utility Daemon",
      "etaHEN Critical services",
      NULL,
  };
  static const char *yoncore[] = {"Yoncore.elf", NULL};
  static const char *kylin[] = {"kylin-core.elf", NULL};
  static const char *wmdw[] = {"wmdw-jwm.elf", NULL};
  static const char *cheat[] = {"CheatRunner.elf", NULL};

  static const struct {
    const char *family;
    const char *const *names;
  } kFamilies[] = {
      {"etaHEN", etahen},
      {"Yoncore", yoncore},
      {"kylin-core", kylin},
      {"wmdw-jwm", wmdw},
      {"CheatRunner", cheat},
  };

  for (size_t i = 0; i < sizeof(kFamilies) / sizeof(kFamilies[0]); ++i) {
    const int rc =
        expect_family_names(kFamilies[i].family, kFamilies[i].names);
    if (rc != 0)
      return rc;
  }
  return 0;
}

int test_conflict_suite(void) {
  int failures = 0;
  failures += onion_test_run("conflict_builtin_hits", test_builtin_hits);
  failures += onion_test_run("conflict_no_live_processes",
                             test_no_live_processes);
  failures += onion_test_run("conflict_elf_suffix_ignored",
                             test_elf_suffix_ignored);
  failures += onion_test_run("conflict_truncation_lengths",
                             test_truncation_lengths);
  failures += onion_test_run("conflict_scan_first_family_wins",
                             test_scan_first_family_wins);
  failures += onion_test_run("conflict_builtin_table", test_builtin_table);
  return failures;
}
