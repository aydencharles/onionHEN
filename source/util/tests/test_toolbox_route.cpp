/* Host unit tests for toolbox::resolve_resource state machine (no PS5/Mono). */
#include "test_harness.h"

#include "onpress_policy.hpp"
#include "plugins_registry.hpp"
#include "remote_play.hpp"
#include "shellui_state.hpp"
#include "toolbox_route.hpp"

#include <cstring>
#include <string>

using namespace toolbox;

static ResourceNames test_names() {
  return ResourceNames{
      .payloads_xml = "payloads.xml",
      .debug_settings_xml = "debug_settings.xml",
      .cheats_xml = "cheats.xml",
  };
}

static RouteInput make_in(std::string_view resource, bool sc = false,
                          bool sc_not_open = false) {
  return RouteInput{
      .resource = resource,
      .names = test_names(),
      .cheats_shortcut = sc,
      .cheats_shortcut_not_open = sc_not_open,
  };
}

static int test_unknown_passthrough(void) {
  RouteResult r = resolve_resource(make_in("something.else.xml"));
  TEST_ASSERT_TRUE(r.page == Page::None);
  TEST_ASSERT_TRUE(!r.flags.is_payloads);
  TEST_ASSERT_TRUE(!r.flags.is_cheats);
  return 0;
}

static int test_payloads_page(void) {
  RouteResult r = resolve_resource(make_in("payloads.xml"));
  TEST_ASSERT_TRUE(r.page == Page::Payloads);
  TEST_ASSERT_TRUE(r.flags.is_payloads);
  TEST_ASSERT_TRUE(!r.flags.is_cheats);
  return 0;
}

static int test_debug_settings_page(void) {
  RouteResult r = resolve_resource(make_in("debug_settings.xml"));
  TEST_ASSERT_TRUE(r.page == Page::DebugSettings);
  TEST_ASSERT_TRUE(r.flags.is_debug_settings);
  return 0;
}

static int test_cheats_page(void) {
  RouteResult r = resolve_resource(make_in("cheats.xml"));
  TEST_ASSERT_TRUE(r.page == Page::Cheats);
  TEST_ASSERT_TRUE(r.flags.is_cheats);
  TEST_ASSERT_TRUE(r.clear_cheat_shortcuts_after);
  return 0;
}

static int test_auto_payloads_and_plapps(void) {
  RouteResult a = resolve_resource(make_in(kAutoPayloadsXml));
  TEST_ASSERT_TRUE(a.page == Page::AutoPayloads);
  TEST_ASSERT_TRUE(a.flags.is_auto_payload);

  RouteResult p = resolve_resource(make_in(kPlappsXml));
  TEST_ASSERT_TRUE(p.page == Page::Plapps);
  TEST_ASSERT_TRUE(p.flags.is_plapps);
  return 0;
}

static int test_plugins_page(void) {
  RouteResult r = resolve_resource(make_in(kPluginsXml));
  TEST_ASSERT_TRUE(r.page == Page::Plugins);
  TEST_ASSERT_TRUE(r.flags.is_plugins);
  TEST_ASSERT_TRUE(onpress_domain_for_page(r.page) == OnPressDomain::Plugins);
  return 0;
}

static int test_pkg_pages(void) {
  RouteResult i = resolve_resource(make_in(kPkgInstallerXml));
  TEST_ASSERT_TRUE(i.page == Page::PkgInstaller);
  TEST_ASSERT_TRUE(i.flags.is_pkg_installer);
  TEST_ASSERT_TRUE(onpress_domain_for_page(i.page) ==
                   OnPressDomain::PassThrough);

  RouteResult n = resolve_resource(make_in(kPkgNetXml));
  TEST_ASSERT_TRUE(n.page == Page::PkgNet);
  TEST_ASSERT_TRUE(n.flags.is_pkg_net);
  TEST_ASSERT_TRUE(onpress_domain_for_page(n.page) == OnPressDomain::PkgNet);
  return 0;
}

static std::string cfg_res(const char *rel) {
  return std::string(onion::plugins::kConfigResourcePrefix) + rel;
}

static int test_plugin_config_page(void) {
  RouteResult r = resolve_resource(make_in(cfg_res("shadowmount.xml")));
  TEST_ASSERT_TRUE(r.page == Page::PluginConfig);
  TEST_ASSERT_TRUE(r.flags.is_plugin_config);
  TEST_ASSERT_TRUE(onpress_domain_for_page(r.page) ==
                   OnPressDomain::PluginConfig);
  TEST_ASSERT_TRUE(restores_parent_on_pop(r.page));

  RouteResult unknown = resolve_resource(make_in(cfg_res("nope.xml")));
  TEST_ASSERT_TRUE(unknown.page == Page::None);
  return 0;
}

static int test_plugins_registry(void) {
  using namespace onion::plugins;

  TEST_ASSERT_EQ_INT(2, static_cast<int>(kRegistrySize));
  TEST_ASSERT_TRUE(find_by_key("shadowmount") != nullptr);
  TEST_ASSERT_TRUE(find_by_key("missing") == nullptr);
  TEST_ASSERT_TRUE(find_by_toggle_id("id_plugin_kstuff") != nullptr);
  TEST_ASSERT_TRUE(find_by_toggle_id("id_nope") == nullptr);
  TEST_ASSERT_STREQ("shadowmount", find_by_key("shadowmount")->key);
  TEST_ASSERT_STREQ("id_plugin_shadowmount",
                    find_by_key("shadowmount")->toggle_id);
  TEST_ASSERT_STREQ("shadowmount.xml", find_by_key("shadowmount")->config_xml);
  TEST_ASSERT_TRUE(default_key() == std::string_view("kstuff"));

  const Descriptor *by_sm =
      find_by_config_xml_resource(cfg_res("shadowmount.xml"));
  TEST_ASSERT_TRUE(by_sm != nullptr);
  TEST_ASSERT_STREQ("shadowmount", by_sm->key);
  TEST_ASSERT_TRUE(find_by_config_xml_resource(cfg_res("nope.xml")) == nullptr);
  return 0;
}

static int test_plugin_config_restores_parent(void) {
  ToolboxUiState state;
  state.set_active_page(Page::Plugins);
  state.set_active_page(Page::PluginConfig);
  TEST_ASSERT_TRUE(state.active_page == Page::PluginConfig);
  TEST_ASSERT_TRUE(state.parent_page == Page::Plugins);
  TEST_ASSERT_TRUE(state.child_page == Page::PluginConfig);

  state.leave_page(Page::PluginConfig);
  TEST_ASSERT_TRUE(state.active_page == Page::Plugins);
  TEST_ASSERT_TRUE(state.parent_page == Page::None);
  TEST_ASSERT_TRUE(state.child_page == Page::None);
  return 0;
}

static int test_account_page(void) {
  RouteResult r = resolve_resource(make_in(kAccountXml));
  TEST_ASSERT_TRUE(r.page == Page::Account);
  TEST_ASSERT_TRUE(r.flags.is_account);
  return 0;
}

static int test_cheat_progress_page(void) {
  RouteResult r = resolve_resource(make_in(kCheatProgressXml));
  TEST_ASSERT_TRUE(r.page == Page::CheatProgress);
  TEST_ASSERT_TRUE(r.flags.is_cheat_progress);
  return 0;
}

static int test_remote_play_page(void) {
  RouteResult r = resolve_resource(make_in(kRemotePlayXml));
  TEST_ASSERT_TRUE(r.page == Page::RemotePlay);
  TEST_ASSERT_TRUE(r.flags.is_remote_play);
  return 0;
}

static int test_superuser_pass_through(void) {
  RouteResult r = resolve_resource(make_in(kSuperuserXml));
  TEST_ASSERT_TRUE(r.page == Page::SuperuserPass);
  TEST_ASSERT_TRUE(r.flags.is_su_menu);
  return 0;
}

static int test_og_debug_redirect(void) {
  RouteResult r = resolve_resource(make_in(kOgDebugXml));
  TEST_ASSERT_TRUE(r.page == Page::RedirectOgDebug);
  return 0;
}

static int test_shortcut_force_cheats(void) {
  RouteResult r = resolve_resource(make_in("debug_settings.xml", true, false));
  TEST_ASSERT_TRUE(r.page == Page::Cheats);
  TEST_ASSERT_TRUE(r.shortcut_forced_cheats);
  TEST_ASSERT_TRUE(!r.flags.is_debug_settings);
  return 0;
}

static int test_shortcut_not_open(void) {
  RouteResult r = resolve_resource(make_in("debug_settings.xml", false, true));
  TEST_ASSERT_TRUE(r.page == Page::Cheats);
  TEST_ASSERT_TRUE(r.shortcut_forced_cheats);
  return 0;
}

static int test_matrix(void) {
  /* quick matrix: each named resource maps distinctly */
  TEST_ASSERT_TRUE(resolve_resource(make_in("payloads.xml")).page ==
                   Page::Payloads);
  TEST_ASSERT_TRUE(resolve_resource(make_in("cheats.xml")).page == Page::Cheats);
  TEST_ASSERT_TRUE(resolve_resource(make_in("debug_settings.xml")).page ==
                   Page::DebugSettings);
  return 0;
}

static int test_session_flags_clear(void) {
  RouteFlags f{};
  f.is_payloads = true;
  f.is_cheats = true;
  TEST_ASSERT_TRUE(f.is_payloads && f.is_cheats);
  f = RouteFlags{};
  TEST_ASSERT_TRUE(!f.is_payloads && !f.is_cheats);
  return 0;
}

static int test_progress_page_restores_parent(void) {
  ToolboxUiState state;
  state.set_active_page(Page::DebugSettings);
  state.set_active_page(Page::CheatProgress);
  TEST_ASSERT_TRUE(state.active_page == Page::CheatProgress);
  TEST_ASSERT_TRUE(state.parent_page == Page::DebugSettings);
  TEST_ASSERT_TRUE(state.child_page == Page::CheatProgress);

  /* Repeated resource loads must not replace the saved parent. */
  state.set_active_page(Page::CheatProgress);
  TEST_ASSERT_TRUE(state.parent_page == Page::DebugSettings);

  state.leave_page(Page::CheatProgress);
  TEST_ASSERT_TRUE(state.active_page == Page::DebugSettings);
  TEST_ASSERT_TRUE(state.parent_page == Page::None);
  TEST_ASSERT_TRUE(state.child_page == Page::None);
  TEST_ASSERT_TRUE(onpress_domain_for_page(state.active_page) ==
                   OnPressDomain::Root);
  return 0;
}

static int test_remote_play_restores_parent(void) {
  ToolboxUiState state;
  state.set_active_page(Page::DebugSettings);
  state.set_active_page(Page::RemotePlay);
  TEST_ASSERT_TRUE(state.active_page == Page::RemotePlay);
  TEST_ASSERT_TRUE(state.parent_page == Page::DebugSettings);
  TEST_ASSERT_TRUE(state.child_page == Page::RemotePlay);

  /* Repeated resource loads must not replace the saved parent. */
  state.set_active_page(Page::RemotePlay);
  TEST_ASSERT_TRUE(state.parent_page == Page::DebugSettings);

  state.leave_page(Page::RemotePlay);
  TEST_ASSERT_TRUE(state.active_page == Page::DebugSettings);
  TEST_ASSERT_TRUE(state.parent_page == Page::None);
  TEST_ASSERT_TRUE(state.child_page == Page::None);
  TEST_ASSERT_TRUE(onpress_domain_for_page(state.active_page) ==
                   OnPressDomain::Root);

  state.set_active_page(Page::RemotePlay);
  state.leave_page(Page::RemotePlay);
  TEST_ASSERT_TRUE(state.active_page == Page::DebugSettings);

  /* A stale/non-active pop must not alter the restored parent. */
  state.leave_page(Page::RemotePlay);
  TEST_ASSERT_TRUE(state.active_page == Page::DebugSettings);
  return 0;
}

static int test_remote_play_cancel_wins_terminal_race(void) {
  std::atomic<remote_play::PairingState> state{
      remote_play::PairingState::Waiting};
  TEST_ASSERT_TRUE(remote_play::try_finish_pairing(
      state, remote_play::PairingState::Cancelled));
  TEST_ASSERT_TRUE(!remote_play::try_finish_pairing(
      state, remote_play::PairingState::Paired));
  TEST_ASSERT_TRUE(state.load() == remote_play::PairingState::Cancelled);
  return 0;
}

static int test_remote_play_success_wins_terminal_race(void) {
  std::atomic<remote_play::PairingState> state{
      remote_play::PairingState::Waiting};
  TEST_ASSERT_TRUE(remote_play::try_finish_pairing(
      state, remote_play::PairingState::Paired));
  TEST_ASSERT_TRUE(!remote_play::try_finish_pairing(
      state, remote_play::PairingState::Cancelled));
  TEST_ASSERT_TRUE(state.load() == remote_play::PairingState::Paired);
  return 0;
}

static int test_progress_page_restore_is_reusable(void) {
  ToolboxUiState state;
  state.set_active_page(Page::DebugSettings);
  state.set_active_page(Page::CheatProgress);
  state.leave_page(Page::CheatProgress);

  state.set_active_page(Page::DebugSettings);
  state.set_active_page(Page::CheatProgress);
  state.leave_page(Page::CheatProgress);
  TEST_ASSERT_TRUE(state.active_page == Page::DebugSettings);

  /* A stale/non-active pop must not alter the current page. */
  state.leave_page(Page::CheatProgress);
  TEST_ASSERT_TRUE(state.active_page == Page::DebugSettings);
  return 0;
}

static int test_dynamic_cheat_state(void) {
  ToolboxUiState state;
  state.set_cheat_enabled(511, true);
  TEST_ASSERT_TRUE(state.get_cheat_enabled(511));
  TEST_ASSERT_TRUE(!state.get_cheat_enabled(512));
  TEST_ASSERT_TRUE(state.reset_cheats_if_tid_changed("CUSA00016"));
  TEST_ASSERT_TRUE(!state.get_cheat_enabled(511));
  return 0;
}

extern "C" int test_toolbox_route_suite(void) {
  int fails = 0;
  fails += onion_test_run("route.unknown", test_unknown_passthrough);
  fails += onion_test_run("route.payloads", test_payloads_page);
  fails += onion_test_run("route.debug", test_debug_settings_page);
  fails += onion_test_run("route.cheats", test_cheats_page);
  fails += onion_test_run("route.auto_plapps", test_auto_payloads_and_plapps);
  fails += onion_test_run("route.plugins", test_plugins_page);
  fails += onion_test_run("route.pkg_pages", test_pkg_pages);
  fails += onion_test_run("route.plugin_config", test_plugin_config_page);
  fails += onion_test_run("plugins.registry", test_plugins_registry);
  fails += onion_test_run("plugins.config_restores_parent",
                          test_plugin_config_restores_parent);
  fails += onion_test_run("route.account", test_account_page);
  fails += onion_test_run("route.cheat_progress", test_cheat_progress_page);
  fails += onion_test_run("route.remote_play", test_remote_play_page);
  fails += onion_test_run("route.superuser", test_superuser_pass_through);
  fails += onion_test_run("route.og_debug", test_og_debug_redirect);
  fails += onion_test_run("route.shortcut_force", test_shortcut_force_cheats);
  fails += onion_test_run("route.shortcut_not_open", test_shortcut_not_open);
  fails += onion_test_run("route.matrix", test_matrix);
  fails += onion_test_run("session.flags_clear", test_session_flags_clear);
  fails += onion_test_run("session.progress_restores_parent",
                          test_progress_page_restores_parent);
  fails += onion_test_run("session.progress_restore_reusable",
                          test_progress_page_restore_is_reusable);
  fails += onion_test_run("session.remote_play_restores_parent",
                          test_remote_play_restores_parent);
  fails += onion_test_run("remote_play.cancel_wins_terminal_race",
                          test_remote_play_cancel_wins_terminal_race);
  fails += onion_test_run("remote_play.success_wins_terminal_race",
                          test_remote_play_success_wins_terminal_race);
  fails += onion_test_run("cheatmap.dynamic", test_dynamic_cheat_state);
  return fails;
}
