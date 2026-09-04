/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.
*/

#include "bootstrap_assets.h"
#include "bootstrap_config.h"
#include "bootstrap_filesystem.h"
#include "bootstrap_notify.h"
#include "bootstrap_runtime.h"
#include "launch_pipeline.h"
#include "payload_autostart.h"

#include <onion/conflict.h>
#include <onion/log.h>

int main(void) {
  BootstrapConfig config{};
  if (!bootstrap_config_load(&config))
    return -1;

  if (!bootstrap_runtime_prepare())
    return -1;
  bootstrap_runtime_enable_remote_logging();

  const char *conflict = onion_conflict_detect();
  if (conflict) {
    LOG_ERROR("refusing start: %s already running", conflict);
    bootstrap_notify("notify.boot.conflict", conflict);
    return 0;
  }

  LOG_DEBUG("============== Spawner (Bootstrapper) Started =================");
  bootstrap_filesystem_create_directories();
  if (!bootstrap_filesystem_mount_system())
    return -1;

  LOG_DEBUG("Writing embedded assets ...");
  bootstrap_notify_starting(bootstrap_assets_write());
  LOG_DEBUG("   Written!");

  bootstrap_filesystem_disable_updates();

  const int launch_result = bootstrap_launch_services(
      config.firmware_version, config.kstuff_autoload);
  if (launch_result != 0)
    return launch_result;

  bootstrap_payload_autostart();
  LOG_DEBUG("============== Spawner (Bootstrapper) Finished =================");
  return 0;
}
