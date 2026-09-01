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

} // namespace

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

  std::map<std::string, PluginFile *> desired;
  for (PluginFile &plugin : discovery.plugins) {
    if ((plugin.descriptor.flags & kFlagAutoStart) != 0)
      desired.emplace(plugin.descriptor.plugin_id, &plugin);
  }

  for (auto instance = instances_.begin(); instance != instances_.end();) {
    const auto found = desired.find(instance->descriptor.plugin_id);
    if (found == desired.end()) {
      if (runtime_.alive(instance->pid)) runtime_.stop(instance->pid);
      runtime_.persist(instance->descriptor.plugin_id, -1);
      instance = instances_.erase(instance);
      continue;
    }
    PluginFile *current = found->second;
    if (instance->path != current->path ||
        instance->descriptor.version != current->descriptor.version) {
      if (runtime_.alive(instance->pid)) runtime_.stop(instance->pid);
      runtime_.persist(instance->descriptor.plugin_id, -1);
      instance = instances_.erase(instance);
      continue;
    }
    if (!runtime_.alive(instance->pid)) {
      runtime_.persist(instance->descriptor.plugin_id, -1);
      instance = instances_.erase(instance);
      continue;
    }
    desired.erase(found);
    ++report.running;
    ++instance;
  }

  for (auto &[plugin_id, plugin] : desired) {
    pid_t pid = runtime_.recover(plugin_id);
    if (pid <= 1 || !runtime_.alive(pid)) pid = runtime_.launch(*plugin);
    if (pid <= 1) {
      ++report.failed;
      report.issues.push_back({plugin->path, "private elfldr launch failed"});
      runtime_.persist(plugin_id, -1);
      continue;
    }
    runtime_.persist(plugin_id, pid);
    instances_.push_back({plugin->descriptor, plugin->path, pid});
    ++report.running;
    ++report.started;
  }
  return report;
}

void Manager::stop_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const Instance &instance : instances_) {
    if (runtime_.alive(instance.pid)) runtime_.stop(instance.pid);
    runtime_.persist(instance.descriptor.plugin_id, -1);
  }
  instances_.clear();
}

std::vector<Instance> Manager::instances() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return instances_;
}

} // namespace onion::plugin
