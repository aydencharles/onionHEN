#pragma once

#include <onion/plugin_session.hpp>

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace onion::daemon::plugin_host {

/*
 * Persistent per-plugin configuration. Values are stored one `key=value` line
 * per plugin under a shared directory, loaded lazily and cached in memory.
 * Keys must not contain '=' or control characters; values must be single-line.
 */
class ConfigStore {
public:
  explicit ConfigStore(std::string root);

  plugin_ui::Status get(std::string_view owner, std::string_view key,
                        std::vector<uint8_t> &value);
  plugin_ui::Status set(std::string_view owner, std::string_view key,
                        std::string_view value);

private:
  std::string path_for(std::string_view owner) const;
  bool load(std::string_view owner);
  bool save(std::string_view owner);

  std::string root_;
  mutable std::mutex mutex_;
  std::map<std::string, std::map<std::string, std::string>> cache_;
};

/*
 * Concrete host services: routes session LOG/NOTIFY/CONFIG commands into the
 * daemon log, the system notification toast, and the plugin config store.
 */
class PluginHostServices final : public plugin_session::HostServices {
public:
  explicit PluginHostServices(ConfigStore &store);

  plugin_ui::Status log(std::string_view owner, uint32_t level,
                        std::string_view message) override;
  plugin_ui::Status notify(std::string_view owner,
                           std::string_view message) override;
  plugin_ui::Status config_get(std::string_view owner, std::string_view key,
                               std::vector<uint8_t> &value) override;
  plugin_ui::Status config_set(std::string_view owner, std::string_view key,
                               std::string_view value) override;

private:
  ConfigStore &store_;
};

PluginHostServices &host_services();

} // namespace onion::daemon::plugin_host
