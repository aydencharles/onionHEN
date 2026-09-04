#pragma once

#include <cstdint>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include <onion/sprx_catalog.hpp>

namespace onion::daemon::sprx_plugins {

inline constexpr const char *kCatalogPath =
    "/data/OnionHEN/sprx/catalog.ini";

void start();
void reconcile();
std::vector<onion::sprx::SprxInventoryEntry> inventory();
onion::sprx::SprxOperationResult set_enabled(std::string_view id,
                                              bool enabled);
/** Removes only the manifest entry; it never deletes the module file. */
onion::sprx::SprxOperationResult remove(std::string_view id);
void on_big_app_started(pid_t pid, uint32_t app_id,
                        std::string_view title_id);
void on_big_app_exited(pid_t pid, uint32_t app_id,
                       std::string_view title_id);
void stop();

} // namespace onion::daemon::sprx_plugins
