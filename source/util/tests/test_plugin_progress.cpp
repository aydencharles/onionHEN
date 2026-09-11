/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "test_harness.h"
#include "ps5_settings_ui.hpp"
#include "toolbox_route.hpp"
#include "toolbox_i18n.hpp"

#include <string>

static int test_plugin_progress_xml_generation(void) {
  const std::string title = toolbox_i18n::tr("plugins.external.progress_title");
  ps5ui::Page page("id_plugin_progress", title);
  page.root_list_size("1340,740")
      .root_restorable(false)
      .user_custom("id_plugin_progress_custom");
  const std::string xml = page.build();

  TEST_ASSERT_TRUE(!xml.empty());
  TEST_ASSERT_TRUE(xml.find("id=\"id_plugin_progress\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("restorable=\"false\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("list_size=\"1340,740\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_plugin_progress_custom\"") != std::string::npos);
  return 0;
}

static int test_plugin_progress_route(void) {
  using namespace toolbox;
  RouteInput in;
  in.resource = kPluginProgressXml;
  RouteResult r = resolve_resource(in);
  TEST_ASSERT_TRUE(r.page == Page::PluginProgress);
  TEST_ASSERT_TRUE(r.flags.is_plugin_progress);
  TEST_ASSERT_TRUE(restores_parent_on_pop(r.page));
  return 0;
}

extern "C" int test_plugin_progress_suite(void) {
  int fails = 0;
  fails += onion_test_run("plugin_progress.xml_generation",
                          test_plugin_progress_xml_generation);
  fails += onion_test_run("plugin_progress.route",
                          test_plugin_progress_route);
  return fails;
}
