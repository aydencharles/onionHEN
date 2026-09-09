/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Shared Legacy Settings navigation (UIManager.Instance.Push).
 */

#include "toolbox_navigation.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "monodef.h"

#include <onion/platform.h>

namespace {

constexpr const char *kLegacyCoreNamespace = "Sce.Vsh.ShellUI.Settings.CoreUI3";

MonoDomain *current_domain() {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  return domain ? domain : Root_Domain;
}

struct UiManager {
  MonoObject *instance = nullptr;
  MonoMethod *push = nullptr;
  MonoMethod *pop = nullptr;
  MonoMethod *get_page_stack = nullptr;
  MonoClass *stack_class = nullptr;
  MonoMethod *get_under_transition = nullptr;
};

UiManager resolve_manager() {
  UiManager out;
  if (!mono_class_from_name || !mono_class_get_method_from_name ||
      !mono_runtime_invoke)
    return out;

  MonoImage *legacy_image = getDLLimage(legacy_dec.c_str());
  if (!legacy_image)
    return out;

  MonoClass *manager_class =
      mono_class_from_name(legacy_image, kLegacyCoreNamespace, "UIManager");
  if (!manager_class)
    return out;

  MonoMethod *get_instance =
      mono_class_get_method_from_name(manager_class, "get_Instance", 0);
  if (!get_instance)
    return out;

  MonoObject *exc = nullptr;
  out.instance = mono_runtime_invoke(get_instance, nullptr, nullptr, &exc);
  if (exc || !out.instance)
    return {};

  out.push = mono_class_get_method_from_name(manager_class, "Push", 3);
  out.pop = mono_class_get_method_from_name(manager_class, "Pop", 1);
  out.get_page_stack =
      mono_class_get_method_from_name(manager_class, "get_PageStack", 0);

  out.stack_class = mono_class_from_name(legacy_image, kLegacyCoreNamespace,
                                         "SettingPageStack");
  if (out.stack_class) {
    out.get_under_transition = mono_class_get_method_from_name(
        out.stack_class, "get_UnderTransition", 0);
  }

  return out;
}

} // namespace

bool toolbox_push_resource(const char *resource) {
  if (!resource || !resource[0] || !mono_string_new) {
    LOG_ERROR("toolbox_navigation: Legacy navigation API unavailable");
    return false;
  }

  const UiManager manager = resolve_manager();
  MonoDomain *domain = current_domain();
  MonoString *xml = domain ? mono_string_new(domain, resource) : nullptr;
  if (!manager.instance || !manager.push || !xml) {
    LOG_ERROR("toolbox_navigation: failed to resolve UIManager.Push");
    return false;
  }

  int animation = 0;
  void *args[] = {xml, nullptr, &animation};
  MonoObject *exc = nullptr;
  mono_runtime_invoke(manager.push, manager.instance, args, &exc);
  if (exc) {
    LOG_ERROR("toolbox_navigation: UIManager.Push threw");
    return false;
  }

  LOG_DEBUG("toolbox_navigation: pushed %s", resource);
  return true;
}

bool toolbox_pop_page(void) {
  const UiManager manager = resolve_manager();
  if (!manager.instance || !manager.pop) {
    LOG_ERROR("toolbox_navigation: failed to resolve UIManager.Pop");
    return false;
  }

  int animation = 0; // TransitionAnimationType.Default
  void *args[] = {&animation};
  MonoObject *exc = nullptr;
  mono_runtime_invoke(manager.pop, manager.instance, args, &exc);
  if (exc) {
    LOG_ERROR("toolbox_navigation: UIManager.Pop threw");
    return false;
  }

  LOG_DEBUG("toolbox_navigation: popped page");
  return true;
}

bool toolbox_is_stack_under_transition(void) {
  const UiManager manager = resolve_manager();
  if (!manager.instance || !manager.get_page_stack ||
      !manager.get_under_transition || !mono_object_unbox) {
    return false;
  }

  MonoObject *exc = nullptr;
  MonoObject *stack = mono_runtime_invoke(
      manager.get_page_stack, manager.instance, nullptr, &exc);
  if (exc || !stack) {
    return false;
  }

  MonoObject *boxed = mono_runtime_invoke(
      manager.get_under_transition, stack, nullptr, &exc);
  if (exc || !boxed) {
    return false;
  }

  bool *val = static_cast<bool *>(mono_object_unbox(boxed));
  return val ? *val : false;
}


