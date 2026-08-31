/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Host tests for the built-in service key helpers (ShadowMount+ reservation).
 */
#include "test_harness.h"

#include <onion/builtin_services.h>

static int test_shadowmount_key_matching(void) {
  TEST_ASSERT_TRUE(onion_builtin_shadowmount_key("shadowmountplus"));
  TEST_ASSERT_TRUE(onion_builtin_shadowmount_key("ShadowMountPlus"));
  TEST_ASSERT_TRUE(onion_builtin_shadowmount_key("SHADOWMOUNTPLUS"));
  TEST_ASSERT_TRUE(!onion_builtin_shadowmount_key("shadowmount"));
  TEST_ASSERT_TRUE(!onion_builtin_shadowmount_key("ftpsrv"));
  TEST_ASSERT_TRUE(!onion_builtin_shadowmount_key(""));
  TEST_ASSERT_TRUE(!onion_builtin_shadowmount_key(NULL));
  return 0;
}

static int test_reserved_service_keys(void) {
  TEST_ASSERT_TRUE(onion_builtin_service_key_reserved("shadowmountplus"));
  TEST_ASSERT_TRUE(onion_builtin_service_key_reserved("ShadowMountPlus"));
  TEST_ASSERT_TRUE(!onion_builtin_service_key_reserved("shadowmountplusx"));
  TEST_ASSERT_TRUE(!onion_builtin_service_key_reserved("ftp"));
  TEST_ASSERT_TRUE(!onion_builtin_service_key_reserved(""));
  TEST_ASSERT_TRUE(!onion_builtin_service_key_reserved(NULL));
  return 0;
}

static int test_service_ports(void) {
  TEST_ASSERT_EQ_U64(1337u, ONION_FTPSRV_PORT);
  TEST_ASSERT_EQ_U64(9090u, ONION_PKGNET_PORT);
  TEST_ASSERT_EQ_U64(12800u, ONION_WEBUI_PORT);
  return 0;
}

int test_builtin_services_suite(void) {
  int fails = 0;
  fails += onion_test_run("builtin_services.shadowmount_key",
                          test_shadowmount_key_matching);
  fails += onion_test_run("builtin_services.reserved_keys",
                          test_reserved_service_keys);
  fails += onion_test_run("builtin_services.service_ports",
                          test_service_ports);
  return fails;
}
