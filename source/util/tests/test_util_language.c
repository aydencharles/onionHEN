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
  /* Mirrors the daemon → util IPC push (BREW_UTIL_SET_SYSTEM_LANG): a fresh
   * console language with ui_lang=system must cascade to the cache, the
   * notify language and the web UI code. SCE 3 = Spanish. */
  util_store_system_language(3);
  TEST_ASSERT_EQ_INT(3, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ES, onion_notify_get_language());
  TEST_ASSERT_STREQ("es", util_webui_language_code());

  util_store_system_language(1);
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN, onion_notify_get_language());
  TEST_ASSERT_STREQ("en", util_webui_language_code());

  /* An explicit Toolbox language keeps winning over the pushed console lang. */
  util_store_system_language(4);
  util_apply_ui_language(9);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ES, onion_notify_get_language());
  return 0;
}

static int test_webui_codes_cover_all_locales(void) {
  /* Every selectable Toolbox language (1..14) must map to a web UI locale
   * code; the web UI ships a dictionary for each one. */
  static const struct {
    int ui_language;
    const char *code;
  } kWebuiCodes[] = {
    { 1, "zh-Hans" }, { 2, "en" },  { 3, "ar" }, { 4, "zh-Hant" },
    { 5, "ja" },      { 6, "fr" },  { 7, "de" }, { 8, "ko" },
    { 9, "es" },      { 10, "pt-BR" }, { 11, "it" }, { 12, "ru" },
    { 13, "pl" },     { 14, "th" },
  };
  size_t i;

  for (i = 0; i < sizeof(kWebuiCodes) / sizeof(kWebuiCodes[0]); ++i) {
    util_apply_ui_language(kWebuiCodes[i].ui_language);
    TEST_ASSERT_STREQ(kWebuiCodes[i].code, util_webui_language_code());
  }
  /* Restore the suite's original ending state (system=English, ui=system) so
   * later suites observe the English default. */
  util_store_system_language(1);
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN, onion_notify_get_language());
  return 0;
}

int test_util_language_suite(void) {
  return onion_test_run("util_language.refresh_preserves_last_valid",
                        test_refresh_preserves_last_valid_language) +
         onion_test_run("util_language.store_reapplies_runtime_language",
                        test_store_reapplies_runtime_language) +
         onion_test_run("util_language.webui_codes_cover_all_locales",
                        test_webui_codes_cover_all_locales);
}
