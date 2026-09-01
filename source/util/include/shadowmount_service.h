/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

namespace onion::services {

/** Run the vendored ShadowMount+ module to completion on the calling thread.
 *  Blocks until a stop is requested; returns 0 after the full upstream
 *  shutdown sequence ran. */
int shadowmount_module_main();

/** Ask the running module to stop; wakes the scanner loop. Safe to call when
 *  the module is not running. */
void shadowmount_module_request_stop();

} // namespace onion::services
