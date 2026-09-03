#include <onion/plugin_manager.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace onion::plugin {
namespace {

bool has_elf_suffix(std::string_view name) {
  return name.size() > 4 && name.substr(name.size() - 4) == ".elf";
}

bool read_file(const std::string &path, size_t limit, std::vector<uint8_t> &out,
               std::string &error) {
  struct stat entry {};
  if (lstat(path.c_str(), &entry) != 0 || !S_ISREG(entry.st_mode)) {
    error = "not a regular file";
    return false;
  }
  if (entry.st_size <= 0 || static_cast<uint64_t>(entry.st_size) > limit) {
    error = "ELF is empty or exceeds the size limit";
    return false;
  }
#ifdef O_NOFOLLOW
  const int descriptor = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
#else
  const int descriptor = open(path.c_str(), O_RDONLY);
#endif
  if (descriptor < 0) {
    error = std::string("open failed: ") + std::strerror(errno);
    return false;
  }
  out.resize(static_cast<size_t>(entry.st_size));
  size_t offset = 0;
  while (offset < out.size()) {
    const ssize_t count = read(descriptor, out.data() + offset, out.size() - offset);
    if (count <= 0) {
      error = std::string("read failed: ") + std::strerror(errno);
      close(descriptor);
      out.clear();
      return false;
    }
    offset += static_cast<size_t>(count);
  }
  close(descriptor);
  return true;
}

bool write_all(int descriptor, std::span<const uint8_t> image) {
  size_t offset = 0;
  while (offset < image.size()) {
    const ssize_t count = write(descriptor, image.data() + offset,
                                image.size() - offset);
    if (count <= 0) return false;
    offset += static_cast<size_t>(count);
  }
  return true;
}

void stop_instance(ProcessRuntime &runtime, const Instance &instance) {
  if (runtime.alive(instance.pid))
    runtime.stop(instance.pid, instance.descriptor.flags);
  runtime.persist(instance.descriptor.plugin_id, -1);
}

void rebuild_inventory(const Discovery &discovery,
                       const std::vector<Instance> &instances,
                       std::vector<InventoryEntry> &inventory) {
  inventory.clear();
  inventory.reserve(discovery.plugins.size());
  for (const PluginFile &plugin : discovery.plugins) {
    const auto running = std::find_if(
        instances.begin(), instances.end(), [&](const Instance &instance) {
          return instance.descriptor.plugin_id == plugin.descriptor.plugin_id;
        });
    inventory.push_back({plugin.descriptor, plugin.path, plugin.fingerprint,
                         running == instances.end() ? -1 : running->pid});
  }
}

PluginFile *find_plugin(Discovery &discovery, std::string_view plugin_id) {
  const auto found = std::find_if(
      discovery.plugins.begin(), discovery.plugins.end(),
      [&](const PluginFile &plugin) {
        return plugin.descriptor.plugin_id == plugin_id;
      });
  return found == discovery.plugins.end() ? nullptr : &*found;
}

auto find_instance(std::vector<Instance> &instances,
                   std::string_view plugin_id) {
  return std::find_if(instances.begin(), instances.end(),
                      [&](const Instance &instance) {
                        return instance.descriptor.plugin_id == plugin_id;
                      });
}

OperationResult launch_plugin(ProcessRuntime &runtime, const PluginFile &plugin,
                              std::vector<Instance> &instances,
                              bool allow_recover) {
  pid_t pid = allow_recover
                  ? runtime.recover(plugin.descriptor.plugin_id)
                  : -1;
  if (pid <= 1 || !runtime.alive(pid)) pid = runtime.launch(plugin);
  if (pid <= 1) {
    runtime.persist(plugin.descriptor.plugin_id, -1);
    return {false, "private elfldr launch failed"};
  }
  runtime.persist(plugin.descriptor.plugin_id, pid);
  instances.push_back(
      {plugin.descriptor, plugin.path, plugin.fingerprint, pid});
  return {true, {}};
}

} // namespace

uint64_t fingerprint_elf(std::span<const uint8_t> image) {
  // FNV-1a is deterministic across host and PS5 builds and catches in-place
  // replacement without relying on filesystem timestamp precision.
  uint64_t hash = 14695981039346656037ull;
  for (const uint8_t byte : image) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

Repository::Repository(std::string root, size_t max_elf_size)
    : root_(std::move(root)), max_elf_size_(max_elf_size) {}

Discovery Repository::discover() const {
  Discovery result;
  DIR *directory = opendir(root_.c_str());
  if (!directory) {
    if (errno != ENOENT)
      result.issues.push_back({root_, std::string("directory open failed: ") +
                                         std::strerror(errno)});
    return result;
  }

  std::vector<std::string> paths;
  while (dirent *entry = readdir(directory)) {
    const std::string_view name(entry->d_name);
    if (name == "." || name == ".." || !has_elf_suffix(name)) continue;
    paths.push_back(root_ + "/" + std::string(name));
  }
  closedir(directory);
  std::sort(paths.begin(), paths.end());

  for (const std::string &path : paths) {
    PluginFile plugin;
    plugin.path = path;
    std::string error;
    if (!read_file(path, max_elf_size_, plugin.image, error)) {
      result.issues.push_back({path, std::move(error)});
      continue;
    }
    Inspection inspection = inspect_elf(plugin.image);
    if (!inspection) {
      result.issues.push_back({path, std::move(inspection.error)});
      continue;
    }
    plugin.descriptor = std::move(inspection.descriptor);
    plugin.fingerprint = fingerprint_elf(plugin.image);
    result.plugins.push_back(std::move(plugin));
  }

  std::map<std::string, size_t> counts;
  for (const PluginFile &plugin : result.plugins) ++counts[plugin.descriptor.plugin_id];
  std::vector<PluginFile> unique;
  unique.reserve(result.plugins.size());
  for (PluginFile &plugin : result.plugins) {
    if (counts[plugin.descriptor.plugin_id] != 1) {
      result.issues.push_back(
          {plugin.path, "duplicate plugin ID " + plugin.descriptor.plugin_id});
      continue;
    }
    unique.push_back(std::move(plugin));
  }
  result.plugins = std::move(unique);
  return result;
}

InstallResult Repository::install(std::string_view source_path) const {
  InstallResult result;
  std::vector<uint8_t> image;
  if (!read_file(std::string(source_path), max_elf_size_, image, result.error))
    return result;
  Inspection inspection = inspect_elf(image);
  if (!inspection) {
    result.error = std::move(inspection.error);
    return result;
  }
  result.descriptor = std::move(inspection.descriptor);
  if (mkdir(root_.c_str(), 0777) != 0 && errno != EEXIST) {
    result.error = std::string("cannot create plugin directory: ") +
                   std::strerror(errno);
    return result;
  }

  result.path = root_ + "/" + result.descriptor.plugin_id + ".elf";
  const std::string temporary = result.path + ".installing";
  unlink(temporary.c_str());
  const int output = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0755);
  if (output < 0) {
    result.error = std::string("cannot create staging file: ") +
                   std::strerror(errno);
    return result;
  }
  const bool written = write_all(output, image) && fsync(output) == 0;
  const int close_result = close(output);
  if (!written || close_result != 0 || rename(temporary.c_str(), result.path.c_str()) != 0) {
    result.error = std::string("atomic install failed: ") + std::strerror(errno);
    unlink(temporary.c_str());
    return result;
  }
  result.installed = true;
  return result;
}

Manager::Manager(Repository repository, ProcessRuntime &runtime)
    : repository_(std::move(repository)), runtime_(runtime) {}

ReconcileReport Manager::reconcile() {
  std::lock_guard<std::mutex> lock(mutex_);
  Discovery discovery = repository_.discover();
  ReconcileReport report;
  report.discovered = discovery.plugins.size();
  report.issues = std::move(discovery.issues);

  std::map<std::string, PluginFile *> discovered;
  for (PluginFile &plugin : discovery.plugins)
    discovered.emplace(plugin.descriptor.plugin_id, &plugin);
  for (auto suppressed = suppressed_.begin(); suppressed != suppressed_.end();) {
    if (!discovered.contains(*suppressed))
      suppressed = suppressed_.erase(suppressed);
    else
      ++suppressed;
  }
  std::set<std::string> restart;
  std::set<std::string> relaunch;

  for (auto instance = instances_.begin(); instance != instances_.end();) {
    const auto found = discovered.find(instance->descriptor.plugin_id);
    if (found == discovered.end()) {
      stop_instance(runtime_, *instance);
      suppressed_.erase(instance->descriptor.plugin_id);
      instance = instances_.erase(instance);
      continue;
    }
    PluginFile *current = found->second;
    if (instance->path != current->path ||
        instance->fingerprint != current->fingerprint) {
      if (runtime_.alive(instance->pid))
        restart.insert(instance->descriptor.plugin_id);
      stop_instance(runtime_, *instance);
      instance = instances_.erase(instance);
      continue;
    }
    if (!runtime_.alive(instance->pid)) {
      runtime_.persist(instance->descriptor.plugin_id, -1);
      const bool supervised =
          (current->descriptor.flags & kFlagLongRunning) != 0 &&
          !suppressed_.contains(instance->descriptor.plugin_id);
      if (supervised) relaunch.insert(instance->descriptor.plugin_id);
      instance = instances_.erase(instance);
      continue;
    }
    ++report.running;
    ++instance;
  }

  for (auto &[plugin_id, plugin] : discovered) {
    if (find_instance(instances_, plugin_id) != instances_.end()) continue;
    const bool replacing_running = restart.contains(plugin_id);
    const bool auto_start =
        (plugin->descriptor.flags & kFlagAutoStart) != 0 &&
        !suppressed_.contains(plugin_id);
    if (!replacing_running && !auto_start && !relaunch.contains(plugin_id))
      continue;
    const OperationResult launched = launch_plugin(
        runtime_, *plugin, instances_, !replacing_running);
    if (!launched) {
      ++report.failed;
      report.issues.push_back({plugin->path, launched.error});
      continue;
    }
    ++report.running;
    ++report.started;
  }
  rebuild_inventory(discovery, instances_, inventory_);
  return report;
}

OperationResult Manager::start(std::string_view plugin_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  Discovery discovery = repository_.discover();
  PluginFile *plugin = find_plugin(discovery, plugin_id);
  if (!plugin) return {false, "plugin is not installed"};

  auto instance = find_instance(instances_, plugin_id);
  if (instance != instances_.end() && runtime_.alive(instance->pid)) {
    suppressed_.erase(std::string(plugin_id));
    rebuild_inventory(discovery, instances_, inventory_);
    return {true, {}};
  }
  if (instance != instances_.end()) {
    runtime_.persist(plugin_id, -1);
    instances_.erase(instance);
  }
  suppressed_.erase(std::string(plugin_id));
  const OperationResult result =
      launch_plugin(runtime_, *plugin, instances_, false);
  rebuild_inventory(discovery, instances_, inventory_);
  return result;
}

OperationResult Manager::stop(std::string_view plugin_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto known = std::find_if(
      inventory_.begin(), inventory_.end(), [&](const InventoryEntry &entry) {
        return entry.descriptor.plugin_id == plugin_id;
      });
  if (known == inventory_.end()) return {false, "plugin is not installed"};

  auto instance = find_instance(instances_, plugin_id);
  if (instance != instances_.end()) {
    stop_instance(runtime_, *instance);
    instances_.erase(instance);
  }
  suppressed_.insert(std::string(plugin_id));
  known->pid = -1;
  return {true, {}};
}

OperationResult Manager::reload(std::string_view plugin_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  Discovery discovery = repository_.discover();
  PluginFile *plugin = find_plugin(discovery, plugin_id);
  if (!plugin) return {false, "plugin is not installed"};

  auto instance = find_instance(instances_, plugin_id);
  if (instance != instances_.end()) {
    stop_instance(runtime_, *instance);
    instances_.erase(instance);
  }
  suppressed_.erase(std::string(plugin_id));
  const OperationResult result =
      launch_plugin(runtime_, *plugin, instances_, false);
  rebuild_inventory(discovery, instances_, inventory_);
  return result;
}

OperationResult Manager::remove(std::string_view plugin_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto known = std::find_if(
      inventory_.begin(), inventory_.end(), [&](const InventoryEntry &entry) {
        return entry.descriptor.plugin_id == plugin_id;
      });
  if (known == inventory_.end()) return {false, "plugin is not installed"};
  const std::string path = known->path;

  auto instance = find_instance(instances_, plugin_id);
  if (instance != instances_.end()) {
    stop_instance(runtime_, *instance);
    instances_.erase(instance);
  }
  if (unlink(path.c_str()) != 0 && errno != ENOENT) {
    rebuild_inventory(repository_.discover(), instances_, inventory_);
    return {false, std::string("cannot delete plugin: ") +
                       std::strerror(errno)};
  }
  suppressed_.erase(std::string(plugin_id));
  inventory_.erase(known);
  return {true, {}};
}

void Manager::stop_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const Instance &instance : instances_) {
    stop_instance(runtime_, instance);
  }
  instances_.clear();
  for (InventoryEntry &entry : inventory_) entry.pid = -1;
}

std::vector<InventoryEntry> Manager::inventory() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return inventory_;
}

std::vector<Instance> Manager::instances() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return instances_;
}

} // namespace onion::plugin
