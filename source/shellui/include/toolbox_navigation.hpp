/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Single programmatic navigation primitive for dynamic Legacy Settings pages.
 * Pushes a Settings.Plugins resource (e.g. "plugin_config.xml") onto the active
 * page stack via UIManager.Instance.Push. The resource is resolved by the
 * GetManifestResourceStream hook, so dynamic XML is served as usual.
 */

#pragma once

/** Push @p resource (relative, e.g. "plugin_config.xml") on the page stack. */
bool toolbox_push_resource(const char *resource);

/** Pop the topmost page from the active Legacy Settings page stack. */
bool toolbox_pop_page(void);

/** Check if the Legacy Settings page stack is currently transitioning. */
bool toolbox_is_stack_under_transition(void);

