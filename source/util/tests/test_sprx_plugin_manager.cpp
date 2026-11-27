#include "test_harness.h"
#include "test_support.h"

#include <onion/sprx_plugin_manager.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

struct FakeRuntime final : onion::sprx::IRemoteSprxRuntime {
  std::vector<onion::sprx::LoadStatus> responses;
  size_t index = 0;
  int calls = 0;
  std::string loaded_module;

  bool find_loaded(pid_t, std::string_view module_name,
                   onion::sprx::ModuleInfo *) const noexcept override {
    return module_name == loaded_module;
  }

  onion::sprx::LoadResult load(pid_t pid, std::string_view,
                               std::string_view,
                               const onion::sprx::LoadOptions &) noexcept override {
    ++calls;
    onion::sprx::LoadResult result;
    result.pid = pid;
    result.status = index < responses.size()
                        ? responses[index++]
                        : onion::sprx::LoadStatus::Loaded;
    result.underlying_status = result.status;
    result.remote_result = result.succeeded() ? 0 : -1;
    return result;
  }
};

struct FakeAccess final : onion::sprx::ITargetAccessPolicy {
  onion::sprx::AccessResult prepare(pid_t,
                                    std::string_view) noexcept override {
    return {onion::sprx::AccessStatus::Allowed};
  }
  onion::sprx::AccessResult restore(pid_t,
                                    std::string_view) noexcept override {
    return {onion::sprx::AccessStatus::Allowed};
  }
};

struct FakeTarget final : onion::sprx::ISprxTargetProvider {
  bool current(onion::sprx::SprxTarget *out) noexcept override {
    if (!out)
      return false;
    out->pid = 123;
    out->title_id = "CUSA12345";
    return true;
  }
};

int test_dependency_failure_isolated() {
  char path[256] = {};
  char base_path[256] = {};
  char child_path[256] = {};
  char independent_path[256] = {};
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".sprx", "base", base_path, sizeof(base_path)));
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".sprx", "child", child_path, sizeof(child_path)));
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".sprx", "independent", independent_path,
                            sizeof(independent_path)));

  const std::string manifest =
      "[plugin.base]\npath=" + std::string(base_path) +
      "\nexact_title_ids=CUSA12345\nauto_start=true\n"
      "[plugin.child]\npath=" + std::string(child_path) +
      "\nexact_title_ids=CUSA12345\nauto_start=true\ndependencies=base\n"
      "[plugin.independent]\npath=" + std::string(independent_path) +
      "\nexact_title_ids=CUSA12345\nauto_start=true\n";
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".ini", manifest.c_str(), path, sizeof(path)));
  FakeRuntime runtime;
  runtime.responses = {onion::sprx::LoadStatus::Timeout,
                       onion::sprx::LoadStatus::Timeout,
                       onion::sprx::LoadStatus::Timeout,
                       onion::sprx::LoadStatus::Loaded};
  FakeAccess access;
  FakeTarget target;
  onion::sprx::SprxPluginManager manager(runtime, access, target);
  std::vector<onion::sprx::SprxCatalogIssue> issues;
  TEST_ASSERT_TRUE(manager.load_catalog(path, &issues));
  const auto report = manager.start();
  TEST_ASSERT_EQ_INT(3, static_cast<int>(report.results.size()));
  TEST_ASSERT_EQ_INT(4, runtime.calls);
  TEST_ASSERT_TRUE(!report.results[0].load.succeeded());
  TEST_ASSERT_TRUE(report.results[1].skipped_dependency);
  TEST_ASSERT_TRUE(report.results[2].load.succeeded());
  unlink(path);
  unlink(base_path);
  unlink(child_path);
  unlink(independent_path);
  return 0;
}

int test_inventory_enable_and_remove_lifecycle() {
  char manifest_path[256] = {};
  char module_path[256] = {};
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".sprx", "module", module_path, sizeof(module_path)));
  const std::string manifest =
      "[plugin.overlay]\npath=" + std::string(module_path) +
      "\nexact_title_ids=CUSA12345\nauto_start=true\n";
  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_text_file(
                            ".ini", manifest.c_str(), manifest_path,
                            sizeof(manifest_path)));
  FakeRuntime runtime;
  FakeAccess access;
  FakeTarget target;
  onion::sprx::SprxPluginManager manager(runtime, access, target);
  TEST_ASSERT_TRUE(manager.load_catalog(manifest_path));

  const auto before = manager.inventory();
  TEST_ASSERT_EQ_INT(1, static_cast<int>(before.size()));
  TEST_ASSERT_TRUE(before[0].matches_current_target);
  TEST_ASSERT_TRUE(!before[0].loaded_for_current_target);
  TEST_ASSERT_TRUE(manager.set_enabled("overlay", false));
  const auto disabled_report = manager.start();
  TEST_ASSERT_EQ_INT(0, static_cast<int>(disabled_report.results.size()));

  const size_t slash = std::string(module_path).find_last_of('/');
  runtime.loaded_module = std::string(module_path).substr(slash + 1);
  TEST_ASSERT_TRUE(!manager.remove("overlay"));
  runtime.loaded_module.clear();
  TEST_ASSERT_TRUE(manager.remove("overlay"));
  TEST_ASSERT_TRUE(manager.inventory().empty());
  unlink(manifest_path);
  unlink(module_path);
  return 0;
}

struct ToggleableTarget final : onion::sprx::ISprxTargetProvider {
  bool running = true;
  std::string title_id = "PPSA01234";
  bool current(onion::sprx::SprxTarget *out) noexcept override {
    if (!running || !out)
      return false;
    out->pid = 456;
    out->title_id = title_id;
    return true;
  }
};

int test_discovery_bind_and_load_now() {
  char dir_tpl[] = "/tmp/onion_sprx_test_XXXXXX";
  char *dir = mkdtemp(dir_tpl);
  TEST_ASSERT_TRUE(dir != nullptr);

  const std::string scan_dir(dir);
  const std::string catalog_path = scan_dir + "/catalog.ini";
  const std::string mod_a = scan_dir + "/overlay.sprx";
  const std::string mod_b = scan_dir + "/helper.prx";
  const std::string ignored = scan_dir + "/readme.txt";
  const std::string hidden = scan_dir + "/.hidden.sprx";

  auto write_file = [](const std::string &p, const char *data) {
    FILE *fp = std::fopen(p.c_str(), "wb");
    if (!fp)
      return false;
    std::fputs(data, fp);
    std::fclose(fp);
    return true;
  };
  TEST_ASSERT_TRUE(write_file(mod_a, "a"));
  TEST_ASSERT_TRUE(write_file(mod_b, "b"));
  TEST_ASSERT_TRUE(write_file(ignored, "txt"));
  TEST_ASSERT_TRUE(write_file(hidden, "hid"));

  FakeRuntime runtime;
  FakeAccess access;
  ToggleableTarget target;
  onion::sprx::SprxPluginManager manager(runtime, access, target);
  manager.set_scan_directory(scan_dir);
  // Catalog does not exist yet; load_catalog still records path cleanly.
  TEST_ASSERT_TRUE(!manager.load_catalog(catalog_path));

  const auto discovered = manager.inventory();
  TEST_ASSERT_EQ_INT(2, static_cast<int>(discovered.size()));
  TEST_ASSERT_STREQ("helper", discovered[0].id.c_str());
  TEST_ASSERT_TRUE(!discovered[0].configured);
  TEST_ASSERT_STREQ("PPSA01234", discovered[0].current_target_title_id.c_str());
  TEST_ASSERT_STREQ("overlay", discovered[1].id.c_str());
  TEST_ASSERT_TRUE(!discovered[1].configured);

  // Direct on-demand load of an unconfigured discovered module works.
  const auto load_unconfigured = manager.load_now("overlay");
  TEST_ASSERT_TRUE(load_unconfigured.success);
  TEST_ASSERT_EQ_INT(1, runtime.calls);

  // Bind to current game creates catalog.ini and configures the entry.
  const auto bind_cur =
      manager.bind_target("overlay", onion::sprx::SprxTargetScope::CurrentGame);
  TEST_ASSERT_TRUE(bind_cur.success);

  // Bind helper to all games.
  const auto bind_all =
      manager.bind_target("helper", onion::sprx::SprxTargetScope::AllGames);
  TEST_ASSERT_TRUE(bind_all.success);

  // Reload catalog and verify both are now configured and no duplicates exist.
  TEST_ASSERT_TRUE(manager.reload_catalog().success);
  const auto after_bind = manager.inventory();
  TEST_ASSERT_EQ_INT(2, static_cast<int>(after_bind.size()));
  TEST_ASSERT_TRUE(after_bind[0].configured);
  TEST_ASSERT_TRUE(after_bind[1].configured);
  TEST_ASSERT_STREQ("PPSA01234", after_bind[0].target_summary.c_str());
  TEST_ASSERT_STREQ("PPSA*, CUSA*", after_bind[1].target_summary.c_str());

  // Toggle auto_start independently of enabled.
  TEST_ASSERT_TRUE(manager.set_auto_start("overlay", false).success);
  const auto after_auto = manager.inventory();
  TEST_ASSERT_TRUE(after_auto[0].enabled);
  TEST_ASSERT_TRUE(!after_auto[0].auto_start);

  // When no game is running, load_now and bind(CurrentGame) fail cleanly.
  target.running = false;
  TEST_ASSERT_TRUE(!manager.load_now("overlay").success);
  TEST_ASSERT_TRUE(!manager
                        .bind_target("overlay",
                                     onion::sprx::SprxTargetScope::CurrentGame)
                        .success);

  unlink(catalog_path.c_str());
  unlink(mod_a.c_str());
  unlink(mod_b.c_str());
  unlink(ignored.c_str());
  unlink(hidden.c_str());
  rmdir(dir);
  return 0;
}

} // namespace

extern "C" int test_sprx_plugin_manager_suite(void) {
  int failures = 0;
  failures += onion_test_run("sprx_manager_dependency_failure_isolated",
                             test_dependency_failure_isolated);
  failures += onion_test_run("sprx_manager_inventory_enable_remove",
                             test_inventory_enable_and_remove_lifecycle);
  failures += onion_test_run("sprx_manager_discovery_bind_load_now",
                             test_discovery_bind_and_load_now);
  return failures;
}
