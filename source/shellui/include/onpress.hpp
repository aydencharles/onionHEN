/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Table-driven OnPress dispatch for ShellUI toolbox items.
 */

#pragma once

#include "shellui_types.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <cstddef>
#include <string>

/** Per-press context shared by domain handlers. */
struct OnPressContext {
  MonoObject *instance = nullptr;
  MonoObject *element = nullptr;
  MonoObject *event = nullptr;
  std::string id;
  std::string value;
  std::string title;

  /** Request settings_commit after successful handling. */
  bool reload_main = false;
  bool reload_util = false;
  bool dirty = true; // SaveSettings by default when handled
};

/**
 * Handler result:
 * - Handled: domain handled the id; dispatcher will settings_commit (if dirty) then oOnPress.
 * - Consumed: fully handled custom/dynamic id; optional commit if dirty, do NOT call oOnPress.
 *   Use for ids outside the stock Settings model (e.g. id_cheat_*), where original
 *   SettingPage.OnPressed may null-deref on unknown elements.
 * - EarlyReturn: stop now; call oOnPress without commit (no-op / validation fail).
 * - NotMine: try next matcher / fall through.
 */
enum class OnPressResult {
  Handled,
  Consumed,
  EarlyReturn,
  NotMine,
};

using OnPressHandler = OnPressResult (*)(OnPressContext &ctx);

struct OnPressExactEntry {
  const char *id;
  OnPressHandler handler;
};

struct OnPressPrefixEntry {
  const char *prefix;
  OnPressHandler handler;
};

/* Domain tables (defined in onpress_*.cpp). */
const OnPressExactEntry *onpress_overlay_exact(size_t *count);
const OnPressExactEntry *onpress_network_exact(size_t *count);
const OnPressExactEntry *onpress_system_exact(size_t *count);
const OnPressExactEntry *onpress_misc_root_exact(size_t *count);
const OnPressExactEntry *onpress_account_exact(size_t *count);

const OnPressPrefixEntry *onpress_payloads_prefix(size_t *count);
const OnPressPrefixEntry *onpress_cheats_prefix(size_t *count);
const OnPressPrefixEntry *onpress_packages_prefix(size_t *count);

const OnPressExactEntry *onpress_plugins_exact(size_t *count);
const OnPressPrefixEntry *onpress_plugins_prefix(size_t *count);

/**
 * Shared built-in plugin handlers (root page + plugins page).
 * Run toggles change the current session only. Autoload toggles persist for
 * the next OnionHEN start and do not start or stop the service now.
 */
OnPressResult onpress_kstuff_autoload(OnPressContext &ctx);
OnPressResult onpress_delete_kstuff(OnPressContext &ctx);

/** Shared toggle helpers. */
inline bool value_as_int(const OnPressContext &ctx) {
  return atoi(ctx.value.c_str());
}

inline bool value_as_long(const OnPressContext &ctx) {
  return atol(ctx.value.c_str());
}
