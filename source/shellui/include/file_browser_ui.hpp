/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * ShellUI runtime entry points for the lightweight file browser.
 */
#pragma once

#include "onpress.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace onion::shellui::file_browser {

void generate_root_xml(std::string &xml_buffer, std::string_view mode_key);
void generate_node_xml(std::string &xml_buffer, std::uint64_t token);
/** Returns true if a parent file-browser page remains active on the stack. */
bool leave_active_page();
OnPressResult onpress(OnPressContext &ctx);

} // namespace onion::shellui::file_browser
