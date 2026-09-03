/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Host tests for built-in service port assignments.
 */
#include "test_harness.h"

#include <onion/builtin_services.h>

static int test_service_ports(void) {
  TEST_ASSERT_EQ_U64(9090u, ONION_PKGNET_PORT);
  TEST_ASSERT_EQ_U64(12800u, ONION_WEBUI_PORT);
  return 0;
}

int test_builtin_services_suite(void) {
  int fails = 0;
  fails += onion_test_run("builtin_services.service_ports",
                          test_service_ports);
  return fails;
}
