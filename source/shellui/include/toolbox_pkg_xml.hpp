/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Package installer chooser + network installer toggle pages.
 * Kept free of Mono/PS5 globals so host unit tests can assert the exact XML.
 */
#pragma once

#include <string>

namespace toolbox_pkg {

/** USB / network source chooser (pkg_installer.xml). */
void generate_pkg_installer_xml(std::string &xml_buffer);

/** Network installer toggles (pkg_net.xml) with explicit initial states. */
void generate_pkg_net_xml(std::string &xml_buffer, bool run_on,
                          bool autoload_on);

} // namespace toolbox_pkg
