/* Host unit tests for the SPRX loader policy layer (no PS5 SDK). */
#include "test_harness.h"
#include "test_support.h"

#include <onion/sprx_loader.hpp>

#include <cstdio>
#include <limits.h>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

struct FakeRuntime final : onion::sprx::IRemoteSprxRuntime {
  std::vector<onion::sprx::LoadStatus> responses;
  std::string received_path;
  int find_calls = 0;
  int load_calls = 0;
  size_t response_index = 0;
  bool loaded = false;
  bool cleanup_ok = true;

  bool find_loaded(pid_t, std::string_view,
                   onion::sprx::ModuleInfo *out) const noexcept override {
    auto *self = const_cast<FakeRuntime *>(this);
    ++self->find_calls;
    if (!loaded)
      return false;
    if (out)
      out->handle = 42;
    return true;
  }

  onion::sprx::LoadResult load(pid_t pid, std::string_view path,
                               std::string_view,
                               const onion::sprx::LoadOptions &) noexcept override {
    ++load_calls;
    received_path.assign(path.data(), path.size());
    const auto status = response_index < responses.size()
                            ? responses[response_index++]
                            : onion::sprx::LoadStatus::Loaded;
    onion::sprx::LoadResult result;
    result.status = status;
    result.pid = pid;
    result.module_handle = status == onion::sprx::LoadStatus::Loaded ? 42 : 0;
    result.remote_result = status == onion::sprx::LoadStatus::Loaded ? 0 : -1;
    result.cleanup_ok = cleanup_ok;
    if (status == onion::sprx::LoadStatus::Loaded)
      loaded = true;
    return result;
  }
};

struct FakeAccess final : onion::sprx::ITargetAccessPolicy {
  onion::sprx::AccessStatus prepare_status = onion::sprx::AccessStatus::Allowed;
  onion::sprx::AccessStatus restore_status = onion::sprx::AccessStatus::Allowed;
  int prepares = 0;
  int restores = 0;

  onion::sprx::AccessResult prepare(pid_t, std::string_view) noexcept override {
    ++prepares;
    return {prepare_status};
  }

  onion::sprx::AccessResult restore(pid_t, std::string_view) noexcept override {
    ++restores;
    return {restore_status};
  }
};

bool make_request_file(std::string *path) {
  char raw[256] = {};
  if (onion_test_write_temp_text_file(".sprx", "test", raw, sizeof(raw)) != 0)
    return false;
  *path = raw;
  return true;
}

onion::sprx::LoadRequest request_for(pid_t pid, const std::string &path) {
  onion::sprx::LoadRequest request;
  request.pid = pid;
  request.title_id = "CUSA12345";
  request.path = path;
  request.options.retry_delay_ms = 0;
  return request;
}

int test_allowlist_and_canonicalize() {
  std::string path;
  TEST_ASSERT_TRUE(make_request_file(&path));
  const std::string link = path + ".link";
  unlink(link.c_str());
  TEST_ASSERT_EQ_INT(0, symlink(path.c_str(), link.c_str()));

  onion::sprx::TitleIdAllowlist allowlist;
  TEST_ASSERT_TRUE(allowlist.add_exact("CUSA12345"));
  FakeRuntime runtime;
  FakeAccess access;
  onion::sprx::SprxLoader loader(runtime, access, allowlist);
  auto result = loader.load(request_for(123, link));

  TEST_ASSERT_TRUE(result.succeeded());
  TEST_ASSERT_EQ_INT(1, result.attempts);
  char resolved[PATH_MAX] = {};
  TEST_ASSERT_TRUE(realpath(path.c_str(), resolved) != nullptr);
  TEST_ASSERT_STREQ(resolved, runtime.received_path.c_str());
  TEST_ASSERT_EQ_INT(1, access.prepares);
  TEST_ASSERT_EQ_INT(1, access.restores);
  unlink(link.c_str());
  unlink(path.c_str());
  return 0;
}

int test_title_allowlist_denies_before_runtime() {
  std::string path;
  TEST_ASSERT_TRUE(make_request_file(&path));
  onion::sprx::TitleIdAllowlist allowlist;
  TEST_ASSERT_TRUE(allowlist.add_exact("CUSA99999"));
  FakeRuntime runtime;
  FakeAccess access;
  onion::sprx::SprxLoader loader(runtime, access, allowlist);
  const auto result = loader.load(request_for(123, path));
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::sprx::LoadStatus::PolicyDenied),
                     static_cast<int>(result.status));
  TEST_ASSERT_EQ_INT(0, runtime.load_calls);
  TEST_ASSERT_EQ_INT(0, access.prepares);
  unlink(path.c_str());
  return 0;
}

int test_retry_and_failure_state() {
  std::string path;
  TEST_ASSERT_TRUE(make_request_file(&path));
  onion::sprx::TitleIdAllowlist allowlist;
  TEST_ASSERT_TRUE(allowlist.add_prefix("CUSA"));
  FakeRuntime runtime;
  runtime.responses = {onion::sprx::LoadStatus::Timeout,
                       onion::sprx::LoadStatus::AttachFailed,
                       onion::sprx::LoadStatus::Loaded};
  FakeAccess access;
  onion::sprx::SprxLoader loader(runtime, access, allowlist);
  auto request = request_for(123, path);
  request.options.max_attempts = 3;
  const auto result = loader.load(request);
  TEST_ASSERT_TRUE(result.succeeded());
  TEST_ASSERT_EQ_INT(3, result.attempts);
  TEST_ASSERT_EQ_INT(3, runtime.load_calls);
  TEST_ASSERT_EQ_INT(3, access.prepares);
  TEST_ASSERT_EQ_INT(3, access.restores);
  unlink(path.c_str());
  return 0;
}

int test_retry_exhausted_preserves_cause() {
  std::string path;
  TEST_ASSERT_TRUE(make_request_file(&path));
  onion::sprx::TitleIdAllowlist allowlist;
  TEST_ASSERT_TRUE(allowlist.add_exact("CUSA12345"));
  FakeRuntime runtime;
  runtime.responses = {onion::sprx::LoadStatus::Timeout,
                       onion::sprx::LoadStatus::Timeout};
  FakeAccess access;
  onion::sprx::SprxLoader loader(runtime, access, allowlist);
  auto request = request_for(123, path);
  request.options.max_attempts = 2;
  const auto result = loader.load(request);
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::sprx::LoadStatus::RetryExhausted),
                     static_cast<int>(result.status));
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::sprx::LoadStatus::Timeout),
                     static_cast<int>(result.underlying_status));
  TEST_ASSERT_EQ_INT(2, result.attempts);
  unlink(path.c_str());
  return 0;
}

int test_unresolved_cleanup_stops_retry() {
  std::string path;
  TEST_ASSERT_TRUE(make_request_file(&path));
  onion::sprx::TitleIdAllowlist allowlist;
  TEST_ASSERT_TRUE(allowlist.add_exact("CUSA12345"));
  FakeRuntime runtime;
  runtime.responses = {onion::sprx::LoadStatus::Timeout,
                       onion::sprx::LoadStatus::Loaded};
  runtime.cleanup_ok = false;
  FakeAccess access;
  onion::sprx::SprxLoader loader(runtime, access, allowlist);
  auto request = request_for(123, path);
  request.options.max_attempts = 2;
  const auto result = loader.load(request);
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::sprx::LoadStatus::Timeout),
                     static_cast<int>(result.status));
  TEST_ASSERT_EQ_INT(1, result.attempts);
  TEST_ASSERT_EQ_INT(1, runtime.load_calls);
  TEST_ASSERT_TRUE(!result.cleanup_ok);
  unlink(path.c_str());
  return 0;
}

int test_access_prepare_failure() {
  std::string path;
  TEST_ASSERT_TRUE(make_request_file(&path));
  onion::sprx::TitleIdAllowlist allowlist;
  TEST_ASSERT_TRUE(allowlist.add_exact("CUSA12345"));
  FakeRuntime runtime;
  FakeAccess access;
  access.prepare_status = onion::sprx::AccessStatus::Denied;
  onion::sprx::SprxLoader loader(runtime, access, allowlist);
  const auto result = loader.load(request_for(123, path));
  TEST_ASSERT_EQ_INT(static_cast<int>(onion::sprx::LoadStatus::PolicyDenied),
                     static_cast<int>(result.status));
  TEST_ASSERT_EQ_INT(0, runtime.load_calls);
  TEST_ASSERT_EQ_INT(1, access.prepares);
  TEST_ASSERT_EQ_INT(0, access.restores);
  unlink(path.c_str());
  return 0;
}

} // namespace

extern "C" int test_sprx_loader_suite(void) {
  int failures = 0;
  failures += onion_test_run("sprx_allowlist_canonicalize",
                             test_allowlist_and_canonicalize);
  failures += onion_test_run("sprx_title_allowlist_denies",
                             test_title_allowlist_denies_before_runtime);
  failures += onion_test_run("sprx_retry_state", test_retry_and_failure_state);
  failures += onion_test_run("sprx_retry_exhausted", test_retry_exhausted_preserves_cause);
  failures += onion_test_run("sprx_unresolved_cleanup", test_unresolved_cleanup_stops_retry);
  failures += onion_test_run("sprx_access_prepare_failure",
                             test_access_prepare_failure);
  return failures;
}
