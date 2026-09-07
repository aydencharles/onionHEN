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
};

UiManager resolve_manager() {
  UiManager out;
  if (!mono_class_from_name || !mono_class_get_method_from_name ||
      !mono_runtime_invoke)
    return out;

  MonoImage *legacy_image = getDLLimage(legacy_dec.c_str());
  MonoClass *manager_class =
      legacy_image
          ? mono_class_from_name(legacy_image, kLegacyCoreNamespace, "UIManager")
          : nullptr;
  MonoMethod *get_instance =
      manager_class
          ? mono_class_get_method_from_name(manager_class, "get_Instance", 0)
          : nullptr;
  if (!get_instance)
    return out;

  MonoObject *exc = nullptr;
  out.instance = mono_runtime_invoke(get_instance, nullptr, nullptr, &exc);
  if (exc || !out.instance)
    return {};

  out.push = mono_class_get_method_from_name(manager_class, "Push", 3);
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
