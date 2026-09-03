/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Built-in plugin registry. Pure data + lookups (host-testable, no Mono/PS5).
 * The plugins page and its per-plugin config pages are driven by this table,
 * so adding a built-in plugin only extends the registry (open/closed).
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace onion::plugins {

/** Fixed Legacy resource prefix (Sony Settings.Plugins module name is fixed). */
inline constexpr std::string_view kConfigResourcePrefix =
    "Sce.Vsh.ShellUI.Legacy.src.Sce.Vsh.ShellUI.Settings.Plugins.";

struct Descriptor {
  const char *key;        /* stable plugin key, e.g. "kstuff" */
  const char *toggle_id;  /* XML list_item id on the plugins page, "id_plugin_*" */
  const char *config_xml; /* relative config page resource, e.g. "kstuff.xml" */
  const char *title_key;  /* i18n key for the entry title */
  const char *sub_key;    /* i18n key for the entry description */
};

/* Order is the display order on the Plugins page. */
inline constexpr Descriptor kRegistry[] = {
    {"kstuff", "id_plugin_kstuff", "kstuff.xml", "plugin.kstuff.title",
     "plugin.kstuff.sub"},
};

inline constexpr std::size_t kRegistrySize =
    sizeof(kRegistry) / sizeof(kRegistry[0]);

inline const Descriptor *find_by_key(std::string_view key) {
  for (const auto &d : kRegistry)
    if (key == d.key)
      return &d;
  return nullptr;
}

inline const Descriptor *find_by_toggle_id(std::string_view id) {
  for (const auto &d : kRegistry)
    if (id == d.toggle_id)
      return &d;
  return nullptr;
}

/** Resolve a plugin by its full Settings.Plugins config resource name. */
inline const Descriptor *find_by_config_xml_resource(std::string_view resource) {
  for (const auto &d : kRegistry)
    if (resource == std::string(kConfigResourcePrefix) + d.config_xml)
      return &d;
  return nullptr;
}

/** Fallback key used before any plugin has been focused. */
inline constexpr std::string_view default_key() { return kRegistry[0].key; }

} // namespace onion::plugins
