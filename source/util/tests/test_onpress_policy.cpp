/* Host tests for page-scoped ShellUI OnPress ownership (no PS5/Mono). */
#include "test_harness.h"

#include "onpress_policy.hpp"

using toolbox::OnPressDomain;
using toolbox::Page;

static int test_stock_package_installer_is_passthrough(void) {
  Page active = Page::DebugSettings;
  active = toolbox::active_page_after_resource(
      active, Page::None, "PkgInstaller/data/pkginstaller.xml");

  TEST_ASSERT_TRUE(active == Page::None);
  TEST_ASSERT_TRUE(!toolbox::toolbox_owns_settings_page(active));
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(active) ==
                   OnPressDomain::PassThrough);
  return 0;
}

static int test_unrelated_resource_keeps_toolbox_ownership(void) {
  Page active = toolbox::active_page_after_resource(
      Page::DebugSettings, Page::None, "PkgInstaller/assets/icon.png");

  TEST_ASSERT_TRUE(active == Page::DebugSettings);
  TEST_ASSERT_TRUE(toolbox::toolbox_owns_settings_page(active));
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(active) ==
                   OnPressDomain::Root);
  return 0;
}

static int test_page_domain_matrix(void) {
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::None) ==
                   OnPressDomain::PassThrough);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::DebugSettings) ==
                   OnPressDomain::Root);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::Payloads) ==
                   OnPressDomain::Payloads);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::PayloadConfig) ==
                   OnPressDomain::PayloadConfig);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::AutoPayloads) ==
                   OnPressDomain::AutoPayloads);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::Cheats) ==
                   OnPressDomain::Cheats);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::Account) ==
                   OnPressDomain::Account);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::CheatProgress) ==
                   OnPressDomain::Progress);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::SuperuserPass) ==
                   OnPressDomain::PassThrough);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::RedirectOgDebug) ==
                   OnPressDomain::PassThrough);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::Plugins) ==
                   OnPressDomain::Plugins);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::PluginConfig) ==
                   OnPressDomain::PluginConfig);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::Sprx) ==
                   OnPressDomain::Sprx);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(Page::SprxConfig) ==
                   OnPressDomain::Sprx);
  return 0;
}

extern "C" int test_onpress_policy_suite(void) {
  int fails = 0;
  fails += onion_test_run("onpress.stock_pkg_passthrough",
                          test_stock_package_installer_is_passthrough);
  fails += onion_test_run("onpress.ignore_unrelated_resource",
                          test_unrelated_resource_keeps_toolbox_ownership);
  fails += onion_test_run("onpress.page_domain_matrix", test_page_domain_matrix);
  return fails;
}
