/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "service_facade.hpp"

#include <onion/builtin_services.h>
#include <onion/platform.h>

#include "pkgserver_adapter.h"
#include "shadowmount_service.h"
#include "util_language.h"

#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

namespace onion::services {

/* Network package installer (DPI): one worker thread owns the vendored
 * pkg-server accept loop on the fixed port ONION_PKGNET_PORT. The standalone
 * takeover logic inside pkg_server_main replaces external pkg-server copies. */

struct PkgNetRuntime {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t operation_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t thread = {};
  bool running = false;
  bool thread_created = false;
  bool desired_enabled = false;
  bool listener_ready = false;
};

PkgNetRuntime g_pkgnet_runtime;

constexpr int kPkgNetReadyWaitMs = 3000;

void *pkgnet_thread_main(void *) {
  const int result = pkg_server_main();

  pthread_mutex_lock(&g_pkgnet_runtime.mutex);
  g_pkgnet_runtime.running = false;
  g_pkgnet_runtime.listener_ready = false;
  pthread_mutex_unlock(&g_pkgnet_runtime.mutex);

  if (result != 0) {
    LOG_ERROR("pkg-server stopped with error %d", result);
  } else {
    LOG_INFO("pkg-server stopped on TCP %u",
             static_cast<unsigned>(ONION_PKGNET_PORT));
  }
  return nullptr;
}

void pkgnet_stop_thread() {
  pthread_t thread = {};
  bool join = false;

  pthread_mutex_lock(&g_pkgnet_runtime.mutex);
  if (g_pkgnet_runtime.thread_created) {
    pkg_server_request_stop();
    thread = g_pkgnet_runtime.thread;
    join = true;
  }
  pthread_mutex_unlock(&g_pkgnet_runtime.mutex);

  if (!join) {
    return;
  }

  pthread_join(thread, nullptr);
  pthread_mutex_lock(&g_pkgnet_runtime.mutex);
  g_pkgnet_runtime.running = false;
  g_pkgnet_runtime.listener_ready = false;
  g_pkgnet_runtime.thread_created = false;
  pthread_mutex_unlock(&g_pkgnet_runtime.mutex);
}

bool pkgnet_wait_for_listener_ready() {
  for (int waited = 0; waited < kPkgNetReadyWaitMs; waited += 50) {
    if (pkg_server_is_listening()) {
      pthread_mutex_lock(&g_pkgnet_runtime.mutex);
      g_pkgnet_runtime.listener_ready = true;
      pthread_mutex_unlock(&g_pkgnet_runtime.mutex);
      return true;
    }

    pthread_mutex_lock(&g_pkgnet_runtime.mutex);
    const bool active = g_pkgnet_runtime.running;
    pthread_mutex_unlock(&g_pkgnet_runtime.mutex);
    if (!active) {
      return false;
    }
    usleep(50 * 1000);
  }
  const bool ready = pkg_server_is_listening() != 0;
  if (ready) {
    pthread_mutex_lock(&g_pkgnet_runtime.mutex);
    g_pkgnet_runtime.listener_ready = true;
    pthread_mutex_unlock(&g_pkgnet_runtime.mutex);
  }
  return ready;
}

bool pkgnet_start_thread() {
  pthread_mutex_lock(&g_pkgnet_runtime.mutex);
  /* Advertise the current UI language (console-backed) so the SSE stream
     reports it before the first frame; re-synced on every (re)start so a
     disable/enable or settings change never leaves a stale value. */
  pkg_server_set_webui_lang(util_webui_language_code());
  g_pkgnet_runtime.running = true;
  g_pkgnet_runtime.listener_ready = false;
  pkg_server_prepare();
  const int rc = pthread_create(&g_pkgnet_runtime.thread, nullptr,
                                pkgnet_thread_main, nullptr);
  if (rc == 0) {
    g_pkgnet_runtime.thread_created = true;
  } else {
    g_pkgnet_runtime.running = false;
  }
  pthread_mutex_unlock(&g_pkgnet_runtime.mutex);

  if (rc != 0) {
    LOG_ERROR("Failed to create pkg-server thread: %d", rc);
    return false;
  }
  return pkgnet_wait_for_listener_ready();
}

bool PkgNetServiceFacade::start() {
  pthread_mutex_lock(&g_pkgnet_runtime.operation_mutex);
  g_pkgnet_runtime.desired_enabled = true;
  pkgnet_stop_thread();

  const bool ok = pkgnet_start_thread();
  if (!ok) {
    pkgnet_stop_thread();
  }
  pthread_mutex_unlock(&g_pkgnet_runtime.operation_mutex);

  if (!ok) {
    LOG_ERROR("Failed to start pkg-server on TCP %u",
              static_cast<unsigned>(ONION_PKGNET_PORT));
    return false;
  }

  LOG_INFO("pkg-server started on TCP %u",
           static_cast<unsigned>(ONION_PKGNET_PORT));
  return true;
}

void PkgNetServiceFacade::stop() {
  pthread_mutex_lock(&g_pkgnet_runtime.operation_mutex);
  g_pkgnet_runtime.desired_enabled = false;
  pkgnet_stop_thread();
  pthread_mutex_unlock(&g_pkgnet_runtime.operation_mutex);
}

bool PkgNetServiceFacade::recover() {
  pthread_mutex_lock(&g_pkgnet_runtime.operation_mutex);
  pthread_mutex_lock(&g_pkgnet_runtime.mutex);
  const bool desired = g_pkgnet_runtime.desired_enabled;
  const bool ready = g_pkgnet_runtime.listener_ready;
  pthread_mutex_unlock(&g_pkgnet_runtime.mutex);

  if (!desired) {
    pthread_mutex_unlock(&g_pkgnet_runtime.operation_mutex);
    LOG_DEBUG("pkg-server recovery skipped; service is disabled");
    return true;
  }
  if (ready && pkg_server_is_listening()) {
    pthread_mutex_unlock(&g_pkgnet_runtime.operation_mutex);
    return true;
  }

  pkgnet_stop_thread();
  const bool ok = pkgnet_start_thread();
  pthread_mutex_unlock(&g_pkgnet_runtime.operation_mutex);

  if (ok) {
    LOG_INFO("pkg-server recovered on TCP %u",
             static_cast<unsigned>(ONION_PKGNET_PORT));
  } else {
    LOG_ERROR("pkg-server recovery failed on TCP %u",
              static_cast<unsigned>(ONION_PKGNET_PORT));
  }
  return ok;
}

bool PkgNetServiceFacade::running() const {
  pthread_mutex_lock(&g_pkgnet_runtime.mutex);
  const bool value = g_pkgnet_runtime.listener_ready;
  pthread_mutex_unlock(&g_pkgnet_runtime.mutex);
  return value;
}

PkgNetServiceFacade &pkgNetService() {
  static PkgNetServiceFacade service;
  return service;
}

/* ShadowMount+ runs in one facade-owned worker thread, with third-party
 * details confined to the adapter (shadowmount_main.cpp). */

struct ShadowMountRuntime {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t operation_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t thread = {};
  bool running = false;
  bool thread_created = false;
};

ShadowMountRuntime g_shadowmount_runtime;

void *shadowmount_thread_main(void *arg) {
  (void)arg;
  const int result = shadowmount_module_main();

  pthread_mutex_lock(&g_shadowmount_runtime.mutex);
  g_shadowmount_runtime.running = false;
  pthread_mutex_unlock(&g_shadowmount_runtime.mutex);

  if (result != 0) {
    LOG_ERROR("shadowmount stopped with error %d", result);
  } else {
    LOG_INFO("shadowmount module stopped");
  }
  return nullptr;
}

void shadowmount_stop_thread() {
  pthread_t thread = {};
  bool join = false;

  pthread_mutex_lock(&g_shadowmount_runtime.mutex);
  if (g_shadowmount_runtime.thread_created) {
    shadowmount_module_request_stop();
    thread = g_shadowmount_runtime.thread;
    join = true;
  }
  pthread_mutex_unlock(&g_shadowmount_runtime.mutex);

  if (!join) {
    return;
  }

  pthread_join(thread, nullptr);
  pthread_mutex_lock(&g_shadowmount_runtime.mutex);
  g_shadowmount_runtime.running = false;
  g_shadowmount_runtime.thread_created = false;
  pthread_mutex_unlock(&g_shadowmount_runtime.mutex);
}

bool ShadowMountServiceFacade::start() {
  pthread_mutex_lock(&g_shadowmount_runtime.operation_mutex);
  shadowmount_stop_thread();

  bool ok = true;
  pthread_mutex_lock(&g_shadowmount_runtime.mutex);
  g_shadowmount_runtime.running = true;
  const int rc = pthread_create(&g_shadowmount_runtime.thread, nullptr,
                                shadowmount_thread_main, nullptr);
  if (rc == 0) {
    g_shadowmount_runtime.thread_created = true;
  } else {
    g_shadowmount_runtime.running = false;
    ok = false;
  }
  pthread_mutex_unlock(&g_shadowmount_runtime.mutex);

  if (!ok) {
    pthread_mutex_unlock(&g_shadowmount_runtime.operation_mutex);
    LOG_ERROR("Failed to create shadowmount thread: %d", rc);
    return false;
  }
  pthread_mutex_unlock(&g_shadowmount_runtime.operation_mutex);

  LOG_INFO("shadowmount module started");
  return true;
}

void ShadowMountServiceFacade::stop() {
  pthread_mutex_lock(&g_shadowmount_runtime.operation_mutex);
  shadowmount_stop_thread();
  pthread_mutex_unlock(&g_shadowmount_runtime.operation_mutex);
}

bool ShadowMountServiceFacade::running() const {
  pthread_mutex_lock(&g_shadowmount_runtime.mutex);
  const bool value = g_shadowmount_runtime.running;
  pthread_mutex_unlock(&g_shadowmount_runtime.mutex);
  return value;
}

ShadowMountServiceFacade &shadowMountService() {
  static ShadowMountServiceFacade service;
  return service;
}

} // namespace onion::services
