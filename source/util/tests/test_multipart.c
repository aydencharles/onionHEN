/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Host tests for the pkgserver multipart/form-data header parsing.
 */
#include "test_harness.h"

#include "multipart.h"

static int test_boundary_plain(void) {
  char out[128];
  TEST_ASSERT_TRUE(pkgserver_multipart_boundary(
                       "multipart/form-data; boundary=----WebKitFormBoundaryXYZ",
                       out, sizeof(out)) == 0);
  TEST_ASSERT_STREQ("----WebKitFormBoundaryXYZ", out);
  return 0;
}

static int test_boundary_quoted(void) {
  char out[128];
  TEST_ASSERT_TRUE(pkgserver_multipart_boundary(
                       "multipart/form-data; boundary=\"abc def\"", out,
                       sizeof(out)) == 0);
  TEST_ASSERT_STREQ("abc def", out);
  return 0;
}

static int test_boundary_missing(void) {
  char out[128];
  TEST_ASSERT_TRUE(pkgserver_multipart_boundary("application/json", out,
                                                sizeof(out)) != 0);
  TEST_ASSERT_TRUE(pkgserver_multipart_boundary(NULL, out, sizeof(out)) != 0);
  TEST_ASSERT_TRUE(pkgserver_multipart_boundary("multipart/form-data",
                                                NULL, 64) != 0);
  return 0;
}

static int test_filename_quoted(void) {
  char out[128];
  TEST_ASSERT_TRUE(pkgserver_multipart_filename(
                       "form-data; name=\"packages\"; filename=\"game.pkg\"",
                       out, sizeof(out)) == 0);
  TEST_ASSERT_STREQ("game.pkg", out);
  return 0;
}

static int test_filename_unquoted(void) {
  char out[128];
  /* Unquoted values end at whitespace per RFC 7578; browsers always quote. */
  TEST_ASSERT_TRUE(pkgserver_multipart_filename(
                       "form-data; name=packages; filename=update_v2.pkg", out,
                       sizeof(out)) == 0);
  TEST_ASSERT_STREQ("update_v2.pkg", out);
  return 0;
}

static int test_filename_missing(void) {
  char out[128];
  TEST_ASSERT_TRUE(pkgserver_multipart_filename("form-data; name=\"packages\"",
                                                out, sizeof(out)) != 0);
  TEST_ASSERT_TRUE(pkgserver_multipart_filename(NULL, out, sizeof(out)) != 0);
  return 0;
}

int test_multipart_suite(void) {
  int fails = 0;
  fails += onion_test_run("multipart.boundary_plain", test_boundary_plain);
  fails += onion_test_run("multipart.boundary_quoted", test_boundary_quoted);
  fails += onion_test_run("multipart.boundary_missing", test_boundary_missing);
  fails += onion_test_run("multipart.filename_quoted", test_filename_quoted);
  fails += onion_test_run("multipart.filename_unquoted", test_filename_unquoted);
  fails += onion_test_run("multipart.filename_missing", test_filename_missing);
  return fails;
}
