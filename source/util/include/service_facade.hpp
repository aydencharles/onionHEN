/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

#include <stdint.h>

namespace onion::services {

class FtpServiceFacade {
public:
  /** Start the in-process FTP module on @port. */
  bool start(uint16_t port);
  /** Stop the current session; idempotent. */
  void stop();
  /** Apply a port change without enabling a manually disabled service. */
  bool reconfigure(uint16_t port);
  /** Rebind the listener after a network resume when FTP was enabled. */
  bool recover();
  bool running() const;
  uint16_t port() const;
};

FtpServiceFacade &ftpService();

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
