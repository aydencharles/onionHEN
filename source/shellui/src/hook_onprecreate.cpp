/* Copyright (C) 2025 OnionHEN / LightningMods
 * Extracted from hook_functions.cpp — hook_onprecreate
 *
 * Value binding is table-driven via resolve_toolbox_control_value → set_Value
 * on SettingElement during SettingPage.OnCreating.
 */
#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "ipc.hpp"
#include "onpress_policy.hpp"
#include "progress_dialog.hpp"
#include "remote_play.hpp"
#include "toolbox_values.hpp"
#include "settings_page_refresh.hpp"

#include "shellui_state.hpp"
#include <onion/platform.h>

#include <string>


std::string GetPropertyValue(MonoObject *element, const char *propertyName);

namespace {

int call_original(MonoObject *instance, MonoObject *element) {
  return oOnPreCreate ? oOnPreCreate(instance, element) : 0;
}

bool ensure_set_value_method() {
  if (set_value_method)
    return true;

  MonoAssembly *Legacy_assembly =
      mono_domain_assembly_open(Root_Domain, legacy_dec.c_str());
  if (!Legacy_assembly) {
    LOG_ERROR("Failed to open assembly.");
    return false;
  }

  MonoImage *leg_img = mono_assembly_get_image(Legacy_assembly);
  if (!leg_img) {
    LOG_ERROR("Failed to get image.");
    return false;
  }

  MonoClass *klass =
      mono_class_from_name(leg_img, UI3_dec.c_str(), "SettingElement");
  if (!klass) {
    sceKernelDebugOutText(0, "Failed to find class\n");
    return false;
  }

  MonoProperty *s_Property = mono_class_get_property_from_name(klass, "Value");
  if (!s_Property) {
    LOG_ERROR("Failed to find property");
    return false;
  }

  set_value_method = mono_property_get_set_method(s_Property);
  if (!set_value_method) {
    LOG_ERROR("Failed to find set method");
    return false;
  }
  return true;
}

} // namespace

int OnPreCreate_Hook(MonoObject *Instance, MonoObject *element) {
  if (!shellui_hooks_are_ready())
    return call_original(Instance, element);

  if (!Instance || !element) {
#if SHELL_DEBUG == 1
    LOG_DEBUG("[LM HOOK] OnPreCreate_Hook: args are null");
#endif
    return call_original(Instance, element);
  }

  onion::shellui::settings::bind(Instance);
  const std::string id = GetPropertyValue(element, "Id");

  if (!toolbox::toolbox_owns_settings_page(g_ui.active_page)) {
    return call_original(Instance, element);
  }

  if (g_ui.active_page == toolbox::Page::CheatProgress) {
    cheat_progress_bind_page(Instance);
    return call_original(Instance, element);
  }

  if (g_ui.active_page == toolbox::Page::RemotePlay)
    remote_play_bind_page(Instance);

  if (!ensure_set_value_method())
    return -1;

  const std::string value = resolve_toolbox_control_value(id);

  if (!value.empty()) {
    MonoDomain *dom = (mono_domain_get ? mono_domain_get() : nullptr);
    if (!dom)
      dom = Root_Domain;
    MonoString *text =
        (dom && mono_string_new) ? mono_string_new(dom, value.c_str()) : nullptr;
    if (text) {
      mono_runtime_invoke(set_value_method, element,
                          reinterpret_cast<void **>(&text), nullptr);
    }
  }

  return call_original(Instance, element);
}
