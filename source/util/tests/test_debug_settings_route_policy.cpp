/* Host unit tests for DebugSettingsRoutePolicy (no PS5/Mono). */
#include "test_harness.h"

#include "welcome_toast.hpp"
#include "onion_cjson.hpp"
#include <onion/debug_settings_route_policy.hpp>

#include <string>

using onion::debug_settings_route::DebugSettingsRoutePolicy;
using onion::debug_settings_route::UriKind;

namespace {

struct WelcomeFields {
  bool valid = false;
  std::string message;
  std::string sub_message;
  std::string action_name;
  std::string action_url;
  std::string icon_url;
};

WelcomeFields parse_welcome(const std::string &json) {
  WelcomeFields fields;
  onion_cjson::Root root(json);
  const cJSON *raw = root ? onion_cjson::item(root.get(), "rawData") : nullptr;
  const cJSON *view = raw ? onion_cjson::item(raw, "viewData") : nullptr;
  const cJSON *message = view ? onion_cjson::item(view, "message") : nullptr;
  const cJSON *sub_message =
      view ? onion_cjson::item(view, "subMessage") : nullptr;
  const cJSON *actions = view ? onion_cjson::item(view, "actions") : nullptr;
  const cJSON *action = cJSON_IsArray(actions)
                            ? cJSON_GetArrayItem(actions, 0)
                            : nullptr;
  const cJSON *params = action ? onion_cjson::item(action, "parameters") : nullptr;
  const cJSON *icon = view ? onion_cjson::item(view, "icon") : nullptr;
  const cJSON *icon_params = icon ? onion_cjson::item(icon, "parameters") : nullptr;
  const char *message_text = onion_cjson::string_item(message, "body");
  const char *sub_message_text = onion_cjson::string_item(sub_message, "body");
  const char *action_name = onion_cjson::string_item(action, "actionName");
  const char *action_url = onion_cjson::string_item(params, "actionUrl");
  const char *icon_url = onion_cjson::string_item(icon_params, "url");
  if (!message_text || !sub_message_text || !action_name || !action_url ||
      !icon_url) {
    return fields;
  }
  fields.valid = true;
  fields.message = message_text;
  fields.sub_message = sub_message_text;
  fields.action_name = action_name;
  fields.action_url = action_url;
  fields.icon_url = icon_url;
  return fields;
}

} // namespace

static int test_0403_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x04030000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_0510_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x05100000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_0600_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x06000000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_0740_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x07400000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_0761_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x07610000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_0800_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x08000000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_0840_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x08040000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ(
      "pssettings:play?mode=settings&function=debug_settings",
      policy.toolbox_uri(UriKind::WithMode));
  return 0;
}

static int test_0900_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x09000000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ(
      "pssettings:play?mode=settings&function=debug_settings",
      policy.toolbox_uri(UriKind::WithMode));
  return 0;
}

static int test_1000_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x10000000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1001_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x10010000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ(
      "pssettings:play?mode=settings&function=debug_settings",
      policy.toolbox_uri(UriKind::WithMode));
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1020_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x10200000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1040_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x10040000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1060_uses_standard_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x10060000);

  TEST_ASSERT_TRUE(!policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1100_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x11000000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1120_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x11200000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1140_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x11400000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1160_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x11060000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ(
      "pssettings:play?mode=settings&function=debug_settings_old",
      policy.toolbox_uri(UriKind::WithMode));
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1270_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x12070000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1202_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x12020000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1240_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x12400000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1260_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x12600000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_1220_uses_old_route(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x12200000);

  TEST_ASSERT_TRUE(policy.uses_old_route());
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    policy.toolbox_uri(UriKind::Simple));
  return 0;
}

static int test_standard_route_does_not_rewrite(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x10010000);

  const std::string input =
      "pssettings:play?mode=settings&function=debug_settings";
  const std::string rewritten = policy.rewrite(input);
  TEST_ASSERT_STREQ(input.c_str(), rewritten.c_str());
  return 0;
}

static int test_old_route_rewrites_debug_settings(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x11060000);

  const std::string rewritten = policy.rewrite(
      "pssettings:play?mode=settings&function=debug_settings&x=1");
  TEST_ASSERT_STREQ(
      "pssettings:play?mode=settings&function=debug_settings_old&x=1",
      rewritten.c_str());
  return 0;
}

static int test_old_route_rewrites_only_function_param(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x11060000);

  const std::string rewritten = policy.rewrite(
      "pssettings:play?xfunction=debug_settings&function=debug_settings#end");
  TEST_ASSERT_STREQ(
      "pssettings:play?xfunction=debug_settings&function=debug_settings_old#end",
      rewritten.c_str());
  return 0;
}

static int test_old_route_rewrite_is_idempotent(void) {
  const DebugSettingsRoutePolicy policy =
      DebugSettingsRoutePolicy::for_system_version(0x11060000);

  const std::string rewritten =
      policy.rewrite("pssettings:play?function=debug_settings_old");
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings_old",
                    rewritten.c_str());
  return 0;
}

static int test_settings_bundle_accepts_known_1001_hash(void) {
  static const uint8_t hash[] = {
      0xad, 0x6c, 0xf2, 0xd6, 0xf8, 0x97, 0x4c, 0xcd, 0x34, 0xb1,
      0x4e, 0x69, 0xbb, 0x6e, 0x34, 0x0e, 0x8d, 0xec, 0x5d, 0xc5};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4dda8c, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_0900_hash(void) {
  static const uint8_t hash[] = {
      0x72, 0x18, 0x8b, 0x52, 0xb1, 0x2b, 0xad, 0x6a, 0xf9, 0x0c,
      0x90, 0xa8, 0x48, 0xb7, 0xfd, 0x76, 0xe5, 0xaf, 0x10, 0x2d};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4b1934, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_0940_hash(void) {
  static const uint8_t hash[] = {
      0x4f, 0x1a, 0xe4, 0xb6, 0x78, 0x6c, 0xc9, 0x66, 0x46, 0xe1,
      0x4e, 0xec, 0x92, 0x3d, 0x09, 0xc3, 0xc0, 0x31, 0xe9, 0x80};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4ba2c0, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_0960_hash(void) {
  static const uint8_t hash[] = {
      0x59, 0x50, 0x6d, 0x7b, 0x5c, 0x59, 0x51, 0x96, 0xb2, 0x16,
      0x61, 0x26, 0x4b, 0xc9, 0xc1, 0xb4, 0x87, 0xd2, 0x1b, 0x51};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4ba1f0, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1040_hash(void) {
  static const uint8_t hash[] = {
      0xab, 0xb8, 0xfd, 0xf5, 0xa8, 0x94, 0xce, 0x6f, 0xd1, 0xe9,
      0x93, 0x81, 0xd0, 0x86, 0x6b, 0x33, 0xf2, 0x79, 0xc7, 0xb9};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4e089c, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1060_hash(void) {
  static const uint8_t hash[] = {
      0x31, 0x65, 0x1a, 0x18, 0x8d, 0x49, 0xb2, 0x3b, 0x76, 0x35,
      0xaf, 0xa4, 0x49, 0x39, 0x5e, 0x0f, 0xbd, 0x9f, 0x68, 0x2a};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4e0954, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1100_hash(void) {
  static const uint8_t hash[] = {
      0x18, 0x24, 0xc9, 0xfb, 0x56, 0x2e, 0x31, 0xee, 0xf6, 0x51,
      0xbb, 0x38, 0x74, 0xc1, 0xc7, 0x3f, 0x7f, 0x6e, 0x24, 0xb0};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4fa540, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1120_hash(void) {
  static const uint8_t hash[] = {
      0xd0, 0x34, 0x62, 0xa9, 0x12, 0xc4, 0xb5, 0xb8, 0xdb, 0x4a,
      0x98, 0xd0, 0x44, 0xb9, 0xd4, 0x88, 0xa2, 0xdf, 0xfc, 0x7a};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4f45b8, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1140_hash(void) {
  static const uint8_t hash[] = {
      0xa7, 0xb7, 0x31, 0x57, 0x1f, 0x84, 0xb6, 0xcd, 0xaf, 0x7c,
      0x42, 0x27, 0xa9, 0x80, 0xba, 0x5e, 0xe2, 0x00, 0x04, 0xa8};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4f45c4, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1160_hash(void) {
  static const uint8_t hash[] = {
      0x92, 0x56, 0x61, 0x24, 0xb6, 0xcf, 0xe0, 0xb0, 0xa7, 0xc8,
      0x12, 0xfc, 0x8a, 0x3b, 0xbf, 0xcf, 0x32, 0xac, 0x46, 0x83};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4f4bfc, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1270_hash(void) {
  static const uint8_t hash[] = {
      0x44, 0x5d, 0xa8, 0xbc, 0xba, 0x93, 0xda, 0x16, 0x54, 0x73,
      0xd3, 0xda, 0x49, 0x1d, 0x9b, 0x13, 0xf9, 0x63, 0x16, 0xcd};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4e9048, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1202_hash(void) {
  static const uint8_t hash[] = {
      0xfc, 0x7c, 0x4f, 0x15, 0xaf, 0x42, 0x92, 0x9e, 0x1d, 0x52,
      0x42, 0x0c, 0x2d, 0x17, 0x49, 0x44, 0xb4, 0xa8, 0x80, 0x43};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4e7bec, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1260_hash(void) {
  static const uint8_t hash[] = {
      0x75, 0x74, 0x7b, 0xb5, 0xfa, 0x7e, 0x3a, 0x4e, 0x22, 0xd5,
      0x57, 0x88, 0x2f, 0x52, 0x81, 0xe4, 0xd1, 0xf1, 0x29, 0x59};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4e9028, hash));
  return 0;
}

static int test_settings_bundle_accepts_known_1220_hash(void) {
  static const uint8_t hash[] = {
      0x5d, 0x44, 0x61, 0x85, 0x8b, 0x0a, 0x38, 0xfc, 0x6e, 0x7b,
      0x08, 0x6d, 0xbf, 0xda, 0xb6, 0x19, 0x51, 0x59, 0x08, 0x0e};
  TEST_ASSERT_TRUE(onion::debug_settings_route::settings_bundle_is_supported(
      0x4e8e54, hash));
  return 0;
}

static int test_settings_bundle_rejects_hash_mismatch(void) {
  uint8_t hash[onion::debug_settings_route::kSourceHashLength]{};
  TEST_ASSERT_TRUE(!onion::debug_settings_route::settings_bundle_is_supported(
      0x4dda8c, hash));
  return 0;
}

static int test_settings_bundle_rejects_length_mismatch(void) {
  static const uint8_t hash[] = {
      0xad, 0x6c, 0xf2, 0xd6, 0xf8, 0x97, 0x4c, 0xcd, 0x34, 0xb1,
      0x4e, 0x69, 0xbb, 0x6e, 0x34, 0x0e, 0x8d, 0xec, 0x5d, 0xc5};
  TEST_ASSERT_TRUE(!onion::debug_settings_route::settings_bundle_is_supported(
      0x4dda8d, hash));
  return 0;
}

static int test_welcome_toast_replaces_toolbox_uri(void) {
  const char *uri =
      "pssettings:play?function=debug_settings_old&label=\"quoted\"";
  const WelcomeFields fields = parse_welcome(
      onion::daemon::make_welcome_toast_json(uri));
  TEST_ASSERT_TRUE(fields.valid);
  TEST_ASSERT_STREQ(uri, fields.action_url.c_str());
  TEST_ASSERT_STREQ(ONION_NOTIFY_ICON_PATH, fields.icon_url.c_str());
  TEST_ASSERT_STREQ("Welcome to OnionHEN", fields.sub_message.c_str());
  TEST_ASSERT_STREQ(ONIONHEN_VERSION " made by " ONIONHEN_AUTHOR,
                    fields.message.c_str());
  return 0;
}

static int test_welcome_toast_fallback_uri(void) {
  const WelcomeFields fields =
      parse_welcome(onion::daemon::make_welcome_toast_json(""));
  TEST_ASSERT_TRUE(fields.valid);
  TEST_ASSERT_STREQ("pssettings:play?function=debug_settings",
                    fields.action_url.c_str());
  return 0;
}

static int test_welcome_toast_localizes_text(void) {
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);
  const WelcomeFields fields = parse_welcome(
      onion::daemon::make_welcome_toast_json(
          "pssettings:play?function=debug_settings"));
  TEST_ASSERT_TRUE(fields.valid);
  TEST_ASSERT_STREQ("欢迎使用 OnionHEN", fields.sub_message.c_str());
  TEST_ASSERT_STREQ("前往 OnionHEN 工具箱", fields.action_name.c_str());
  const std::string expected =
      std::string(ONIONHEN_VERSION) + " · 作者：" + ONIONHEN_AUTHOR;
  TEST_ASSERT_STREQ(expected.c_str(), fields.message.c_str());
  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  return 0;
}

extern "C" int test_debug_settings_route_policy_suite(void) {
  int fails = 0;
  fails += onion_test_run("debug_route.0403_standard",
                          test_0403_uses_standard_route);
  fails += onion_test_run("debug_route.0510_standard",
                          test_0510_uses_standard_route);
  fails += onion_test_run("debug_route.0600_standard",
                          test_0600_uses_standard_route);
  fails += onion_test_run("debug_route.0740_standard",
                          test_0740_uses_standard_route);
  fails += onion_test_run("debug_route.0761_standard",
                          test_0761_uses_standard_route);
  fails += onion_test_run("debug_route.0800_standard",
                          test_0800_uses_standard_route);
  fails += onion_test_run("debug_route.0840_standard",
                          test_0840_uses_standard_route);
  fails += onion_test_run("debug_route.0900_standard",
                          test_0900_uses_standard_route);
  fails += onion_test_run("debug_route.1000_standard",
                          test_1000_uses_standard_route);
  fails += onion_test_run("debug_route.1001_standard",
                          test_1001_uses_standard_route);
  fails += onion_test_run("debug_route.1020_standard",
                          test_1020_uses_standard_route);
  fails += onion_test_run("debug_route.1040_standard",
                          test_1040_uses_standard_route);
  fails += onion_test_run("debug_route.1060_standard",
                          test_1060_uses_standard_route);
  fails += onion_test_run("debug_route.1100_old", test_1100_uses_old_route);
  fails += onion_test_run("debug_route.1120_old", test_1120_uses_old_route);
  fails += onion_test_run("debug_route.1140_old", test_1140_uses_old_route);
  fails += onion_test_run("debug_route.1160_old", test_1160_uses_old_route);
  fails += onion_test_run("debug_route.1202_old", test_1202_uses_old_route);
  fails += onion_test_run("debug_route.1240_old", test_1240_uses_old_route);
  fails += onion_test_run("debug_route.1260_old", test_1260_uses_old_route);
  fails += onion_test_run("debug_route.1270_old", test_1270_uses_old_route);
  fails += onion_test_run("debug_route.1220_old", test_1220_uses_old_route);
  fails += onion_test_run("debug_route.standard_no_rewrite",
                          test_standard_route_does_not_rewrite);
  fails += onion_test_run("debug_route.old_rewrite",
                          test_old_route_rewrites_debug_settings);
  fails += onion_test_run("debug_route.old_query_rewrite",
                          test_old_route_rewrites_only_function_param);
  fails += onion_test_run("debug_route.old_idempotent",
                          test_old_route_rewrite_is_idempotent);
  fails += onion_test_run("debug_route.bundle_1001",
                          test_settings_bundle_accepts_known_1001_hash);
  fails += onion_test_run("debug_route.bundle_0900",
                          test_settings_bundle_accepts_known_0900_hash);
  fails += onion_test_run("debug_route.bundle_0940",
                          test_settings_bundle_accepts_known_0940_hash);
  fails += onion_test_run("debug_route.bundle_0960",
                          test_settings_bundle_accepts_known_0960_hash);
  fails += onion_test_run("debug_route.bundle_1040",
                          test_settings_bundle_accepts_known_1040_hash);
  fails += onion_test_run("debug_route.bundle_1060",
                          test_settings_bundle_accepts_known_1060_hash);
  fails += onion_test_run("debug_route.bundle_1100",
                          test_settings_bundle_accepts_known_1100_hash);
  fails += onion_test_run("debug_route.bundle_1120",
                          test_settings_bundle_accepts_known_1120_hash);
  fails += onion_test_run("debug_route.bundle_1140",
                          test_settings_bundle_accepts_known_1140_hash);
  fails += onion_test_run("debug_route.bundle_1160",
                          test_settings_bundle_accepts_known_1160_hash);
  fails += onion_test_run("debug_route.bundle_1202",
                          test_settings_bundle_accepts_known_1202_hash);
  fails += onion_test_run("debug_route.bundle_1260",
                          test_settings_bundle_accepts_known_1260_hash);
  fails += onion_test_run("debug_route.bundle_1270",
                          test_settings_bundle_accepts_known_1270_hash);
  fails += onion_test_run("debug_route.bundle_1220",
                          test_settings_bundle_accepts_known_1220_hash);
  fails += onion_test_run("debug_route.bundle_hash_reject",
                          test_settings_bundle_rejects_hash_mismatch);
  fails += onion_test_run("debug_route.bundle_length_reject",
                          test_settings_bundle_rejects_length_mismatch);
  fails += onion_test_run("daemon.welcome_toast_uri",
                          test_welcome_toast_replaces_toolbox_uri);
  fails += onion_test_run("daemon.welcome_toast_fallback",
                          test_welcome_toast_fallback_uri);
  fails += onion_test_run("daemon.welcome_toast_i18n",
                          test_welcome_toast_localizes_text);
  return fails;
}
