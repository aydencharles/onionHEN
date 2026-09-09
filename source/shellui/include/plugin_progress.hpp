/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * External plugin start/stop progress page backed by a Legacy Settings user_custom Panel.
 * The worker thread handles IPC and waits for dynamic UI registration.
 * UI3 widget creation and updates stay on the ShellUI thread.
 */
#pragma once

#include "monodef.h"

#include <string>
#include <string_view>

/** Reset progress state and start the detached plugin start/stop worker. */
void plugin_progress_show(std::string_view plugin_id, bool start);

/** Push plugin_progress.xml on the active Legacy Settings page stack. */
bool plugin_progress_open_page(void);

/** Generate the dynamic plugin_progress.xml document. */
void generate_plugin_progress_xml(std::string &xml_buffer);

/** Append the UI3 Panel to the Widget created for the user_custom element. */
void plugin_progress_attach_panel(std::string_view id, MonoObject *widget);

/** Remember the managed SettingPage instance that owns the progress UI. */
void plugin_progress_bind_page(MonoObject *page);

/**
 * Invalidate and clear the progress session when its owning page is popped.
 * Returns true only when @p outgoing is the bound progress page.
 */
bool plugin_progress_handle_popping(MonoObject *outgoing);

/** Apply pending status and percentage to real UI3 widgets on the UI thread. */
void shellui_poll_plugin_progress(void);
