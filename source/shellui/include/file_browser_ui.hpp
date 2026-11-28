/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * ShellUI runtime entry points for the lightweight file browser.
 */
#pragma once

#include "onpress.hpp"

#include <string>
#include <string_view>

namespace onion::shellui::file_browser {

void generate_xml(std::string &xml_buffer, std::string_view mode_key);
OnPressResult onpress(OnPressContext &ctx);

} // namespace onion::shellui::file_browser
