/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Process-wide resume coordination.  The system-state event flag is the
 * resume source; service recovery and ShellUI reconciliation remain owned by
 * their respective modules.
 */

#include "daemon_ops.hpp"
#include "daemon_power_state.hpp"
#include "plugin_ipc_server.hpp"
#include "plugin_manager_runtime.hpp"
#include <onion/ipc_client.hpp>
#include <onion/platform.h>
#include <atomic>
#include <unistd.h>

namespace {

bool listeners_ready() {
  return control_tcp_is_listening() && crit_ipc_is_listening() &&
         onion::daemon::plugin_ipc::is_listening();
}

} // namespace

void *resume_recovery_thread(void *args) noexcept {
  (void)args;
  bool was_sleeping = false;
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const DaemonPowerState power_state = daemon_power_state_get();
    if (daemon_power_state_is_sleeping(power_state)) {
      was_sleeping = true;
    }
    if (was_sleeping && power_state == DaemonPowerState::Working) {
      was_sleeping = false;
      LOG_INFO("rest: WORKING transition; starting recovery transaction");

      /* Network interfaces and loopback sockets need time to settle. */
      usleep(1000 * 1000);
      restart_crit_ipc_server();
      control_tcp_restart();
      onion::daemon::plugin_ipc::restart();

      bool ready = false;
      for (int i = 0; i < 30; ++i) {
        if (listeners_ready()) {
          ready = true;
          break;
        }
        usleep(100 * 1000);
      }
      LOG_INFO("rest: listener recovery %s (tcp9048=%d crit_ipc=%d plugin_ipc=%d)",
               ready ? "ready" : "pending",
               control_tcp_is_listening() ? 1 : 0,
               crit_ipc_is_listening() ? 1 : 0,
               onion::daemon::plugin_ipc::is_listening() ? 1 : 0);

      if (onion::daemon::plugin_ipc::is_listening()) {
        /* plugin_ipc restart closes every stream; restart managed processes so
         * their SDK clients perform HELLO again on a fresh connection. */
        onion::daemon::plugins::stop();
        onion::daemon::plugins::start();
      }

      /* NOTE_EXEC remains authoritative; this is only compensation. */
      toolbox_on_resume();
    }
    usleep(100 * 1000);
  }
  return nullptr;
}
