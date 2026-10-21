/* Standalone-FPS shim for the daemon's globalconf.hpp. */
#pragma once

#include <onion/settings.hpp>

namespace onion {
struct SettingsStore {
  Settings snapshot() const { return Settings{}; }
};
} // namespace onion

extern onion::SettingsStore g_settings;
