#include "test_harness.h"

#include "plugin_sprx_pages.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_route.hpp"

#include <string>

using namespace onion::shellui::plugin_pages;

static int test_plugin_list_is_flat_links(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  std::vector<PluginInventoryItem> plugins = {
      {.plugin_id = "FTPS00001",
       .version = "1.00",
       .name = "FTP Server",
       .running = true,
       .auto_start = true},
      {.plugin_id = "DPIV00001",
       .version = "1.00",
       .name = "DPI",
       .running = false,
       .auto_start = false},
  };

  ps5ui::Page page("id_plugins", "Plugins");
  append_plugin_list_links(page, plugins);
  const std::string xml = page.build();

  TEST_ASSERT_TRUE(xml.find("id=\"id_external_plugins\"") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("setting_list id=\"id_external_plugin_") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find("file=\"plugin_FTPS00001.xml\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"plugin_DPIV00001.xml\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Running · Auto-start\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Stopped\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id_external_plugin_run_") == std::string::npos);
  return 0;
}

static int test_plugin_config_order(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  const PluginInventoryItem plugin{.plugin_id = "FTPS00001",
                                   .version = "1.00",
                                   .name = "FTP Server",
                                   .running = true,
                                   .auto_start = false};
  onion::shellui::dynamic_ui::PluginSettingsLink settings{
      .plugin_id = "FTPS00001",
      .control_id = "id_settings",
      .title = "FTP Settings",
      .resource = "onion_ui_ftp.xml",
      .description = "Ports and auth",
  };

  ps5ui::Page page("id_plugin_config", plugin.name);
  fill_plugin_config(page, plugin, &settings);
  const std::string xml = page.build();

  const auto run_at = xml.find("id=\"id_external_plugin_run_FTPS00001\"");
  const auto auto_at =
      xml.find("id=\"id_external_plugin_autostart_FTPS00001\"");
  const auto settings_at =
      xml.find("id=\"id_external_plugin_settings_FTPS00001\"");
  const auto delete_at = xml.find("id=\"id_external_plugin_delete_FTPS00001\"");
  TEST_ASSERT_TRUE(run_at != std::string::npos);
  TEST_ASSERT_TRUE(auto_at != std::string::npos);
  TEST_ASSERT_TRUE(settings_at != std::string::npos);
  TEST_ASSERT_TRUE(delete_at != std::string::npos);
  TEST_ASSERT_TRUE(run_at < auto_at);
  TEST_ASSERT_TRUE(auto_at < settings_at);
  TEST_ASSERT_TRUE(settings_at < delete_at);
  TEST_ASSERT_TRUE(xml.find("file=\"onion_ui_ftp.xml\"") != std::string::npos);
  return 0;
}

static int test_sprx_list_is_flat_links_and_sorted(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  std::vector<SprxInventoryItem> sprx = {
      {.id = "other",
       .path = "/data/OnionHEN/sprx/other.sprx",
       .enabled = true,
       .auto_start = true,
       .configured = true,
       .target_summary = "CUSA99999",
       .current_target_title_id = "PPSA01234"},
      {.id = "rawmod",
       .path = "/data/OnionHEN/sprx/rawmod.sprx",
       .enabled = false,
       .auto_start = false,
       .configured = false,
       .current_target_title_id = "PPSA01234"},
      {.id = "fps.overlay",
       .path = "/data/OnionHEN/sprx/fps.sprx",
       .enabled = true,
       .auto_start = true,
       .matches_current_target = true,
       .configured = true,
       .target_summary = "PPSA01234",
       .current_target_title_id = "PPSA01234"},
      {.id = "overlay",
       .path = "/data/OnionHEN/sprx/overlay.sprx",
       .enabled = true,
       .auto_start = true,
       .matches_current_target = true,
       .loaded_for_current_target = true,
       .configured = true,
       .target_summary = "PPSA01234",
       .current_target_title_id = "PPSA01234"},
  };

  ps5ui::Page page("id_sprx", "SPRX");
  append_sprx_list_links(page, sprx);
  const std::string xml = page.build();

  TEST_ASSERT_TRUE(xml.find("id=\"id_external_sprx\"") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_external_sprx_target_ctx\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("Current game: PPSA01234") != std::string::npos);

  const auto pos_loaded = xml.find("file=\"sprx_overlay.xml\"");
  const auto pos_match = xml.find("file=\"sprx_fps.overlay.xml\"");
  const auto pos_unconf = xml.find("file=\"sprx_rawmod.xml\"");
  const auto pos_other = xml.find("file=\"sprx_other.xml\"");
  TEST_ASSERT_TRUE(pos_loaded != std::string::npos);
  TEST_ASSERT_TRUE(pos_match != std::string::npos);
  TEST_ASSERT_TRUE(pos_unconf != std::string::npos);
  TEST_ASSERT_TRUE(pos_other != std::string::npos);
  TEST_ASSERT_TRUE(pos_loaded < pos_match);
  TEST_ASSERT_TRUE(pos_match < pos_unconf);
  TEST_ASSERT_TRUE(pos_unconf < pos_other);

  TEST_ASSERT_TRUE(xml.find("second_title=\"Loaded · Auto-start\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find("second_title=\"Matches current game · Auto-start\"") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find("second_title=\"Unconfigured · Ready to load\"") !=
      std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Other game · CUSA99999\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id_external_sprx_enabled_") == std::string::npos);
  return 0;
}

static int test_sprx_config_configured_layout(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  const SprxInventoryItem entry{.id = "overlay",
                                .path = "/data/OnionHEN/sprx/overlay.sprx",
                                .enabled = false,
                                .auto_start = true,
                                .priority = 50,
                                .matches_current_target = true,
                                .loaded_for_current_target = false,
                                .configured = true,
                                .target_summary = "CUSA12345",
                                .current_target_title_id = "CUSA12345",
                                .dependencies = {"base"}};

  ps5ui::Page page("id_sprx_config", entry.id);
  fill_sprx_config(page, entry);
  const std::string xml = page.build();

  const auto load_at = xml.find("id=\"id_external_sprx_load_overlay\"");
  const auto enabled_at = xml.find("id=\"id_external_sprx_enabled_overlay\"");
  const auto auto_at = xml.find("id=\"id_external_sprx_autostart_overlay\"");
  const auto bind_cur_at =
      xml.find("id=\"id_external_sprx_bind_current_overlay\"");
  const auto target_at = xml.find("id=\"id_external_sprx_target_overlay\"");
  const auto path_at = xml.find("id=\"id_external_sprx_path_overlay\"");
  const auto prio_at = xml.find("id=\"id_external_sprx_priority_overlay\"");
  const auto deps_at = xml.find("id=\"id_external_sprx_deps_overlay\"");
  const auto delete_at = xml.find("id=\"id_external_sprx_delete_overlay\"");

  TEST_ASSERT_TRUE(load_at != std::string::npos);
  TEST_ASSERT_TRUE(enabled_at != std::string::npos);
  TEST_ASSERT_TRUE(auto_at != std::string::npos);
  TEST_ASSERT_TRUE(bind_cur_at != std::string::npos);
  TEST_ASSERT_TRUE(target_at != std::string::npos);
  TEST_ASSERT_TRUE(path_at != std::string::npos);
  TEST_ASSERT_TRUE(prio_at != std::string::npos);
  TEST_ASSERT_TRUE(deps_at != std::string::npos);
  TEST_ASSERT_TRUE(delete_at != std::string::npos);

  TEST_ASSERT_TRUE(load_at < enabled_at);
  TEST_ASSERT_TRUE(enabled_at < auto_at);
  TEST_ASSERT_TRUE(auto_at < bind_cur_at);
  TEST_ASSERT_TRUE(bind_cur_at < target_at);
  TEST_ASSERT_TRUE(target_at < path_at);
  TEST_ASSERT_TRUE(path_at < prio_at);
  TEST_ASSERT_TRUE(prio_at < deps_at);
  TEST_ASSERT_TRUE(deps_at < delete_at);

  TEST_ASSERT_TRUE(xml.find("Target: CUSA12345") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("Path: /data/OnionHEN/sprx/overlay.sprx") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("Priority: 50") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("Dependencies: base") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("confirm_phrase=") != std::string::npos);
  return 0;
}

static int test_sprx_config_unconfigured_and_loaded(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  const SprxInventoryItem unconfigured{
      .id = "rawmod",
      .path = "/data/OnionHEN/sprx/rawmod.sprx",
      .configured = false,
      .current_target_title_id = "PPSA01234"};
  ps5ui::Page p1("id_sprx_config", unconfigured.id);
  fill_sprx_config(p1, unconfigured);
  const std::string xml1 = p1.build();
  TEST_ASSERT_TRUE(xml1.find("id=\"id_external_sprx_load_rawmod\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml1.find("id=\"id_external_sprx_bind_current_rawmod\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml1.find("id=\"id_external_sprx_bind_all_rawmod\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml1.find("id=\"id_external_sprx_enabled_rawmod\"") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(xml1.find("id=\"id_external_sprx_delete_rawmod\"") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(xml1.find("Target: Not configured") != std::string::npos);

  const SprxInventoryItem loaded{
      .id = "overlay",
      .path = "/data/OnionHEN/sprx/overlay.sprx",
      .enabled = true,
      .auto_start = true,
      .matches_current_target = true,
      .loaded_for_current_target = true,
      .configured = true,
      .target_summary = "PPSA01234",
      .current_target_title_id = "PPSA01234"};
  ps5ui::Page p2("id_sprx_config", loaded.id);
  fill_sprx_config(p2, loaded);
  const std::string xml2 = p2.build();
  TEST_ASSERT_TRUE(xml2.find("id=\"id_external_sprx_load_overlay\"") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(xml2.find("Loaded in the current game (cannot unload)") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(
      xml2.find("Loaded in the current game; close the game before removing") !=
      std::string::npos);
  return 0;
}

extern "C" int test_plugin_sprx_pages_suite(void) {
  int fails = 0;
  fails += onion_test_run("plugin_pages.flat_list", test_plugin_list_is_flat_links);
  fails += onion_test_run("plugin_pages.config_order", test_plugin_config_order);
  fails += onion_test_run("sprx_pages.flat_list_sorted",
                          test_sprx_list_is_flat_links_and_sorted);
  fails += onion_test_run("sprx_pages.config_configured",
                          test_sprx_config_configured_layout);
  fails += onion_test_run("sprx_pages.config_unconfigured_and_loaded",
                          test_sprx_config_unconfigured_and_loaded);
  return fails;
}
