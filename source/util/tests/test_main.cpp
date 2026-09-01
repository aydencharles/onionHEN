#include <cstdio>

extern "C" int test_cheat_utils_suite(void);
extern "C" int test_cheat_parsers_suite(void);
extern "C" int test_cheat_repository_suite(void);
extern "C" int test_cheat_flatten_suite(void);
extern "C" int test_payload_suite(void);
extern "C" int test_base64_suite(void);
extern "C" int test_aes_cbc_suite(void);
extern "C" int test_hde64_suite(void);
extern "C" int test_hotpatch_suite(void);
extern "C" int test_x64_relocator_suite(void);
extern "C" int test_git_mirror_factory_suite(void);
extern "C" int test_cheat_catalog_suite(void);
extern "C" int test_cheat_sync_suite(void);
extern "C" int test_reg_entity_suite(void);
extern "C" int test_settings_suite(void);
extern "C" int test_ready_suite(void);
extern "C" int test_platform_fs_suite(void);
extern "C" int test_platform_log_suite(void);
extern "C" int test_platform_net_suite(void);
extern "C" int test_platform_notify_suite(void);
extern "C" int test_util_language_suite(void);
extern "C" int test_msg_protocol_suite(void);
extern "C" int test_app_jailbreak_policy_suite(void);
extern "C" int test_debug_settings_route_policy_suite(void);
extern "C" int test_ipc_harden_suite(void);
extern "C" int test_toolbox_injection_suite(void);
extern "C" int test_trampoline_arena_suite(void);
extern "C" int test_hook_lifecycle_suite(void);
extern "C" int test_hijack_retry_suite(void);
extern "C" int test_ps5_settings_ui_suite(void);
extern "C" int test_toolbox_route_suite(void);
extern "C" int test_onpress_policy_suite(void);
extern "C" int test_toolbox_helpers_suite(void);
extern "C" int test_toolbox_i18n_suite(void);
extern "C" int test_overlay_text_metrics_suite(void);
extern "C" int test_overlay_layout_suite(void);
extern "C" int test_fps_formula_suite(void);
extern "C" int test_builtin_services_suite(void);
extern "C" int test_multipart_suite(void);
extern "C" int test_toolbox_pkg_xml_suite(void);

int main() {
  int failures = 0;

  failures += test_cheat_utils_suite();
  failures += test_cheat_parsers_suite();
  failures += test_cheat_repository_suite();
  failures += test_cheat_flatten_suite();
  failures += test_payload_suite();
  failures += test_base64_suite();
  failures += test_aes_cbc_suite();
  failures += test_hde64_suite();
  failures += test_hotpatch_suite();
  failures += test_x64_relocator_suite();
  failures += test_git_mirror_factory_suite();
  failures += test_cheat_catalog_suite();
  failures += test_cheat_sync_suite();
  failures += test_reg_entity_suite();
  failures += test_settings_suite();
  failures += test_ready_suite();
  failures += test_platform_fs_suite();
  failures += test_platform_log_suite();
  failures += test_platform_net_suite();
  failures += test_platform_notify_suite();
  failures += test_util_language_suite();
  failures += test_msg_protocol_suite();
  failures += test_app_jailbreak_policy_suite();
  failures += test_debug_settings_route_policy_suite();
  failures += test_ipc_harden_suite();
  failures += test_toolbox_injection_suite();
  failures += test_trampoline_arena_suite();
  failures += test_hook_lifecycle_suite();
  failures += test_hijack_retry_suite();
  failures += test_ps5_settings_ui_suite();
  failures += test_toolbox_route_suite();
  failures += test_onpress_policy_suite();
  failures += test_toolbox_helpers_suite();
  failures += test_toolbox_i18n_suite();
  failures += test_overlay_text_metrics_suite();
  failures += test_overlay_layout_suite();
  failures += test_fps_formula_suite();
  failures += test_builtin_services_suite();
  failures += test_multipart_suite();
  failures += test_toolbox_pkg_xml_suite();

  if (failures == 0) {
    std::fprintf(stderr, "All util host tests passed.\n");
    return 0;
  }

  std::fprintf(stderr, "%d util host tests failed.\n", failures);
  return 1;
}
