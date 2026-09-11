#include "test_harness.h"

#include "util_language.h"

#include <onion/notify_i18n.h>

void onion_test_system_language_configure(int result, int value);

static int test_refresh_preserves_last_valid_language(void) {
  onion_test_system_language_configure(0, 11);
  TEST_ASSERT_TRUE(util_refresh_system_language());
  TEST_ASSERT_EQ_INT(11, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS, onion_notify_get_language());

  onion_test_system_language_configure(-1, 1);
  TEST_ASSERT_TRUE(!util_refresh_system_language());
  TEST_ASSERT_EQ_INT(11, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS, onion_notify_get_language());

  onion_test_system_language_configure(0, 1);
  TEST_ASSERT_TRUE(util_refresh_system_language());
  TEST_ASSERT_EQ_INT(1, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN, onion_notify_get_language());
  return 0;
}

static int test_store_reapplies_runtime_language(void) {
  /* Mirrors the daemon → util IPC push: a fresh console language with
   * ui_lang=system must cascade to the notification language. SCE 3 = Spanish. */
  util_store_system_language(3);
  TEST_ASSERT_EQ_INT(3, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ES, onion_notify_get_language());

  util_store_system_language(1);
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN, onion_notify_get_language());

  /* An explicit Toolbox language keeps winning over the pushed console lang. */
  util_store_system_language(4);
  util_apply_ui_language(9);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ES, onion_notify_get_language());

  /* Keep later suites independent from this process-global language state. */
  util_store_system_language(1);
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN, onion_notify_get_language());
  return 0;
}

int test_util_language_suite(void) {
  return onion_test_run("util_language.refresh_preserves_last_valid",
                        test_refresh_preserves_last_valid_language) +
         onion_test_run("util_language.store_reapplies_runtime_language",
                        test_store_reapplies_runtime_language);
}
