#pragma once

#include <onion/plugin_session.hpp>
#include <onion/plugin_ui.hpp>

namespace onion::daemon::plugin_ui_bridge {

bool start(plugin_ui::ProtocolBroker &broker, plugin_ui::Registry &registry);
void stop();
void restart();
bool is_listening();
plugin_session::EventSource &event_source();

} // namespace onion::daemon::plugin_ui_bridge
