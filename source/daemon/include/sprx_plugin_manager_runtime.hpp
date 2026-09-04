#pragma once

#include <cstdint>
#include <string_view>
#include <sys/types.h>

namespace onion::daemon::sprx_plugins {

inline constexpr const char *kCatalogPath =
    "/data/OnionHEN/sprx/catalog.ini";

void start();
void reconcile();
void on_big_app_started(pid_t pid, uint32_t app_id,
                        std::string_view title_id);
void on_big_app_exited(pid_t pid, uint32_t app_id,
                       std::string_view title_id);
void stop();

} // namespace onion::daemon::sprx_plugins
