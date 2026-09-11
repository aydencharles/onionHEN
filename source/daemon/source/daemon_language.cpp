/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Live system-language poll. The util process switches to PTRACE_AUTHID after
 * launch and can never query the SystemService again, so the daemon (which
 * keeps normal credentials) polls the console language here and pushes each
 * change to util over IPC. Toolbox language changes still flow through
 * BREW_RELOAD_SETTINGS, so the poll only reacts to console-language switches.
 */

#include "daemon_ops.hpp"
#include "globalconf.hpp"

#include <onion/ipc_client.hpp>
#include <onion/log.h>
#include <onion/notify_i18n.h>
#include <onion/settings.hpp>

#include <unistd.h>

extern "C" int sceSystemServiceParamGetInt(int param_id, int *value);

namespace {

/** Poll cadence; a console language switch converges within this window. */
constexpr useconds_t kLanguagePollUs = 5u * 1000u * 1000u;

} // namespace

void *system_language_poll_thread(void *args) noexcept {
  (void)args;

  int last_pushed = -1;
  bool logged_error = false;

  LOG_INFO("system language poll started (every %lu s)",
           (unsigned long)(kLanguagePollUs / (1000u * 1000u)));

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    usleep(kLanguagePollUs);

    const onion::Settings cfg = g_settings.snapshot();
    int system_language = 1;
    if (sceSystemServiceParamGetInt(1, &system_language) < 0 ||
        system_language < 0) {
      if (!logged_error) {
        LOG_ERROR("system language query failed at runtime; keeping %d",
                  last_pushed);
        logged_error = true;
      }
      continue;
    }
    logged_error = false;

    if (system_language == last_pushed)
      continue;
    last_pushed = system_language;

    onion_notify_apply_ui_language(cfg.ui_lang, system_language);
    if (IPC_Client::getInstance(true).SetSystemLanguage(system_language)) {
      LOG_DEBUG("pushed console system language %d to util", system_language);
    }
  }

  LOG_INFO("system language poll stopped");
  return nullptr;
}
