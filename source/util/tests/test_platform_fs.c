/* Host tests for libonion_platform fs helpers. */
#include "test_harness.h"

#include <onion/fs.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int make_temp_dir(char *buf, size_t buflen) {
  char tmpl[] = "/tmp/onion-fs-XXXXXX";
  if (!mkdtemp(tmpl)) {
    return -1;
  }
  if (strlen(tmpl) + 1 > buflen) {
    return -1;
  }
  memcpy(buf, tmpl, strlen(tmpl) + 1);
  return 0;
}

static int test_if_exists_null_and_missing(void) {
  TEST_ASSERT_TRUE(!if_exists(NULL));
  TEST_ASSERT_TRUE(!if_exists("/tmp/onion-fs-definitely-missing-xyz-9f3a"));
  return 0;
}

static int read_file(const char *path, char *buf, size_t buf_size, size_t *out_len) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return -1;
  const size_t n = fread(buf, 1, buf_size, fp);
  fclose(fp);
  if (out_len)
    *out_len = n;
  return 0;
}

static int test_write_file_atomic(void) {
  char dir[64];
  char path[128];
  char buf[64];
  size_t n = 0;
  const char first[] = "hello";
  const char second[] = "replaced-content";

  TEST_ASSERT_TRUE(!write_file_atomic(NULL, first, sizeof(first) - 1));
  TEST_ASSERT_TRUE(!write_file_atomic("", first, sizeof(first) - 1));
  TEST_ASSERT_TRUE(!write_file_atomic("/tmp/x", NULL, 4));

  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/blob", dir);

  TEST_ASSERT_TRUE(write_file_atomic(path, first, sizeof(first) - 1));
  TEST_ASSERT_TRUE(if_exists(path));
  TEST_ASSERT_EQ_INT(0, read_file(path, buf, sizeof(buf), &n));
  TEST_ASSERT_EQ_U64(sizeof(first) - 1, n);
  TEST_ASSERT_TRUE(memcmp(buf, first, n) == 0);

  TEST_ASSERT_TRUE(write_file_atomic(path, second, sizeof(second) - 1));
  TEST_ASSERT_EQ_INT(0, read_file(path, buf, sizeof(buf), &n));
  TEST_ASSERT_EQ_U64(sizeof(second) - 1, n);
  TEST_ASSERT_TRUE(memcmp(buf, second, n) == 0);

  TEST_ASSERT_TRUE(write_file_atomic(path, NULL, 0));
  TEST_ASSERT_EQ_INT(0, read_file(path, buf, sizeof(buf), &n));
  TEST_ASSERT_EQ_U64(0, n);

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

static int test_touch_and_exists(void) {
  char dir[64];
  char path[128];
  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/flag", dir);

  TEST_ASSERT_TRUE(!if_exists(path));
  TEST_ASSERT_TRUE(touch_file(path));
  TEST_ASSERT_TRUE(if_exists(path));
  TEST_ASSERT_TRUE(touch_file(path)); /* truncate ok */

  TEST_ASSERT_TRUE(!touch_file(NULL));

  unlink(path);
  rmdir(dir);
  return 0;
}

static int test_mkdir_tree(void) {
  char dir[64];
  char nested[160];
  char too_long[1025];
  struct stat st;

  TEST_ASSERT_TRUE(!mkdir_tree(NULL));
  TEST_ASSERT_TRUE(!mkdir_tree(""));
  memset(too_long, 'a', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';
  TEST_ASSERT_TRUE(!mkdir_tree(too_long));

  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(nested, sizeof(nested), "%s/a/b/c", dir);
  TEST_ASSERT_TRUE(mkdir_tree(nested));
  TEST_ASSERT_TRUE(if_exists(nested));
  TEST_ASSERT_EQ_INT(0, stat(nested, &st));
  TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
  TEST_ASSERT_TRUE(mkdir_tree(nested));

  TEST_ASSERT_TRUE(rmtree(dir));
  return 0;
}

static int test_rmtree_nested(void) {
  char dir[64];
  char sub[128];
  char file[160];
  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(sub, sizeof(sub), "%s/nested", dir);
  TEST_ASSERT_EQ_INT(0, mkdir(sub, 0777));
  snprintf(file, sizeof(file), "%s/a.txt", sub);
  TEST_ASSERT_TRUE(touch_file(file));
  snprintf(file, sizeof(file), "%s/b.txt", dir);
  TEST_ASSERT_TRUE(touch_file(file));

  TEST_ASSERT_TRUE(rmtree(dir));
  TEST_ASSERT_TRUE(!if_exists(dir));
  TEST_ASSERT_TRUE(!rmtree(NULL));
  TEST_ASSERT_TRUE(!rmtree("/tmp/onion-fs-missing-tree-xyz"));
  return 0;
}

static size_t progress_calls;
static size_t progress_completed;
static size_t progress_total;

static void capture_rmtree_progress(size_t completed, size_t total,
                                    void *user) {
  (void)user;
  ++progress_calls;
  progress_completed = completed;
  progress_total = total;
}

static int test_rmtree_progress(void) {
  char dir[64];
  char sub[128];
  char file[160];
  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(sub, sizeof(sub), "%s/nested", dir);
  TEST_ASSERT_EQ_INT(0, mkdir(sub, 0777));
  snprintf(file, sizeof(file), "%s/a.txt", sub);
  TEST_ASSERT_TRUE(touch_file(file));
  snprintf(file, sizeof(file), "%s/b.txt", dir);
  TEST_ASSERT_TRUE(touch_file(file));

  progress_calls = 0;
  progress_completed = 0;
  progress_total = 0;
  TEST_ASSERT_TRUE(
      rmtree_with_progress(dir, capture_rmtree_progress, NULL));
  TEST_ASSERT_TRUE(!if_exists(dir));
  TEST_ASSERT_EQ_U64(4, progress_total);
  TEST_ASSERT_EQ_U64(progress_total, progress_completed);
  TEST_ASSERT_EQ_U64(5, progress_calls);
  return 0;
}

int test_platform_fs_suite(void) {
  int failures = 0;
  failures += onion_test_run("fs_if_exists_null_missing", test_if_exists_null_and_missing);
  failures += onion_test_run("fs_touch_and_exists", test_touch_and_exists);
  failures += onion_test_run("fs_write_file_atomic", test_write_file_atomic);
  failures += onion_test_run("fs_mkdir_tree", test_mkdir_tree);
  failures += onion_test_run("fs_rmtree_nested", test_rmtree_nested);
  failures += onion_test_run("fs_rmtree_progress", test_rmtree_progress);
  return failures;
}
