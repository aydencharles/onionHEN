/* Copyright (C) 2026 OnionHEN / LightningMods */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <sys/types.h>

namespace onion::sprx {

enum class LoadStatus : uint8_t {
  Loaded,
  AlreadyLoaded,
  InvalidArgument,
  InvalidPath,
  CanonicalizeFailed,
  PolicyDenied,
  AccessPrepareFailed,
  AccessRestoreFailed,
  TargetNotFound,
  AttachFailed,
  ResolveFailed,
  AllocationFailed,
  WriteFailed,
  ProtectFailed,
  ThreadCreateFailed,
  Timeout,
  TargetExited,
  RetryExhausted,
  CleanupFailed,
  RemoteError,
};

const char *load_status_name(LoadStatus status) noexcept;

struct LoadOptions {
  uint32_t timeout_ms = 5000;
  uint32_t poll_ms = 10;
  uint32_t max_attempts = 3;
  uint32_t retry_delay_ms = 100;
};

struct LoadRequest {
  pid_t pid = -1;
  std::string title_id;
  std::string path;
  LoadOptions options{};
};

struct LoadResult {
  LoadStatus status = LoadStatus::InvalidArgument;
  pid_t pid = -1;
  uint32_t module_handle = 0;
  int64_t remote_result = -1;
  bool cleanup_ok = true;
  uint32_t attempts = 0;
  LoadStatus underlying_status = LoadStatus::InvalidArgument;

  bool succeeded() const noexcept {
    return status == LoadStatus::Loaded || status == LoadStatus::AlreadyLoaded;
  }
};

struct ModuleInfo {
  uint32_t handle = 0;
};

enum class AccessStatus : uint8_t {
  Allowed,
  Denied,
  PrepareFailed,
  RestoreFailed,
};

struct AccessResult {
  AccessStatus status = AccessStatus::Denied;

  bool allowed() const noexcept { return status == AccessStatus::Allowed; }
};

/** Target privilege/sandbox policy. The loader never elevates arbitrary PIDs. */
class ITargetAccessPolicy {
public:
  virtual ~ITargetAccessPolicy() = default;

  virtual AccessResult prepare(pid_t pid,
                               std::string_view title_id) noexcept = 0;
  virtual AccessResult restore(pid_t pid,
                               std::string_view title_id) noexcept = 0;
};

/** Explicit opt-in adapter for already-prepared targets. */
class NoopTargetAccessPolicy final : public ITargetAccessPolicy {
public:
  AccessResult prepare(pid_t, std::string_view) noexcept override {
    return {AccessStatus::Allowed};
  }

  AccessResult restore(pid_t, std::string_view) noexcept override {
    return {AccessStatus::Allowed};
  }
};

/** Exact/prefix Title ID allowlist used before any target-side operation. */
class TitleIdAllowlist final {
public:
  bool add_exact(std::string_view title_id);
  bool add_prefix(std::string_view prefix);
  bool allows(std::string_view title_id) const noexcept;
  bool empty() const noexcept { return exact_.empty() && prefixes_.empty(); }

private:
  std::vector<std::string> exact_;
  std::vector<std::string> prefixes_;
};

/** Low-level transport. Implementations own ptrace, remote memory and calls. */
class IRemoteSprxRuntime {
public:
  virtual ~IRemoteSprxRuntime() = default;

  virtual bool find_loaded(pid_t pid, std::string_view module_name,
                           ModuleInfo *out) const noexcept = 0;

  virtual LoadResult load(pid_t pid, std::string_view path,
                          std::string_view module_name,
                          const LoadOptions &options) noexcept = 0;
};

/** Public use-case boundary; policy is independent from ptrace details. */
class ISprxLoader {
public:
  virtual ~ISprxLoader() = default;
  virtual LoadResult load(const LoadRequest &request) noexcept = 0;
};

class SprxLoader final : public ISprxLoader {
public:
  SprxLoader(IRemoteSprxRuntime &runtime, ITargetAccessPolicy &access_policy,
             const TitleIdAllowlist &allowlist) noexcept;

  LoadResult load(const LoadRequest &request) noexcept override;

private:
  IRemoteSprxRuntime &runtime_;
  ITargetAccessPolicy &access_policy_;
  const TitleIdAllowlist &allowlist_;
};

/** Production PS5 runtime backed by the shared ptrace/credential primitives. */
class PtraceSprxRuntime final : public IRemoteSprxRuntime {
public:
  bool find_loaded(pid_t pid, std::string_view module_name,
                   ModuleInfo *out) const noexcept override;

  LoadResult load(pid_t pid, std::string_view path,
                  std::string_view module_name,
                  const LoadOptions &options) noexcept override;
};

} // namespace onion::sprx
