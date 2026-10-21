/* Standalone-FPS shim: replaces libonion_settings.
 * daemon_fps.cpp only reads overlay_enabled / overlay_fps; here both are
 * always on. */
#pragma once

namespace onion {
struct Settings {
  bool overlay_enabled = true;
  bool overlay_fps = true;
};
} // namespace onion
