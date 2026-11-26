/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * ReactButton uses invertedIconSource while focused.  Asset-path icons only
 * populate iconSource, so mirror OnionHEN's source into both properties.
 *
 * Lives outside homeui_top_nav_patch.cpp so the Control Center extension host
 * keeps a working focus icon when the HomeUI top-nav patch is A/B disabled
 * (and vice versa); either switch alone must not blank the other's icon.
 */
#include "onion_icon_source_hook.hpp"

#include "control_center_patch.hpp"
#include "defs.h"
#include "detour.h"
#include "external_symbols.hpp"
#include "homeui_top_nav_patch.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <string>

namespace {

#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1 || SHELLUI_CONTROL_CENTER_PATCH == 1

constexpr const char *kOnionHenIconPath = "/system_ex/vsh_asset/onionhen.png";

void (*g_set_icon_source_orig)(MonoObject *instance, MonoObject *source) =
    nullptr;
void (*g_set_inverted_icon_source)(MonoObject *instance, MonoObject *source) =
    nullptr;

/*
 * Crash history was from Fps body hijack on game-close remount, not from this
 * mirror itself. Still harden: path filter, exception-safe ToString, re-entry
 * guard (SetinvertedIconSource must not re-enter SetIconSource).
 */
bool is_onionhen_icon_source(MonoObject *source) {
  if (!source || !mono_object_to_string) {
    return false;
  }

  MonoObject *exception = nullptr;
  MonoString *text = mono_object_to_string(source, &exception);
  if (exception || !text) {
    return false;
  }

  return Mono_to_String(text).find(kOnionHenIconPath) != std::string::npos;
}

void set_icon_source_hook(MonoObject *instance, MonoObject *source) {
  thread_local int depth = 0;

  if (g_set_icon_source_orig) {
    g_set_icon_source_orig(instance, source);
  }

  if (depth > 0 || !shellui_hooks_are_ready() || !instance || !source ||
      !g_set_inverted_icon_source || !is_onionhen_icon_source(source)) {
    return;
  }

  ++depth;
  g_set_inverted_icon_source(instance, source);
  --depth;
#if SHELL_DEBUG == 1
  LOG_DEBUG("onion_icon_source_hook: mirrored OnionHEN icon to invertedIcon");
#endif
}

#endif

} // namespace

void install_onion_icon_source_hook(MonoImage *react_pui) {
#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1 || SHELLUI_CONTROL_CENTER_PATCH == 1
  if (!react_pui) {
    LOG_ERROR("onion_icon_source_hook: ReactNative.PUI image missing");
    return;
  }

  g_set_inverted_icon_source =
      reinterpret_cast<void (*)(MonoObject *, MonoObject *)>(
          Get_Address_of_Method(react_pui, "ReactNative.Views.UI3.View",
                                "ReactButtonShadowNode",
                                "SetinvertedIconSource", 1));
  if (!g_set_inverted_icon_source) {
    LOG_ERROR("onion_icon_source_hook: SetinvertedIconSource missing; "
              "focused icon may be blank");
  }

  const uint64_t target =
      Get_Address_of_Method(react_pui, "ReactNative.Views.UI3.View",
                            "ReactButtonShadowNode", "SetIconSource", 1);
  if (!target) {
    LOG_ERROR("onion_icon_source_hook: SetIconSource missing");
    return;
  }

  const bool installed = InstallDetour(
      target, reinterpret_cast<void *>(&set_icon_source_hook),
      reinterpret_cast<void **>(&g_set_icon_source_orig));
  LOG_DEBUG(installed ? "onion_icon_source_hook: SetIconSource hooked "
                        "(invertedIcon mirror for OnionHEN)"
                      : "onion_icon_source_hook: SetIconSource detour failed");
#else
  (void)react_pui;
#endif
}
