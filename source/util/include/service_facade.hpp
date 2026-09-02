/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

#include <stdint.h>

namespace onion::services {

class PkgNetServiceFacade {
public:
  /** Start the in-process DPI package install server (TCP 9090). */
  bool start();
  /** Stop the current session; idempotent. */
  void stop();
  /** Re-establish the listener after rest mode when enabled. */
  bool recover();
  bool running() const;
};

PkgNetServiceFacade &pkgNetService();

class ShadowMountServiceFacade {
public:
  /** Start the in-process ShadowMount+ module on a worker thread. */
  bool start();
  /** Request module shutdown and join its worker; idempotent. */
  void stop();
  bool running() const;
};

ShadowMountServiceFacade &shadowMountService();

} // namespace onion::services
