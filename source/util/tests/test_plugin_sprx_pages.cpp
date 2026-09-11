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

static int test_sprx_list_is_flat_links(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  std::vector<SprxInventoryItem> sprx = {
      {.id = "overlay",
       .path = "/data/OnionHEN/sprx/overlay.sprx",
       .enabled = true,
       .loaded_for_current_target = true},
      {.id = "fps.overlay",
       .path = "/data/OnionHEN/sprx/fps.sprx",
       .enabled = true,
       .matches_current_target = true},
  };

  ps5ui::Page page("id_sprx", "SPRX");
  append_sprx_list_links(page, sprx);
  const std::string xml = page.build();

  TEST_ASSERT_TRUE(xml.find("id=\"id_external_sprx\"") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("setting_list id=\"id_external_sprx_") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"sprx_overlay.xml\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"sprx_fps.overlay.xml\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("second_title=\"Loaded in the current game\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find(
          "second_title=\"Matches the current game; loads at its next start\"") !=
      std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id_external_sprx_enabled_") == std::string::npos);
  return 0;
}

static int test_sprx_config_has_status_and_path(void) {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  const SprxInventoryItem entry{.id = "overlay",
                                .path = "/data/OnionHEN/sprx/overlay.sprx",
                                .enabled = false,
                                .matches_current_target = true};

  ps5ui::Page page("id_sprx_config", entry.id);
  fill_sprx_config(page, entry);
  const std::string xml = page.build();

  TEST_ASSERT_TRUE(xml.find("id=\"id_external_sprx_status_overlay\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("/data/OnionHEN/sprx/overlay.sprx") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_external_sprx_enabled_overlay\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("value=\"0\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_external_sprx_delete_overlay\"") !=
                   std::string::npos);
  return 0;
}

extern "C" int test_plugin_sprx_pages_suite(void) {
  int fails = 0;
  fails += onion_test_run("plugin_pages.flat_list", test_plugin_list_is_flat_links);
  fails += onion_test_run("plugin_pages.config_order", test_plugin_config_order);
  fails += onion_test_run("sprx_pages.flat_list", test_sprx_list_is_flat_links);
  fails += onion_test_run("sprx_pages.config_status",
                          test_sprx_config_has_status_and_path);
  return fails;
}
