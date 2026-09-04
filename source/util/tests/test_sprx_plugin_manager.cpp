#include "test_harness.h"
#include "test_support.h"

#include <onion/sprx_plugin_manager.hpp>

#include <string>
#include <vector>
#include <unistd.h>

namespace {

struct FakeRuntime final : onion::sprx::IRemoteSprxRuntime {
  std::vector<onion::sprx::LoadStatus> responses;
  size_t index = 0;
  int calls = 0;

  bool find_loaded(pid_t, std::string_view,
                   onion::sprx::ModuleInfo *) const noexcept override {
    return false;
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

} // namespace

extern "C" int test_sprx_plugin_manager_suite(void) {
  return onion_test_run("sprx_manager_dependency_failure_isolated",
                        test_dependency_failure_isolated);
}
