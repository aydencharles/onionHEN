/* Host unit tests for the reusable ShellUI lightweight file browser. */
#include "test_harness.h"

#include "file_browser.hpp"
#include "onpress_policy.hpp"
#include "pkg_installer.hpp"
#include "settings_page_update.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

#include <onion/fs.h>
#include <onion/tree.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace onion::shellui::file_browser;

namespace {

std::string make_temp_dir() {
  char tmpl[] = "/tmp/onion-file-browser-XXXXXX";
  char *created = ::mkdtemp(tmpl);
  return created ? std::string(created) : std::string();
}

bool write_text(const std::string &path, const std::string &content) {
  return write_file_atomic(path.c_str(), content.data(), content.size());
}

} // namespace

static int test_classification_and_filters(void) {
  TEST_ASSERT_TRUE(classify_file("Game.PKG") == FileKind::Pkg);
  TEST_ASSERT_TRUE(classify_file("ftpsrv.elf") == FileKind::Payload);
  TEST_ASSERT_TRUE(classify_file("PPSA12345.XML") == FileKind::Patch);
  TEST_ASSERT_TRUE(classify_file("CUSA00001_01.00.json") == FileKind::Patch);
  TEST_ASSERT_TRUE(classify_file("cheat.ShnExt") == FileKind::Patch);
  TEST_ASSERT_TRUE(classify_file("overlay.prx") == FileKind::Sprx);
  TEST_ASSERT_TRUE(classify_file("menu.SPRX") == FileKind::Sprx);
  TEST_ASSERT_TRUE(classify_file("readme.txt") == FileKind::Other);
  TEST_ASSERT_TRUE(classify_file(".hidden.pkg") == FileKind::Other);
  TEST_ASSERT_TRUE(classify_file("../escape.pkg") == FileKind::Other);

  TEST_ASSERT_TRUE(matches_filter(FileKind::Directory, FilterMode::Pkg));
  TEST_ASSERT_TRUE(matches_filter(FileKind::Pkg, FilterMode::Pkg));
  TEST_ASSERT_TRUE(!matches_filter(FileKind::Payload, FilterMode::Pkg));
  TEST_ASSERT_TRUE(matches_filter(FileKind::Payload, FilterMode::Payload));
  TEST_ASSERT_TRUE(matches_filter(FileKind::Patch, FilterMode::Patch));
  TEST_ASSERT_TRUE(matches_filter(FileKind::Other, FilterMode::All));

  TEST_ASSERT_TRUE(filter_mode_from_key("all") == FilterMode::All);
  TEST_ASSERT_TRUE(filter_mode_from_key("pkg") == FilterMode::Pkg);
  TEST_ASSERT_TRUE(filter_mode_from_key("payload") == FilterMode::Payload);
  TEST_ASSERT_TRUE(filter_mode_from_key("patch") == FilterMode::Patch);
  return 0;
}

static int test_list_filter_and_import(void) {
  const std::string tmp = make_temp_dir();
  TEST_ASSERT_TRUE(!tmp.empty());

  const std::string src_dir = join_path(tmp, "src");
  const std::string sub_dir = join_path(src_dir, "SubFolder");
  DestinationDirs dst;
  dst.payloads = join_path(tmp, "dst/payloads");
  dst.cheats = join_path(tmp, "dst/cheats");
  dst.sprx = join_path(tmp, "dst/sprx");

  TEST_ASSERT_TRUE(mkdir_tree(sub_dir.c_str()));
  TEST_ASSERT_TRUE(write_text(join_path(src_dir, "game.pkg"), "PKGDATA"));
  TEST_ASSERT_TRUE(write_text(join_path(src_dir, "tool.ELF"), "ELFDATA"));
  TEST_ASSERT_TRUE(write_text(join_path(src_dir, "PPSA00001.xml"), "<Patch/>"));
  TEST_ASSERT_TRUE(write_text(join_path(src_dir, "menu.prx"), "PRXDATA"));
  TEST_ASSERT_TRUE(write_text(join_path(src_dir, "notes.txt"), "hello"));
  TEST_ASSERT_TRUE(write_text(join_path(src_dir, ".ignored.pkg"), "skip"));

  const BrowserListing all_list = list_directory(src_dir, FilterMode::All, dst);
  TEST_ASSERT_TRUE(all_list.accessible);
  TEST_ASSERT_TRUE(!all_list.truncated);
  TEST_ASSERT_EQ_INT(6, static_cast<int>(all_list.entries.size()));
  TEST_ASSERT_TRUE(all_list.entries[0].kind == FileKind::Directory);
  TEST_ASSERT_STREQ("SubFolder", all_list.entries[0].name.c_str());

  const BrowserListing pkg_list = list_directory(src_dir, FilterMode::Pkg, dst);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(pkg_list.entries.size()));
  TEST_ASSERT_STREQ("SubFolder", pkg_list.entries[0].name.c_str());
  TEST_ASSERT_STREQ("game.pkg", pkg_list.entries[1].name.c_str());

  const BrowserListing patch_list =
      list_directory(src_dir, FilterMode::Patch, dst);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(patch_list.entries.size()));
  TEST_ASSERT_STREQ("PPSA00001.xml", patch_list.entries[1].name.c_str());

  std::string out_path;
  TEST_ASSERT_TRUE(import_file_to_destination(
                       join_path(src_dir, "PPSA00001.xml"), "PPSA00001.xml",
                       FileKind::Patch, dst, false, &out_path) ==
                   ImportStatus::Completed);
  TEST_ASSERT_TRUE(if_exists(join_path(dst.cheats, "PPSA00001.xml").c_str()));
  TEST_ASSERT_TRUE(import_file_to_destination(
                       join_path(src_dir, "PPSA00001.xml"), "PPSA00001.xml",
                       FileKind::Patch, dst, false, &out_path) ==
                   ImportStatus::AlreadyExists);
  TEST_ASSERT_TRUE(import_file_to_destination(
                       join_path(src_dir, "PPSA00001.xml"), "PPSA00001.xml",
                       FileKind::Patch, dst, true, &out_path) ==
                   ImportStatus::Replaced);
  TEST_ASSERT_TRUE(import_file_to_destination(
                       join_path(dst.cheats, "PPSA00001.xml"), "PPSA00001.xml",
                       FileKind::Patch, dst, true, &out_path) ==
                   ImportStatus::AlreadySameFile);

  TEST_ASSERT_TRUE(import_file_to_destination(
                       join_path(src_dir, "tool.ELF"), "tool.ELF",
                       FileKind::Payload, dst, false, &out_path) ==
                   ImportStatus::Completed);
  TEST_ASSERT_TRUE(if_exists(join_path(dst.payloads, "tool.elf").c_str()));

  TEST_ASSERT_TRUE(import_file_to_destination(
                       join_path(src_dir, "menu.prx"), "menu.prx",
                       FileKind::Sprx, dst, false, &out_path) ==
                   ImportStatus::Completed);
  TEST_ASSERT_TRUE(if_exists(join_path(dst.sprx, "menu.prx").c_str()));

  const BrowserListing after = list_directory(src_dir, FilterMode::Patch, dst);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(after.entries.size()));
  TEST_ASSERT_TRUE(after.entries[1].already_installed);

  (void)rmtree(tmp.c_str());
  return 0;
}

static int test_minimal_pages_and_routes(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);

  std::vector<RootLocation> roots = {
      {"Internal Storage", "/data", "/user/data", false, 0},
      {"USB 0", "/mnt/usb0", "/usb0", true, 0},
  };
  ps5ui::Page root_page("id_fb_page_root", "File Browser");
  fill_file_browser_root_page(
      root_page, roots, [](std::size_t idx, const std::string &) {
        return toolbox::file_browser_node_xml(idx + 1);
      });
  TEST_ASSERT_TRUE(
      onion::shellui::settings::valid_live_model(root_page.root()));
  TEST_ASSERT_EQ_INT(2, static_cast<int>(root_page.root().children.size()));
  const std::string root_xml = root_page.build();
  TEST_ASSERT_TRUE(root_xml.find("file=\"fb_1.xml\"") != std::string::npos);
  TEST_ASSERT_TRUE(root_xml.find("file=\"fb_2.xml\"") != std::string::npos);

  BrowserListing dir_listing{};
  dir_listing.accessible = true;
  dir_listing.entries = {
      {"pkgs", FileKind::Directory, 0, false},
      {"Alpha.pkg", FileKind::Pkg, 2ULL * 1024ULL * 1024ULL * 1024ULL, false},
  };
  ps5ui::Page dir_page("id_fb_page_1", "/data");
  fill_file_browser_dir_page(
      dir_page, 1, dir_listing, [](std::size_t, const std::string &) {
        return toolbox::file_browser_node_xml(9);
      });
  TEST_ASSERT_TRUE(onion::shellui::settings::valid_live_model(dir_page.root()));
  // Zero clutter: only the folder link and the file button.
  TEST_ASSERT_EQ_INT(2, static_cast<int>(dir_page.root().children.size()));
  const std::string dir_xml = dir_page.build();
  TEST_ASSERT_TRUE(dir_xml.find("file=\"fb_9.xml\"") != std::string::npos);
  TEST_ASSERT_TRUE(dir_xml.find("id=\"id_fb_file_1_1\"") != std::string::npos);
  TEST_ASSERT_TRUE(dir_xml.find("2.00 GB · Install PKG") != std::string::npos);

  const toolbox::ResourceNames names{"payloads.xml", "debug_settings.xml",
                                     "cheats.xml"};
  std::string mode;
  std::uint64_t token = 0;
  const std::string node_res =
      std::string(toolbox::kSettingsPluginsPrefix) + "fb_42.xml";
  TEST_ASSERT_TRUE(toolbox::parse_file_browser_node_resource(node_res, &token));
  TEST_ASSERT_EQ_U64(42, token);
  TEST_ASSERT_TRUE(
      !toolbox::parse_file_browser_node_resource(
          std::string(toolbox::kSettingsPluginsPrefix) + "fb_0.xml", nullptr));
  TEST_ASSERT_TRUE(
      !toolbox::parse_file_browser_node_resource(
          std::string(toolbox::kSettingsPluginsPrefix) + "fb_abc.xml", nullptr));

  for (const auto &[res, expected] :
       std::vector<std::pair<std::string_view, const char *>>{
           {toolbox::kFileBrowserXml, "all"},
           {toolbox::kFileBrowserPkgXml, "pkg"},
           {toolbox::kFileBrowserPayloadXml, "payload"},
           {toolbox::kFileBrowserPatchXml, "patch"},
           {node_res, "node"},
       }) {
    const auto r = toolbox::resolve_resource({res, names, false, false});
    TEST_ASSERT_TRUE(r.page == toolbox::Page::FileBrowser);
    TEST_ASSERT_TRUE(toolbox::restores_parent_on_pop(r.page));
    TEST_ASSERT_TRUE(toolbox::onpress_domain_for_page(r.page) ==
                     toolbox::OnPressDomain::FileBrowser);
    TEST_ASSERT_TRUE(toolbox::parse_file_browser_resource(res, &mode));
    TEST_ASSERT_STREQ(expected, mode.c_str());
  }
  return 0;
}

static int test_pkg_path_and_size_helpers(void) {
  TEST_ASSERT_STREQ("/data/pkg",
                    normalize_pkg_search_path("  /data/pkg///  ").c_str());
  TEST_ASSERT_STREQ("/data", normalize_pkg_search_path("/data/").c_str());

  TEST_ASSERT_TRUE(pkg_search_path_is_valid("/data"));
  TEST_ASSERT_TRUE(pkg_search_path_is_valid("/data/OnionHEN/pkgs"));
  TEST_ASSERT_TRUE(pkg_search_path_is_valid("/mnt/usb0/pkgs"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/etc/passwd"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/data/../etc"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/data/./pkgs"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("/data/a,b"));
  TEST_ASSERT_TRUE(!pkg_search_path_is_valid("data/pkgs"));

  TEST_ASSERT_STREQ("/user/data", pkg_search_path_shellui_view("/data").c_str());
  TEST_ASSERT_STREQ("/user/data/pkg",
                    pkg_search_path_shellui_view("/data/pkg").c_str());
  TEST_ASSERT_STREQ("/usb0/pkgs",
                    pkg_search_path_shellui_view("/mnt/usb0/pkgs").c_str());

  TEST_ASSERT_STREQ("512 B", format_byte_size(512).c_str());
  TEST_ASSERT_STREQ("1.5 KB", format_byte_size(1536).c_str());
  TEST_ASSERT_STREQ("10.0 MB",
                    format_byte_size(10ULL * 1024ULL * 1024ULL).c_str());
  TEST_ASSERT_STREQ("4.00 GB",
                    format_byte_size(4ULL * 1024ULL * 1024ULL * 1024ULL).c_str());
  return 0;
}

extern "C" int test_file_browser_suite(void) {
  int fails = 0;
  fails += onion_test_run("file_browser.classify_filter",
                          test_classification_and_filters);
  fails += onion_test_run("file_browser.list_and_import",
                          test_list_filter_and_import);
  fails += onion_test_run("file_browser.minimal_pages_and_routes",
                          test_minimal_pages_and_routes);
  fails += onion_test_run("file_browser.pkg_path_and_size",
                          test_pkg_path_and_size_helpers);
  return fails;
}
