/* Host tests for onion_notify_format (pure string path). */
#include "test_harness.h"

#include <onion/notify.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void format_msg(char *out, size_t out_sz, int wm, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  onion_notify_format(out, out_sz, wm, fmt, ap);
  va_end(ap);
}

static int test_notify_format_prefix(void) {
  char out[128];
  format_msg(out, sizeof(out), 1, "hello %s", "world");
  TEST_ASSERT_STREQ("[OnionHEN] hello world", out);
  return 0;
}

static int test_notify_format_truncates(void) {
  char out[16];
  format_msg(out, sizeof(out), 1, "0123456789ABCDEFGHIJ");
  /* must be NUL-terminated and start with watermark prefix */
  TEST_ASSERT_TRUE(out[sizeof(out) - 1] == '\0' || strlen(out) < sizeof(out));
  TEST_ASSERT_TRUE(strncmp(out, "[OnionHEN]", 10) == 0);
  return 0;
}

static int test_notify_format_no_watermark(void) {
  char out[128];
  format_msg(out, sizeof(out), 0, "hello %s", "world");
  TEST_ASSERT_STREQ("hello world", out);
  return 0;
}

static int test_notify_send_noop(void) {
  /* hits sceKernelSendNotificationRequest stub — must not crash */
  onion_notify(1, "host test notify %d", 7);
  onion_notify_debug("host test debug notify %d", 7);
  return 0;
}

static int test_notify_language_resolution(void) {
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS,
                     onion_notify_resolve_language(0, 11));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS,
                     onion_notify_resolve_language(1, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN,
                     onion_notify_resolve_language(2, 11));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_AR,
                     onion_notify_resolve_language(3, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANT,
                     onion_notify_resolve_language(4, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_JA,
                     onion_notify_resolve_language(5, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_FR,
                     onion_notify_resolve_language(6, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_DE,
                     onion_notify_resolve_language(7, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_KO,
                     onion_notify_resolve_language(8, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ES,
                     onion_notify_resolve_language(9, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_PT_BR,
                     onion_notify_resolve_language(10, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_IT,
                     onion_notify_resolve_language(11, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_RU,
                     onion_notify_resolve_language(12, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_PL,
                     onion_notify_resolve_language(13, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_TH,
                     onion_notify_resolve_language(14, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_AR,
                     onion_notify_resolve_language(0, 21));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_JA,
                     onion_notify_resolve_language(0, 0));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_FR,
                     onion_notify_resolve_language(0, 2));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_FR,
                     onion_notify_resolve_language(0, 22));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_DE,
                     onion_notify_resolve_language(0, 4));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANT,
                     onion_notify_resolve_language(0, 10));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS,
                     onion_notify_resolve_language(0, 11));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN,
                     onion_notify_resolve_language(0, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_KO,
                     onion_notify_resolve_language(0, 9));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ES,
                     onion_notify_resolve_language(0, 3));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ES,
                     onion_notify_resolve_language(0, 20));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_PT_BR,
                     onion_notify_resolve_language(0, 17));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_PT_BR,
                     onion_notify_resolve_language(0, 7));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_IT,
                     onion_notify_resolve_language(0, 5));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_RU,
                     onion_notify_resolve_language(0, 8));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_PL,
                     onion_notify_resolve_language(0, 16));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_TH,
                     onion_notify_resolve_language(0, 27));
  return 0;
}

static int test_notify_format_localized(void) {
  char out[128];
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] 无法提升权限", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Unable to raise privileges", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_AR);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] تعذّر رفع الامتيازات", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANT);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] 無法提升權限", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_JA);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] 権限を昇格できません", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_FR);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Impossible d’élever les privilèges", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_DE);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Berechtigungen konnten nicht erhöht werden",
                    out);
  onion_notify_set_language(ONION_NOTIFY_LANG_KO);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] 권한을 올릴 수 없습니다", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_ES);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] No se pudieron elevar los privilegios", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_PT_BR);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Não foi possível elevar os privilégios", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_IT);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Impossibile elevare i privilegi", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_RU);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Не удалось повысить привилегии", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_PL);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Nie udało się podnieść uprawnień", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_TH);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] ไม่สามารถยกระดับสิทธิ์ได้", out);
  return 0;
}

static int test_notify_payload_localized(void) {
  char out[256];
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);

  format_msg(out, sizeof(out), 1, "notify.payload.loading", "demo.elf");
  TEST_ASSERT_STREQ("[OnionHEN] 正在加载 Payload demo.elf...", out);

  format_msg(out, sizeof(out), 1,
             "notify.payload.launched", "/data/demo.elf",
             "demo");
  TEST_ASSERT_STREQ(
      "[OnionHEN] Payload 已启动\n路径：/data/demo.elf\n标识：demo", out);

  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  return 0;
}

static char g_rich_payload[4096];

static int32_t capture_rich_notify(int32_t user_id, bool is_logged,
                                   const char *payload) {
  TEST_ASSERT_TRUE(user_id == 0xFE);
  TEST_ASSERT_TRUE(is_logged);
  snprintf(g_rich_payload, sizeof(g_rich_payload), "%s",
           payload ? payload : "");
  return 0;
}

static int test_notify_rich_formats_payload(void) {
  g_rich_payload[0] = '\0';
  onion_notify_set_rich_send(capture_rich_notify);
  onion_notify_rich("Title", "Sub \"quoted\"", "/icon.png", "download", "42");
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "InteractiveToastTemplateB") != NULL);
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "\"body\": \"Title\"") != NULL);
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "Sub \\\"quoted\\\"") != NULL);
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "\"localNotificationId\": \"42\"") !=
                   NULL);
  return 0;
}

static int test_notify_rich_localizes_both_text_fields(void) {
  g_rich_payload[0] = '\0';
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);
  onion_notify_set_rich_send(capture_rich_notify);
  onion_notify_rich("notify.brand", "notify.boot.starting", "/icon.png",
                    "download", "43");
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "OnionHEN 正在启动...") != NULL);
  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  return 0;
}

int test_platform_notify_suite(void) {
  int failures = 0;
  failures += onion_test_run("notify_format_prefix", test_notify_format_prefix);
  failures += onion_test_run("notify_format_truncates", test_notify_format_truncates);
  failures +=
      onion_test_run("notify_format_no_watermark", test_notify_format_no_watermark);
  failures += onion_test_run("notify_send_noop", test_notify_send_noop);
  failures += onion_test_run("notify_language_resolution",
                             test_notify_language_resolution);
  failures += onion_test_run("notify_format_localized",
                             test_notify_format_localized);
  failures += onion_test_run("notify_payload_localized",
                             test_notify_payload_localized);
  failures +=
      onion_test_run("notify_rich_formats_payload", test_notify_rich_formats_payload);
  failures += onion_test_run("notify_rich_localizes_both_text_fields",
                             test_notify_rich_localizes_both_text_fields);
  return failures;
}
