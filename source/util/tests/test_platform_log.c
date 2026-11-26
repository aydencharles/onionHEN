/* Host tests for libonion_platform logging: sinks, levels, rotation. */
#include "test_harness.h"

#include <onion/log.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- helpers -------------------------------------------------------------- */

static char g_path[64];
static char g_crash_path[72];

static void cleanup_log_files(void) {
  char rotated[80];
  unlink(g_path);
  for (unsigned i = 1; i <= ONION_LOG_DEFAULT_ROTATE_COUNT + 1; ++i) {
    snprintf(rotated, sizeof(rotated), "%s.%u", g_path, i);
    unlink(rotated);
  }
  /* Clean up artifacts produced by the previous one-generation policy. */
  snprintf(rotated, sizeof(rotated), "%s.old", g_path);
  unlink(rotated);
  /* Sidecar for the cross-process rotation lock. */
  snprintf(rotated, sizeof(rotated), "%s.lock", g_path);
  unlink(rotated);
  unlink(g_crash_path);
  /* Previous crash generation kept by the crash-sink cap. */
  snprintf(rotated, sizeof(rotated), "%s.1", g_crash_path);
  unlink(rotated);
}

static void begin(const char *tag) {
  snprintf(g_path, sizeof(g_path), "/tmp/onion-log-test-%d", (int)getpid());
  snprintf(g_crash_path, sizeof(g_crash_path), "%s-crash", g_path);
  cleanup_log_files();
  onion_log_set_max_bytes(0); /* restore default cap */
  onion_log_set_level(ONION_LOG_TRACE);
  onion_log_configure(tag, g_path);
}

static void end(void) {
  onion_log_configure_crash(NULL);
  onion_log_configure("OnionHEN", NULL);
  cleanup_log_files();
  onion_log_set_level(ONION_LOG_INFO);
  onion_log_set_max_bytes(0);
}

static size_t read_file(const char *path, char *out, size_t out_size) {
  FILE *f = fopen(path, "r");
  if (!f) {
    out[0] = '\0';
    return 0;
  }
  size_t n = fread(out, 1, out_size - 1, f);
  fclose(f);
  out[n] = '\0';
  return n;
}

static long file_size(const char *path) {
  struct stat st;
  return (stat(path, &st) == 0) ? (long)st.st_size : -1;
}

/* --- tests ---------------------------------------------------------------- */

static int test_log_file_sink(void) {
  char buf[512];
  begin("HostTest");
  LOG_INFO("hello %d", 42);

  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "hello 42") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "[HostTest]") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "info:") != NULL);
  TEST_ASSERT_TRUE(strchr(buf, '\n') != NULL);
  end();
  return 0;
}

/* A daemon restart must append to the previous run instead of truncating it. */
static int test_configure_appends_existing_log(void) {
  char buf[512];
  snprintf(g_path, sizeof(g_path), "/tmp/onion-log-test-%d", (int)getpid());
  cleanup_log_files();

  FILE *stale = fopen(g_path, "w");
  TEST_ASSERT_TRUE(stale != NULL);
  fputs("previous-run\n", stale);
  fclose(stale);

  onion_log_set_level(ONION_LOG_INFO);
  onion_log_configure("Append", g_path);
  LOG_INFO("current-run");

  TEST_ASSERT_TRUE(file_size(g_path) >= 0);
  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "previous-run") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "current-run") != NULL);
  end();
  return 0;
}

static int test_log_configure_tag_only(void) {
  onion_log_configure("TagOnly", NULL);
  LOG_INFO("no file sink configured"); /* must not crash */
  onion_log_configure("OnionHEN", NULL);
  return 0;
}

/* The runtime gate must drop records below the threshold. */
static int test_runtime_level_filters(void) {
  char buf[1024];
  begin("Lvl");

  onion_log_set_level(ONION_LOG_WARN);
  onion_log_write(ONION_LOG_ERROR, "keep-error");
  onion_log_write(ONION_LOG_WARN, "keep-warn");
  onion_log_write(ONION_LOG_INFO, "drop-info");
  onion_log_write(ONION_LOG_DEBUG, "drop-debug");
  LOG_INFO("drop-legacy-info");

  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "keep-error") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "keep-warn") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "drop-info") == NULL);
  TEST_ASSERT_TRUE(strstr(buf, "drop-debug") == NULL);
  TEST_ASSERT_TRUE(strstr(buf, "drop-legacy-info") == NULL);
  end();
  return 0;
}

static int test_level_off_silences_everything(void) {
  char buf[512];
  begin("Off");
  onion_log_set_level(ONION_LOG_OFF);
  onion_log_write(ONION_LOG_ERROR, "must-not-appear");
  LOG_INFO("must-not-appear-either");

  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "must-not-appear") == NULL);
  end();
  return 0;
}

/* An unbounded log on a console the user cannot clean out is a real failure
 * mode, so the cap must actually rotate and keep a numbered backup. */
static int test_rotation_caps_size_and_keeps_backup(void) {
  char rotated_path[80];
  char buf[2048];

  begin("Rot");
  onion_log_set_max_bytes(512);
  for (int i = 0; i < 60; i++) {
    onion_log_write(ONION_LOG_ERROR, "record-%02d-padding-padding-padding", i);
  }

  const long live = file_size(g_path);
  TEST_ASSERT_TRUE(live >= 0);
  TEST_ASSERT_TRUE(live <= 512);

  snprintf(rotated_path, sizeof(rotated_path), "%s.1", g_path);
  TEST_ASSERT_TRUE(file_size(rotated_path) > 0);

  /* Newest records live in the current file. */
  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "record-59") != NULL);
  end();
  return 0;
}

/* Rotation keeps .1 as newest through .3 as the oldest retained backup. */
static int test_rotation_keeps_three_backups(void) {
  char rotated_path[80];
  char buf[2048];

  begin("Rot2");
  onion_log_set_max_bytes(256);
  for (int i = 0; i < 200; i++) {
    onion_log_write(ONION_LOG_ERROR, "spam-%03d-aaaaaaaaaaaaaaaaaaaa", i);
  }
  for (unsigned i = 1; i <= ONION_LOG_DEFAULT_ROTATE_COUNT; ++i) {
    snprintf(rotated_path, sizeof(rotated_path), "%s.%u", g_path, i);
    TEST_ASSERT_TRUE(file_size(rotated_path) > 0);
  }
  snprintf(rotated_path, sizeof(rotated_path), "%s.%u", g_path,
           ONION_LOG_DEFAULT_ROTATE_COUNT + 1);
  TEST_ASSERT_TRUE(file_size(rotated_path) < 0);

  snprintf(rotated_path, sizeof(rotated_path), "%s.1", g_path);
  read_file(rotated_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "spam-000") == NULL); /* oldest rotated away */
  end();
  return 0;
}

/* Rotation must use the file's real size, including bytes appended outside
 * this logger instance after its descriptor was opened. */
static int test_rotation_observes_external_append(void) {
  char rotated_path[80];
  char buf[1024];
  char padding[480];

  begin("External");
  onion_log_set_max_bytes(512);
  memset(padding, 'p', sizeof(padding));
  FILE *external = fopen(g_path, "ab");
  TEST_ASSERT_TRUE(external != NULL);
  TEST_ASSERT_EQ_INT(sizeof(padding), fwrite(padding, 1, sizeof(padding), external));
  fclose(external);

  onion_log_write(ONION_LOG_ERROR, "after-external-append");

  snprintf(rotated_path, sizeof(rotated_path), "%s.1", g_path);
  TEST_ASSERT_TRUE(file_size(rotated_path) >= (long)sizeof(padding));
  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "after-external-append") != NULL);
  end();
  return 0;
}

/* Exactly one trailing newline regardless of what the caller passed. */
static int test_single_trailing_newline(void) {
  char buf[512];
  begin("NL");
  onion_log_write(ONION_LOG_ERROR, "trailing-newline-supplied\n");

  const size_t n = read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n >= 2);
  TEST_ASSERT_EQ_INT('\n', buf[n - 1]);
  TEST_ASSERT_TRUE(buf[n - 2] != '\n');
  end();
  return 0;
}

/* Over-long records must truncate, stay terminated, and still end in \n. */
static int test_oversized_record_truncates(void) {
  static char big[0x2000];
  char buf[0x2000];

  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';

  begin("Big");
  onion_log_set_max_bytes(1024u * 1024u);
  onion_log_write(ONION_LOG_ERROR, "%s", big);

  const size_t n = read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQ_INT('\n', buf[n - 1]);
  TEST_ASSERT_TRUE(n < sizeof(big)); /* truncated, not overflowed */
  end();
  return 0;
}

static int test_level_name_roundtrip(void) {
  onion_log_level lvl;

  TEST_ASSERT_TRUE(onion_log_level_from_name("error", &lvl));
  TEST_ASSERT_EQ_INT(ONION_LOG_ERROR, lvl);
  TEST_ASSERT_TRUE(onion_log_level_from_name("TRACE", &lvl)); /* case-insensitive */
  TEST_ASSERT_EQ_INT(ONION_LOG_TRACE, lvl);
  TEST_ASSERT_TRUE(onion_log_level_from_name("Off", &lvl));
  TEST_ASSERT_EQ_INT(ONION_LOG_OFF, lvl);

  TEST_ASSERT_TRUE(!onion_log_level_from_name("verbose", &lvl));
  TEST_ASSERT_TRUE(!onion_log_level_from_name("inf", &lvl)); /* prefix != match */
  TEST_ASSERT_TRUE(!onion_log_level_from_name("infoo", &lvl));
  TEST_ASSERT_TRUE(!onion_log_level_from_name(NULL, &lvl));

  TEST_ASSERT_TRUE(strcmp("warn", onion_log_level_name(ONION_LOG_WARN)) == 0);
  TEST_ASSERT_TRUE(strcmp("?", onion_log_level_name((onion_log_level)99)) == 0);
  return 0;
}

static int test_set_level_clamps(void) {
  onion_log_set_level((onion_log_level)99);
  TEST_ASSERT_EQ_INT(ONION_LOG_TRACE, onion_log_get_level());
  onion_log_set_level((onion_log_level)-5);
  TEST_ASSERT_EQ_INT(ONION_LOG_OFF, onion_log_get_level());
  onion_log_set_level(ONION_LOG_INFO);
  return 0;
}

/* The crash path bypasses both gates — a fault is always worth recording. */
static int test_emergency_bypasses_level(void) {
  char buf[512];
  begin("Emerg");
  onion_log_set_level(ONION_LOG_OFF);
  onion_log_emergency("crash-record %d", 7);

  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "crash-record 7") != NULL);
  end();
  return 0;
}

/* Crash records use a dedicated append-only sink; ordinary records stay out. */
static int test_emergency_preserves_crash_sink(void) {
  char main_buf[512];
  char crash_buf[512];
  begin("CrashSink");

  FILE *previous = fopen(g_crash_path, "w");
  TEST_ASSERT_TRUE(previous != NULL);
  fputs("previous-crash\n", previous);
  fclose(previous);

  onion_log_configure_crash(g_crash_path);
  LOG_ERROR("ordinary-record");
  onion_log_set_level(ONION_LOG_OFF);
  onion_log_emergency("fatal-signal %d", 11);

  read_file(g_path, main_buf, sizeof(main_buf));
  read_file(g_crash_path, crash_buf, sizeof(crash_buf));
  TEST_ASSERT_TRUE(strstr(main_buf, "ordinary-record") != NULL);
  TEST_ASSERT_TRUE(strstr(main_buf, "fatal-signal 11") != NULL);
  TEST_ASSERT_TRUE(strstr(crash_buf, "previous-crash") != NULL);
  TEST_ASSERT_TRUE(strstr(crash_buf, "fatal-signal 11") != NULL);
  TEST_ASSERT_TRUE(strstr(crash_buf, "ordinary-record") == NULL);
  end();
  return 0;
}

/* Rotation is serialised across the processes that share a log path, which
 * needs a sidecar lock next to it. Reconfiguring drops the descriptor but the
 * sidecar stays behind — it carries no state beyond the kernel lock. */
static int test_rotation_creates_lock_sidecar(void) {
  char sidecar[80];
  char buf[512];

  begin("Lock");
  onion_log_set_max_bytes(512);
  for (int i = 0; i < 60; i++) {
    onion_log_write(ONION_LOG_ERROR, "lock-%02d-padding-padding-padding", i);
  }

  snprintf(sidecar, sizeof(sidecar), "%s.lock", g_path);
  TEST_ASSERT_TRUE(file_size(sidecar) >= 0);

  onion_log_configure("Lock", NULL);
  onion_log_configure("Lock", g_path);
  onion_log_write(ONION_LOG_ERROR, "after-reconfigure");

  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "after-reconfigure") != NULL);
  end();
  return 0;
}

/* Two payload processes append to the same path. A rotation done by one of
 * them must not drop the other's bytes, and the surviving writer must be able
 * to carry on against the live file afterwards. */
static int test_rotation_survives_second_writer(void) {
  static const char peer_record[] = "peer-record-from-other-process\n";
  char rotated_path[80];
  char buf[2048];

  begin("Shared");
  onion_log_set_max_bytes(512);

  /* Stand in for the other process with its own descriptor on the same path. */
  const int peer = open(g_path, O_WRONLY | O_CREAT | O_APPEND, 0777);
  TEST_ASSERT_TRUE(peer >= 0);
  TEST_ASSERT_EQ_INT((int)sizeof(peer_record) - 1,
                     (int)write(peer, peer_record, sizeof(peer_record) - 1));

  /* Write only until the first rotation happens, so the peer record is still
   * within the retained generations. */
  snprintf(rotated_path, sizeof(rotated_path), "%s.1", g_path);
  for (int i = 0; i < 64 && file_size(rotated_path) < 0; i++) {
    onion_log_write(ONION_LOG_ERROR, "own-%02d-padding-padding-padding", i);
  }
  TEST_ASSERT_TRUE(file_size(rotated_path) > 0);

  read_file(rotated_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, peer_record) != NULL);

  onion_log_write(ONION_LOG_ERROR, "after-rotation");
  read_file(g_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "after-rotation") != NULL);

  close(peer);
  end();
  return 0;
}

/* The crash sink has no rotation, so an oversized file must be moved aside
 * when a process configures it — otherwise /data fills up over time. */
static int test_crash_sink_caps_oversized_file(void) {
  char backup[80];
  char buf[256];
  static char filler[4096];

  begin("CrashCap");

  /* Stand in for crash files accumulated by earlier sessions. */
  FILE *stale = fopen(g_crash_path, "w");
  TEST_ASSERT_TRUE(stale != NULL);
  memset(filler, 'c', sizeof(filler));
  for (unsigned i = 0; i <= ONION_LOG_DEFAULT_CRASH_MAX_BYTES / sizeof(filler);
       ++i) {
    TEST_ASSERT_EQ_INT(sizeof(filler),
                       fwrite(filler, 1, sizeof(filler), stale));
  }
  fclose(stale);
  TEST_ASSERT_TRUE(file_size(g_crash_path) >
                   (long)ONION_LOG_DEFAULT_CRASH_MAX_BYTES);

  onion_log_configure_crash(g_crash_path);

  /* The old contents moved aside; the live file starts fresh. */
  snprintf(backup, sizeof(backup), "%s.1", g_crash_path);
  TEST_ASSERT_TRUE(file_size(backup) >
                   (long)ONION_LOG_DEFAULT_CRASH_MAX_BYTES);
  TEST_ASSERT_EQ_INT(0, (int)file_size(g_crash_path));

  onion_log_set_level(ONION_LOG_OFF);
  onion_log_emergency("after-crash-cap");
  read_file(g_crash_path, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "after-crash-cap") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "cccc") == NULL); /* old bytes are not mixed in */
  end();
  return 0;
}

/* A sink whose path cannot be opened must degrade, not crash: records fall back
 * to klog/stdout and no file appears. This is the shape a process whose sandbox
 * hides the path it was handed runs into — ShellUI pointing at /data inside the
 * vsh sandbox is the live example. onion_log_configure() reports it on klog,
 * which is the only channel left once the file sink is closed. */
static int test_unopenable_sink_degrades(void) {
  const char *bad = "/nonexistent-onion-dir-xyz/OnionHEN.log";

  onion_log_set_level(ONION_LOG_TRACE);
  onion_log_configure("NoSink", bad);
  LOG_ERROR("record with no file sink"); /* must not crash */
  onion_log_emergency("emergency with no file sink");

  TEST_ASSERT_TRUE(file_size(bad) < 0); /* nothing was created */

  onion_log_configure("OnionHEN", NULL);
  onion_log_set_level(ONION_LOG_INFO);
  return 0;
}

int test_platform_log_suite(void) {
  int failures = 0;
  failures += onion_test_run("log.file_sink", test_log_file_sink);
  failures += onion_test_run("log.configure_appends",
                             test_configure_appends_existing_log);
  failures += onion_test_run("log.configure_tag_only", test_log_configure_tag_only);
  failures += onion_test_run("log.runtime_level_filters", test_runtime_level_filters);
  failures += onion_test_run("log.level_off", test_level_off_silences_everything);
  failures += onion_test_run("log.rotation_caps_size", test_rotation_caps_size_and_keeps_backup);
  failures += onion_test_run("log.rotation_three_backups", test_rotation_keeps_three_backups);
  failures += onion_test_run("log.rotation_external_append",
                             test_rotation_observes_external_append);
  failures += onion_test_run("log.rotation_lock_sidecar",
                             test_rotation_creates_lock_sidecar);
  failures += onion_test_run("log.rotation_second_writer",
                             test_rotation_survives_second_writer);
  failures += onion_test_run("log.single_newline", test_single_trailing_newline);
  failures += onion_test_run("log.oversized_truncates", test_oversized_record_truncates);
  failures += onion_test_run("log.level_name_roundtrip", test_level_name_roundtrip);
  failures += onion_test_run("log.set_level_clamps", test_set_level_clamps);
  failures += onion_test_run("log.emergency_bypasses_level", test_emergency_bypasses_level);
  failures += onion_test_run("log.emergency_preserves_crash_sink",
                             test_emergency_preserves_crash_sink);
  failures += onion_test_run("log.crash_sink_caps_oversized",
                             test_crash_sink_caps_oversized_file);
  failures += onion_test_run("log.unopenable_sink_degrades",
                             test_unopenable_sink_degrades);
  return failures;
}
