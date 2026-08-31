/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Host tests for the package installer chooser and network installer toggle
 * XML generators (toolbox_pkg).
 */
#include "test_harness.h"

#include "toolbox_i18n.hpp"
#include "toolbox_pkg_xml.hpp"

#include <string>

static int test_pkg_installer_chooser(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);

  std::string xml;
  toolbox_pkg::generate_pkg_installer_xml(xml);

  TEST_ASSERT_TRUE(xml.find("<setting_list id=\"id_pkg_installer\" "
                            "title=\"Package Installer\">") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find("<link id=\"id_pkg_installer_usb\" "
               "title=\"Install content from USB\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"PkgInstaller/data/pkginstaller.xml\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<link id=\"id_pkg_installer_network\" "
                            "title=\"Install content from Network\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"pkg_net.xml\"") != std::string::npos);
  /* The chooser is navigation-only: no toggles, no buttons. */
  TEST_ASSERT_TRUE(xml.find("toggle_switch") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<button ") == std::string::npos);
  return 0;
}

static int test_pkg_net_toggles(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);

  std::string xml;
  toolbox_pkg::generate_pkg_net_xml(xml, /*run_on=*/true,
                                     /*autoload_on=*/false);

  TEST_ASSERT_TRUE(xml.find("<setting_list id=\"id_pkg_net\" "
                            "title=\"Network Installer\">") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<toggle_switch id=\"id_pkgnet_run\" "
                            "title=\"Run network installer now\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Start or stop it for this "
                            "OnionHEN session only\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<toggle_switch id=\"id_pkgnet_autoload\" "
                            "title=\"Start network installer with OnionHEN\"") !=
                   std::string::npos);
  /* Toggle values reflect the passed initial states. */
  const std::string run_tag = xml.substr(
      xml.find("<toggle_switch id=\"id_pkgnet_run\""), 220);
  TEST_ASSERT_TRUE(run_tag.find("value=\"1\"") != std::string::npos);
  const std::string autoload_tag = xml.substr(
      xml.find("<toggle_switch id=\"id_pkgnet_autoload\""), 200);
  TEST_ASSERT_TRUE(autoload_tag.find("value=\"0\"") != std::string::npos);
  /* No info label, no port field. */
  TEST_ASSERT_TRUE(xml.find("<label ") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("text_field") == std::string::npos);
  return 0;
}

extern "C" int test_toolbox_pkg_xml_suite(void) {
  int fails = 0;
  fails += onion_test_run("pkg_xml.chooser", test_pkg_installer_chooser);
  fails += onion_test_run("pkg_xml.net_toggles", test_pkg_net_toggles);
  return fails;
}
