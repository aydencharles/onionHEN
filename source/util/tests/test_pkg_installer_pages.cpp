#include "test_harness.h"

#include "onpress_policy.hpp"
#include "pkg_installer.hpp"
#include "pkg_installer_pages.hpp"
#include "settings_page_update.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

#include <string>
#include <vector>

using namespace onion::shellui::pkg_pages;

static int test_path_validation_and_normalization(void) {
  const std::string trimmed = normalize_pkg_search_path("  /data/pkg///  ");
  TEST_ASSERT_STREQ("/data/pkg", trimmed.c_str());

  const std::string data_root = normalize_pkg_search_path("/data/");
  TEST_ASSERT_STREQ("/data", data_root.c_str());

  TEST_ASSERT_TRUE(pkg_search_path_is_valid("/data"));
  TEST_ASSERT_TRUE(pkg_search_path_is_valid("/data/OnionHEN/pkgs"));
  TEST_ASSERT_TRUE(pkg_search_path_is_valid("/mnt/usb0/pkgs"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/etc/passwd"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/data/../etc"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/data/./pkgs"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/data/a,b"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("data/pkgs"));

  const std::string v_data = pkg_search_path_shellui_view("/data");
  const std::string v_sub = pkg_search_path_shellui_view("/data/pkg");
  const std::string v_mnt = pkg_search_path_shellui_view("/mnt/usb0/pkgs");
  TEST_ASSERT_STREQ("/user/data", v_data.c_str());
  TEST_ASSERT_STREQ("/user/data/pkg", v_sub.c_str());
  TEST_ASSERT_STREQ("/usb0/pkgs", v_mnt.c_str());
  return 0;
}

static int test_size_and_subtitle_formatting(void) {
  const std::string s_b = format_pkg_size(512);
  const std::string s_kb = format_pkg_size(1536);
  const std::string s_mb = format_pkg_size(10ULL * 1024ULL * 1024ULL);
  const std::string s_gb = format_pkg_size(4ULL * 1024ULL * 1024ULL * 1024ULL);
  TEST_ASSERT_STREQ("512 B", s_b.c_str());
  TEST_ASSERT_STREQ("1.5 KB", s_kb.c_str());
  TEST_ASSERT_STREQ("10.0 MB", s_mb.c_str());
  TEST_ASSERT_STREQ("4.00 GB", s_gb.c_str());

  PkgInstallerEntry entry;
  entry.name = "game.pkg";
  entry.search_path = "/data/OnionHEN/pkgs";
  entry.size_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  const std::string sub = format_pkg_subtitle(entry);
  TEST_ASSERT_STREQ("2.00 GB · /data/OnionHEN/pkgs", sub.c_str());
  return 0;
}

static int test_main_pkg_page_is_flat_and_live_refreshable(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  PkgInstallerScan scan{};
  scan.accessible = true;
  scan.folders = {
      {"/data/OnionHEN/pkgs", true, 2},
      {"/data/pkg", true, 0},
  };
  scan.entries = {
      {"Alpha.pkg", "/user/data/OnionHEN/pkgs/Alpha.pkg",
       "/data/OnionHEN/pkgs/Alpha.pkg", "/data/OnionHEN/pkgs",
       512ULL * 1024ULL * 1024ULL},
      {"Beta.pkg", "/user/data/OnionHEN/pkgs/Beta.pkg",
       "/data/OnionHEN/pkgs/Beta.pkg", "/data/OnionHEN/pkgs",
       2ULL * 1024ULL * 1024ULL * 1024ULL},
  };

  ps5ui::Page page("custom_pkg_installer", "Custom PKG Installer");
  fill_custom_pkg_installer_page(page, scan, scan.folders.size());

  // Must be flat and valid for in-place settings::reconcile live refresh.
  TEST_ASSERT_TRUE(onion::shellui::settings::valid_live_model(page.root()));

  const std::string xml = page.build();
  const auto p0 = xml.find("id=\"id_pkg_install_0\"");
  const auto p1 = xml.find("id=\"id_pkg_install_1\"");
  const auto rescan = xml.find("id=\"id_pkg_rescan\"");
  const auto folders = xml.find("id=\"id_pkg_folders\"");
  TEST_ASSERT_TRUE(p0 != std::string::npos);
  TEST_ASSERT_TRUE(p1 != std::string::npos);
  TEST_ASSERT_TRUE(rescan != std::string::npos);
  TEST_ASSERT_TRUE(folders != std::string::npos);
  TEST_ASSERT_TRUE(p0 < p1);
  TEST_ASSERT_TRUE(p1 < rescan);
  TEST_ASSERT_TRUE(rescan < folders);

  TEST_ASSERT_TRUE(xml.find("512.0 MB · /data/OnionHEN/pkgs") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("2.00 GB · /data/OnionHEN/pkgs") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"custom_pkg_folders.xml\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("2 folder(s) · 2 PKG(s)") != std::string::npos);
  // Direct install on click (no confirmation prompt on PKG install buttons).
  TEST_ASSERT_TRUE(xml.find("confirm=") == std::string::npos);
  return 0;
}

static int test_folders_page_live_refreshable_and_confirm_remove(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  PkgInstallerScan scan{};
  scan.accessible = true;
  scan.inaccessible_paths = 1;
  scan.folders = {
      {"/data/OnionHEN/pkgs", true, 3},
      {"/data/missing", false, 0},
  };

  const std::array<std::string, 3> quick_paths = {
      "/data/OnionHEN/pkgs",
      "/data/pkg",
      "/data/games",
  };
  ps5ui::Page page("custom_pkg_folders", "Search folders");
  fill_custom_pkg_folders_page(page, scan, quick_paths.data(),
                               quick_paths.size(), {1, 2});

  // Must be flat and live-refreshable (no TextField directly in this page).
  TEST_ASSERT_TRUE(onion::shellui::settings::valid_live_model(page.root()));

  const std::string xml = page.build();
  TEST_ASSERT_TRUE(xml.find("id=\"id_pkg_remove_0\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_pkg_remove_1\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"3 PKG(s)\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Folder unavailable\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("confirm=\"Remove /data/OnionHEN/pkgs") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_pkg_quick_add_1\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("Add /data/pkg") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_pkg_quick_add_2\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("Add /data/games") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_pkg_add_custom\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"custom_pkg_add_folder.xml\"") !=
                   std::string::npos);
  return 0;
}

static int test_add_folder_page_and_routes(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  ps5ui::Page page("custom_pkg_add_folder", "Add custom folder...");
  fill_custom_pkg_add_folder_page(page, "/data/games");
  const std::string xml = page.build();
  TEST_ASSERT_TRUE(xml.find("id=\"id_pkg_path_input\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("value=\"/data/games\"") != std::string::npos);

  const toolbox::ResourceNames names{"payloads.xml", "debug_settings.xml",
                                     "cheats.xml"};
  const auto r_main = toolbox::resolve_resource(
      {toolbox::kCustomPkgInstallerXml, names, false, false});
  const auto r_folders = toolbox::resolve_resource(
      {toolbox::kCustomPkgFoldersXml, names, false, false});
  const auto r_add = toolbox::resolve_resource(
      {toolbox::kCustomPkgAddFolderXml, names, false, false});
  TEST_ASSERT_TRUE(r_main.page == toolbox::Page::CustomPkgInstaller);
  TEST_ASSERT_TRUE(r_folders.page == toolbox::Page::CustomPkgFolders);
  TEST_ASSERT_TRUE(r_add.page == toolbox::Page::CustomPkgAddFolder);
  TEST_ASSERT_TRUE(toolbox::restores_parent_on_pop(r_folders.page));
  TEST_ASSERT_TRUE(toolbox::restores_parent_on_pop(r_add.page));
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(r_folders.page) ==
                   toolbox::OnPressDomain::CustomPkgInstaller);
  TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(r_add.page) ==
                   toolbox::OnPressDomain::CustomPkgInstaller);
  return 0;
}

extern "C" int test_pkg_installer_pages_suite(void) {
  int fails = 0;
  fails += onion_test_run("pkg_pages.path_validation",
                          test_path_validation_and_normalization);
  fails += onion_test_run("pkg_pages.size_subtitle",
                          test_size_and_subtitle_formatting);
  fails += onion_test_run("pkg_pages.main_flat_live",
                          test_main_pkg_page_is_flat_and_live_refreshable);
  fails += onion_test_run("pkg_pages.folders_live_confirm",
                          test_folders_page_live_refreshable_and_confirm_remove);
  fails += onion_test_run("pkg_pages.add_page_and_routes",
                          test_add_folder_page_and_routes);
  return fails;
}
