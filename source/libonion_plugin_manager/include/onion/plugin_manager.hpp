#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <set>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace onion::plugin {

inline constexpr uint32_t kAbiVersion = 1;
inline constexpr size_t kIdCapacity = 32;
inline constexpr size_t kVersionCapacity = 16;
inline constexpr size_t kNameCapacity = 64;
inline constexpr size_t kDescriptorV1Size = 128;
inline constexpr size_t kDefaultMaxElfSize = 64u * 1024u * 1024u;
inline constexpr uint32_t kKnownCapabilities = (1u << 6) - 1u;
inline constexpr uint32_t kKnownFlags = (1u << 3) - 1u;
inline constexpr uint32_t kFlagAutoStart = 1u << 0;

#if defined(ONION_HOST_TEST)
#ifndef ONION_DATA_ROOT
#define ONION_DATA_ROOT "/tmp/onionhen"
#endif
inline constexpr const char *kInstallRoot = ONION_DATA_ROOT "/plugins";
#else
inline constexpr const char *kInstallRoot = "/data/OnionHEN/plugins";
#endif

struct Descriptor {
  uint32_t struct_size = 0;
  uint32_t abi_version = 0;
  uint32_t capabilities = 0;
  uint32_t flags = 0;
  std::string plugin_id;
  std::string version;
  std::string name;
};

struct Inspection {
  Descriptor descriptor;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

Inspection inspect_elf(std::span<const uint8_t> image);

struct PluginFile {
  std::string path;
  Descriptor descriptor;
  std::vector<uint8_t> image;
  uint64_t fingerprint = 0;
};

uint64_t fingerprint_elf(std::span<const uint8_t> image);

struct DiscoveryIssue {
  std::string path;
  std::string message;
};

struct Discovery {
  std::vector<PluginFile> plugins;
  std::vector<DiscoveryIssue> issues;
};

struct InstallResult {
  bool installed = false;
  std::string path;
  Descriptor descriptor;
  std::string error;
};

class Repository {
public:
  explicit Repository(std::string root = kInstallRoot,
                      size_t max_elf_size = kDefaultMaxElfSize);

  Discovery discover() const;
  InstallResult install(std::string_view source_path) const;
  const std::string &root() const { return root_; }

private:
  std::string root_;
  size_t max_elf_size_;
};

class ProcessRuntime {
public:
  virtual ~ProcessRuntime() = default;
  virtual pid_t recover(std::string_view plugin_id) = 0;
  virtual pid_t launch(const PluginFile &plugin) = 0;
  virtual bool alive(pid_t pid) = 0;
  virtual void persist(std::string_view plugin_id, pid_t pid) = 0;
  virtual void stop(pid_t pid) = 0;
};

struct Instance {
  Descriptor descriptor;
  std::string path;
  uint64_t fingerprint = 0;
  pid_t pid = -1;
};

struct InventoryEntry {
  Descriptor descriptor;
  std::string path;
  uint64_t fingerprint = 0;
  pid_t pid = -1;

  bool running() const { return pid > 1; }
  bool auto_start() const {
    return (descriptor.flags & kFlagAutoStart) != 0;
  }
};

struct OperationResult {
  bool success = false;
  std::string error;

  explicit operator bool() const { return success; }
};

struct ReconcileReport {
  size_t discovered = 0;
  size_t running = 0;
  size_t started = 0;
  size_t failed = 0;
  std::vector<DiscoveryIssue> issues;
};

class Manager {
public:
  Manager(Repository repository, ProcessRuntime &runtime);

  ReconcileReport reconcile();
  OperationResult start(std::string_view plugin_id);
  OperationResult stop(std::string_view plugin_id);
  OperationResult reload(std::string_view plugin_id);
  OperationResult remove(std::string_view plugin_id);
  void stop_all();
  std::vector<Instance> instances() const;
  std::vector<InventoryEntry> inventory() const;

private:
  Repository repository_;
  ProcessRuntime &runtime_;
  mutable std::mutex mutex_;
  std::vector<Instance> instances_;
  std::vector<InventoryEntry> inventory_;
  std::set<std::string> suppressed_;
};

} // namespace onion::plugin
