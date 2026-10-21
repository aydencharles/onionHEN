/* Standalone-FPS shim for the daemon's daemon_ops.hpp: only what
 * daemon_fps.cpp uses. */
#pragma once

#include <atomic>
#include <string>

extern std::atomic_bool g_stack_shutting_down;

/* Settings are fixed in the standalone build; nothing to reload. */
inline bool LoadSettings(bool force = false) {
  (void)force;
  return true;
}

bool Get_Running_App_TID(std::string &title_id, int &BigAppid);

void *fps_sampler_thread(void *args) noexcept;
void *vsync_fps_sampler_thread(void *args) noexcept;
