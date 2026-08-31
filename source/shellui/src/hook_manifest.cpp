/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */

#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include "plugins_registry.hpp"
#include "shellui_state.hpp"
#include "onpress_policy.hpp"
#include "progress_dialog.hpp"
#include "remote_play.hpp"
#include "toolbox_route.hpp"
#include <onion/platform.h>
#include <string>

extern MonoClass *MemoryStream_IO;
extern MonoObject *MemoryStream_Instance;
extern std::string payloads_xml, debug_settings_xml, cheats_xml;
extern std::string UI3_dec, legacy_dec;
void generate_payload_xml(std::string &xml_buffer, bool list_page);
void generate_plugins_xml(std::string &xml_buffer);
void generate_plugin_config_xml(std::string &xml_buffer);
void generate_account_xml(std::string &xml_buffer);
void generate_plapps_xml(std::string &new_xml);
void generate_toolbox_xml(std::string &new_xml);
void generate_pkg_installer_xml(std::string &xml_buffer);
void generate_pkg_net_xml(std::string &xml_buffer);
void generate_cheats_xml(std::string &new_xml, std::string &not_open_tid,
                         bool running_as_debug_settings,
                         bool show_while_not_open);
static MonoDomain *current_mono_domain() {
  MonoDomain *domain = (mono_domain_get ? mono_domain_get() : nullptr);
  return domain ? domain : Root_Domain;
}

uint64_t GetManifestResourceStream_Hook(uint64_t inst, MonoString *FileName) {
  if (!shellui_hooks_are_ready())
    return GetManifestResourceStream_Original
               ? GetManifestResourceStream_Original(inst, FileName)
               : 0;

  std::string new_xml_string;
  std::string resourceName = Mono_to_String(FileName);
  MonoDomain *domain = current_mono_domain();

#if SHELL_DEBUG == 1
  LOG_DEBUG("GetManifestResourceStream_Hook: %s domain=%p root=%p",
              resourceName.c_str(), (void *)domain, (void *)Root_Domain);
#endif

  const bool shortcut = g_ui.any_cheat_shortcut();
  const bool shortcut_not_open = g_ui.cheats_shortcut_activated_not_open;

  toolbox::RouteResult route = toolbox::resolve_resource({
      .resource = resourceName,
      .names =
          {
              .payloads_xml = payloads_xml,
              .debug_settings_xml = debug_settings_xml,
              .cheats_xml = cheats_xml,
          },
      .cheats_shortcut = g_ui.cheats_shortcut_activated,
      .cheats_shortcut_not_open = g_ui.cheats_shortcut_activated_not_open,
  });

  g_ui.set_active_page(toolbox::active_page_after_resource(
      g_ui.active_page, route.page, resourceName));

  if (route.page == toolbox::Page::RedirectOgDebug) {
    MonoString *debug_resource =
        (domain && mono_string_new)
            ? mono_string_new(domain, debug_settings_xml.c_str())
            : FileName;
    return GetManifestResourceStream_Original(
        inst, debug_resource);
  }

  if (route.page == toolbox::Page::None ||
      route.page == toolbox::Page::SuperuserPass) {
    return GetManifestResourceStream_Original(inst, FileName);
  }

  if (!MemoryStream_IO) {
    if (!domain) {
      LOG_DEBUG("GetManifestResourceStream_Hook: no mono domain");
      return GetManifestResourceStream_Original(inst, FileName);
    }
    MonoAssembly *Assembly = mono_domain_assembly_open(
        domain, "/system_ex/common_ex/lib/mscorlib.dll");
    if (!Assembly) {
      LOG_ERROR("Failed to open mscorlib assembly");
      return GetManifestResourceStream_Original(inst, FileName);
    }
    MonoImage *mscorelib_image = mono_assembly_get_image(Assembly);
    if (!mscorelib_image) {
      LOG_ERROR("Failed to get mscorelib image");
      return GetManifestResourceStream_Original(inst, FileName);
    }

    MemoryStream_IO =
        mono_class_from_name(mscorelib_image, "System.IO", "MemoryStream");
    if (!MemoryStream_IO) {
      LOG_ERROR("Failed to open class MemoryStream");
      return GetManifestResourceStream_Original(inst, FileName);
    }
  }

  switch (route.page) {
  case toolbox::Page::DebugSettings:
    LoadSettings();
    generate_toolbox_xml(new_xml_string);
    break;
  case toolbox::Page::Payloads:
    g_ui.payloads_list.clear();
    generate_payload_xml(new_xml_string, true);
    break;
  case toolbox::Page::Plugins:
    generate_plugins_xml(new_xml_string);
    break;
  case toolbox::Page::PluginConfig: {
    const onion::plugins::Descriptor *d =
        onion::plugins::find_by_config_xml_resource(resourceName);
    g_ui.active_plugin =
        d ? std::string(d->key) : std::string(onion::plugins::default_key());
    generate_plugin_config_xml(new_xml_string);
    break;
  }
  case toolbox::Page::Cheats:
    generate_cheats_xml(new_xml_string, g_ui.current_menu_tid, shortcut,
                        shortcut_not_open);
    if (route.clear_cheat_shortcuts_after)
      g_ui.clear_cheat_shortcuts();
    break;
  case toolbox::Page::AutoPayloads:
    g_ui.auto_payloads_list.clear();
    generate_payload_xml(new_xml_string, false);
    break;
  case toolbox::Page::Account:
    generate_account_xml(new_xml_string);
    break;
  case toolbox::Page::Plapps:
    g_ui.payloads_apps_list.clear();
    generate_plapps_xml(new_xml_string);
    break;
  case toolbox::Page::CheatProgress:
    generate_cheat_progress_xml(new_xml_string);
    break;
  case toolbox::Page::RemotePlay:
    generate_remote_play_xml(new_xml_string);
    break;
  case toolbox::Page::PkgInstaller:
    generate_pkg_installer_xml(new_xml_string);
    break;
  case toolbox::Page::PkgNet:
    generate_pkg_net_xml(new_xml_string);
    break;
  default:
    return GetManifestResourceStream_Original(inst, FileName);
  }

  MemoryStream_Instance = New_Mono_XML_From_String(new_xml_string, domain);
  if (!MemoryStream_Instance) {
    return GetManifestResourceStream_Original(inst, FileName);
  }

  return (uint64_t)MemoryStream_Instance;
}
