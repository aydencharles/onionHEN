#pragma once

#include <onion/plugin_session.hpp>

namespace onion::daemon::plugin_ipc {

bool start();
void stop();
void restart();
bool is_listening();

/* Serve one accepted stream until EOF or a fatal protocol error. */
void serve_connection(int socket, plugin_session::SessionDirectory &directory,
                      plugin_ui::ProtocolBroker &ui_broker,
                      plugin_session::EventSource *events = nullptr,
                      plugin_session::HostServices *host = nullptr);

} // namespace onion::daemon::plugin_ipc
