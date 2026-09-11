#pragma once

#include <onion/plugin_manager.hpp>

#include <string_view>
#include <vector>

namespace onion::daemon::plugins {

void start();
void reconcile();
void stop();
std::vector<plugin::InventoryEntry> inventory();
plugin::OperationResult start_plugin(std::string_view plugin_id);
plugin::OperationResult stop_plugin(std::string_view plugin_id);
plugin::OperationResult reload_plugin(std::string_view plugin_id);
plugin::OperationResult remove_plugin(std::string_view plugin_id);
plugin::OperationResult set_auto_start(std::string_view plugin_id, bool enabled);

} // namespace onion::daemon::plugins
