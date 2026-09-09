#include "test_harness.h"

#include "cheats/sync/cheat_catalog_registry.hpp"
#include "cheats/sync/cheat_mirror_factory.hpp"

#include <onion/settings.hpp>

#include <string>

using onion::cheats::sync::CheatCatalogRegistry;
using onion::cheats::sync::CheatMirrorId;
using onion::cheats::sync::CheatMirrorPref;
using onion::cheats::sync::CheatMirrorFactory;

static int test_auto_zh_hans_prefers_cnb(void) {
  const auto &cat = CheatCatalogRegistry::primary();
  auto pick = CheatMirrorFactory::create(CheatMirrorPref::Auto,
                                         onion::kUiLanguageZhHans, 1);
  TEST_ASSERT_TRUE(pick.primary != nullptr);
  TEST_ASSERT_TRUE(pick.fallback != nullptr);
  TEST_ASSERT_STREQ("cnb", pick.primary->name());
  TEST_ASSERT_STREQ("github", pick.fallback->name());
  const std::string url = pick.primary->archiveUrl(cat);
  TEST_ASSERT_STREQ("https://cnb.cool/PS5-xiexiyu/CN-Cheats-Collection/-/git/"
                    "archive/refs/heads/chinese-build.zip",
                    url.c_str());
  TEST_ASSERT_STREQ("cnb.cool", pick.primary->archiveHost());
  return 0;
}

static int test_auto_english_prefers_github(void) {
  const auto &cat = CheatCatalogRegistry::primary();
  auto pick =
      CheatMirrorFactory::create(CheatMirrorPref::Auto, onion::kUiLanguageEn, 1);
  TEST_ASSERT_TRUE(pick.primary != nullptr);
  TEST_ASSERT_TRUE(pick.fallback != nullptr);
  TEST_ASSERT_STREQ("github", pick.primary->name());
  TEST_ASSERT_STREQ("cnb", pick.fallback->name());
  const std::string url = pick.primary->archiveUrl(cat);
  TEST_ASSERT_TRUE(url.rfind("https://codeload.github.com/", 0) == 0);
  TEST_ASSERT_TRUE(url.find(cat.slugFor(CheatMirrorId::Github)) !=
                   std::string::npos);
  TEST_ASSERT_STREQ("codeload.github.com", pick.primary->archiveHost());
  return 0;
}

static int test_auto_zh_hant_uses_github(void) {
  auto pick = CheatMirrorFactory::create(CheatMirrorPref::Auto,
                                         onion::kUiLanguageZhHant, 10);
  TEST_ASSERT_STREQ("github", pick.primary->name());
  TEST_ASSERT_TRUE(pick.fallback != nullptr);
  return 0;
}

static int test_explicit_has_no_fallback(void) {
  auto gh = CheatMirrorFactory::create(CheatMirrorPref::Github,
                                       onion::kUiLanguageZhHans, 11);
  TEST_ASSERT_STREQ("github", gh.primary->name());
  TEST_ASSERT_TRUE(gh.fallback == nullptr);

  auto cnb = CheatMirrorFactory::create(CheatMirrorPref::Cnb,
                                        onion::kUiLanguageEn, 1);
  TEST_ASSERT_STREQ("cnb", cnb.primary->name());
  TEST_ASSERT_TRUE(cnb.fallback == nullptr);
  return 0;
}

static int test_parse_pref_tokens(void) {
  TEST_ASSERT_TRUE(CheatMirrorFactory::parsePref("auto", CheatMirrorPref::Github) ==
                   CheatMirrorPref::Auto);
  TEST_ASSERT_TRUE(CheatMirrorFactory::parsePref("github", CheatMirrorPref::Auto) ==
                   CheatMirrorPref::Github);
  TEST_ASSERT_TRUE(CheatMirrorFactory::parsePref("cnb", CheatMirrorPref::Auto) ==
                   CheatMirrorPref::Cnb);
  TEST_ASSERT_TRUE(CheatMirrorFactory::parsePref("nope", CheatMirrorPref::Github) ==
                   CheatMirrorPref::Github);
  TEST_ASSERT_STREQ("auto", CheatMirrorFactory::prefName(CheatMirrorPref::Auto));
  return 0;
}

extern "C" int test_git_mirror_factory_suite(void) {
  int fails = 0;
  fails += onion_test_run("mirror.auto_zh_hans", test_auto_zh_hans_prefers_cnb);
  fails += onion_test_run("mirror.auto_en", test_auto_english_prefers_github);
  fails += onion_test_run("mirror.auto_zh_hant", test_auto_zh_hant_uses_github);
  fails += onion_test_run("mirror.explicit", test_explicit_has_no_fallback);
  fails += onion_test_run("mirror.parse", test_parse_pref_tokens);
  return fails;
}
