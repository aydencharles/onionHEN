#pragma once

namespace onion::daemon::sprx_plugins {

inline constexpr const char *kCatalogPath =
    "/data/OnionHEN/sprx/catalog.ini";

void start();
void reconcile();
void stop();

} // namespace onion::daemon::sprx_plugins
