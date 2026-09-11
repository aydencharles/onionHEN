/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/sprx_loader.hpp>
#include <onion/log.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::sprx {
namespace {

bool has_component_parent(std::string_view path) noexcept {
  size_t start = 0;
  while (start < path.size()) {
    const size_t slash = path.find('/', start);
    const size_t end = slash == std::string_view::npos ? path.size() : slash;
    if (path.substr(start, end - start) == "..")
      return true;
    start = slash == std::string_view::npos ? path.size() : slash + 1;
  }
  return false;
}

bool suffix_equals_ci(std::string_view value, std::string_view suffix) noexcept {
  if (suffix.size() > value.size())
    return false;
  const size_t offset = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    const auto a = static_cast<unsigned char>(value[offset + i]);
    const auto b = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(a) != std::tolower(b))
      return false;
  }
  return true;
}

bool valid_title_id(std::string_view title_id) noexcept {
  if (title_id.size() < 4 || title_id.size() > 16)
    return false;
  for (const unsigned char c : title_id) {
    if (!std::isalnum(c) && c != '-' && c != '_')
      return false;
  }
  return true;
}

bool canonicalize_file(std::string_view path, std::string *out) {
  if (!out || path.empty() || path.size() >= PATH_MAX)
    return false;
  char resolved[PATH_MAX] = {};
  const std::string input(path.data(), path.size());
  if (!realpath(input.c_str(), resolved))
    return false;
  struct stat st {};
  if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode))
    return false;
  *out = resolved;
  return true;
}

bool retryable(LoadStatus status) noexcept {
  switch (status) {
  case LoadStatus::TargetNotFound:
  case LoadStatus::AttachFailed:
  case LoadStatus::ResolveFailed:
  case LoadStatus::AllocationFailed:
  case LoadStatus::ProtectFailed:
  case LoadStatus::ThreadCreateFailed:
  case LoadStatus::Timeout:
  case LoadStatus::TargetExited:
  case LoadStatus::RemoteError:
    return true;
  default:
    return false;
  }
}

std::string_view basename_of(std::string_view path) noexcept {
  const size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

LoadResult invalid(LoadStatus status, pid_t pid) noexcept {
  LoadResult result;
  result.status = status;
  result.pid = pid;
  result.underlying_status = status;
  return result;
}

} // namespace

const char *load_status_name(LoadStatus status) noexcept {
  switch (status) {
  case LoadStatus::Loaded: return "loaded";
  case LoadStatus::AlreadyLoaded: return "already-loaded";
  case LoadStatus::InvalidArgument: return "invalid-argument";
  case LoadStatus::InvalidPath: return "invalid-path";
  case LoadStatus::CanonicalizeFailed: return "canonicalize-failed";
  case LoadStatus::PolicyDenied: return "policy-denied";
  case LoadStatus::AccessPrepareFailed: return "access-prepare-failed";
  case LoadStatus::AccessRestoreFailed: return "access-restore-failed";
  case LoadStatus::TargetNotFound: return "target-not-found";
  case LoadStatus::AttachFailed: return "attach-failed";
  case LoadStatus::ResolveFailed: return "resolve-failed";
  case LoadStatus::AllocationFailed: return "allocation-failed";
  case LoadStatus::WriteFailed: return "write-failed";
  case LoadStatus::ProtectFailed: return "protect-failed";
  case LoadStatus::ThreadCreateFailed: return "thread-create-failed";
  case LoadStatus::Timeout: return "timeout";
  case LoadStatus::TargetExited: return "target-exited";
  case LoadStatus::RetryExhausted: return "retry-exhausted";
  case LoadStatus::CleanupFailed: return "cleanup-failed";
  case LoadStatus::RemoteError: return "remote-error";
  }
  return "unknown";
}

SprxLoader::SprxLoader(IRemoteSprxRuntime &runtime,
                       ITargetAccessPolicy &access_policy,
                       const TitleIdAllowlist &allowlist) noexcept
    : runtime_(runtime), access_policy_(access_policy), allowlist_(allowlist) {}

bool TitleIdAllowlist::add_exact(std::string_view title_id) {
  if (!valid_title_id(title_id))
    return false;
  exact_.emplace_back(title_id);
  return true;
}

bool TitleIdAllowlist::add_prefix(std::string_view prefix) {
  if (!valid_title_id(prefix))
    return false;
  prefixes_.emplace_back(prefix);
  return true;
}

bool TitleIdAllowlist::allows(std::string_view title_id) const noexcept {
  if (!valid_title_id(title_id))
    return false;
  for (const std::string &exact : exact_) {
    if (exact == title_id)
      return true;
  }
  for (const std::string &prefix : prefixes_) {
    if (title_id.size() >= prefix.size() &&
        title_id.compare(0, prefix.size(), prefix) == 0)
      return true;
  }
  return false;
}

LoadResult SprxLoader::load(const LoadRequest &request) noexcept {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  const auto reject = [&](LoadStatus status) noexcept {
    LOG_WARN("sprx: request rejected pid=%d tid=%s path=%s status=%s",
             static_cast<int>(request.pid), request.title_id.c_str(),
             request.path.c_str(), load_status_name(status));
    return invalid(status, request.pid);
  };

  if (request.pid <= 1 || request.path.empty())
    return reject(LoadStatus::InvalidArgument);
  if (!valid_title_id(request.title_id) ||
      !allowlist_.allows(request.title_id))
    return reject(LoadStatus::PolicyDenied);
  if (request.options.timeout_ms == 0 || request.options.poll_ms == 0 ||
      request.options.max_attempts == 0 || request.options.max_attempts > 10 ||
      request.options.poll_ms > request.options.timeout_ms)
    return reject(LoadStatus::InvalidArgument);
  if (request.path.front() != '/' || request.path.size() > PATH_MAX ||
      has_component_parent(request.path))
    return reject(LoadStatus::InvalidPath);

  std::string canonical_path;
  if (!canonicalize_file(request.path, &canonical_path))
    return reject(LoadStatus::CanonicalizeFailed);

  const std::string_view module_name = basename_of(canonical_path);
  if (module_name.empty() || module_name.size() > 255 ||
      (!suffix_equals_ci(module_name, ".sprx") &&
       !suffix_equals_ci(module_name, ".prx")))
    return reject(LoadStatus::InvalidPath);

  ModuleInfo loaded;
  if (runtime_.find_loaded(request.pid, module_name, &loaded)) {
    LoadResult result;
    result.status = LoadStatus::AlreadyLoaded;
    result.pid = request.pid;
    result.module_handle = loaded.handle;
    result.attempts = 1;
    result.underlying_status = result.status;
    LOG_INFO("sprx: module already loaded pid=%d tid=%s module=%u name=%.*s",
             static_cast<int>(request.pid), request.title_id.c_str(),
             loaded.handle, static_cast<int>(module_name.size()),
             module_name.data());
    return result;
  }

  LoadResult last = invalid(LoadStatus::RemoteError, request.pid);
  for (uint32_t attempt = 1; attempt <= request.options.max_attempts; ++attempt) {
    LOG_INFO("sprx: load attempt=%u/%u pid=%d tid=%s path=%s",
             attempt, request.options.max_attempts, static_cast<int>(request.pid),
             request.title_id.c_str(), canonical_path.c_str());

    const AccessResult prepared =
        access_policy_.prepare(request.pid, request.title_id);
    if (!prepared.allowed()) {
      last = invalid(prepared.status == AccessStatus::Denied
                         ? LoadStatus::PolicyDenied
                         : LoadStatus::AccessPrepareFailed,
                     request.pid);
      last.attempts = attempt;
      LOG_WARN("sprx: target access denied pid=%d tid=%s status=%s",
               static_cast<int>(request.pid), request.title_id.c_str(),
               load_status_name(last.status));
      break;
    }

    last = runtime_.load(request.pid, canonical_path, module_name,
                         request.options);
    last.attempts = attempt;
    const AccessResult restored =
        access_policy_.restore(request.pid, request.title_id);
    if (!restored.allowed()) {
      last.status = LoadStatus::AccessRestoreFailed;
      LOG_ERROR("sprx: target access restore failed pid=%d tid=%s",
                static_cast<int>(request.pid), request.title_id.c_str());
      break;
    }

    LOG_INFO("sprx: load attempt=%u status=%s pid=%d module=%u remote=%lld cleanup=%d",
             attempt, load_status_name(last.status), static_cast<int>(request.pid),
             last.module_handle, static_cast<long long>(last.remote_result),
             last.cleanup_ok ? 1 : 0);
    if (last.succeeded() || !retryable(last.status) || !last.cleanup_ok) {
      if (!last.cleanup_ok)
        LOG_ERROR("sprx: stopping retries because cleanup is unresolved pid=%d",
                  static_cast<int>(request.pid));
      break;
    }
    if (attempt < request.options.max_attempts) {
      const uint64_t delay_us =
          static_cast<uint64_t>(request.options.retry_delay_ms) * 1000ULL;
      if (delay_us != 0)
        usleep(static_cast<useconds_t>(std::min<uint64_t>(delay_us, 5000000ULL)));
    }
  }

  if (!last.succeeded() && retryable(last.status) &&
      last.attempts >= request.options.max_attempts) {
    const LoadStatus cause = last.status;
    last.status = LoadStatus::RetryExhausted;
    last.underlying_status = cause;
    LOG_ERROR("sprx: retry exhausted pid=%d tid=%s cause=%s attempts=%u",
              static_cast<int>(request.pid), request.title_id.c_str(),
              load_status_name(cause), last.attempts);
  } else {
    last.underlying_status = last.status;
  }
  return last;
}

} // namespace onion::sprx
