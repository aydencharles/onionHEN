/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Package installer source chooser and network installer toggle pages.
 * Pure XML builders: host-testable (no Mono/PS5 globals).
 */
#include "toolbox_pkg_xml.hpp"

#include "ps5_settings_ui.hpp"
#include "toolbox_i18n.hpp"

namespace toolbox_pkg {

void generate_pkg_installer_xml(std::string &xml_buffer) {
  ps5ui::Page page("id_pkg_installer", toolbox_i18n::tr("pkg.installer"));
  page.link("id_pkg_installer_usb", toolbox_i18n::tr("pkg.usb"),
            "PkgInstaller/data/pkginstaller.xml",
            toolbox_i18n::tr("pkg.usb.sub"))
      .link("id_pkg_installer_network", toolbox_i18n::tr("pkg.network"),
            "pkg_net.xml", toolbox_i18n::tr("pkg.network.sub"));
  xml_buffer = page.build();
}

void generate_pkg_net_xml(std::string &xml_buffer, bool run_on,
                          bool autoload_on) {
  ps5ui::Page page("id_pkg_net", toolbox_i18n::tr("pkgnet.group"));
  page.toggle("id_pkgnet_run", toolbox_i18n::tr("pkgnet.run"), run_on,
              toolbox_i18n::tr("pkgnet.run.sub"))
      .toggle("id_pkgnet_autoload", toolbox_i18n::tr("pkgnet.autoload"),
              autoload_on, toolbox_i18n::tr("pkgnet.autoload.sub"));
  xml_buffer = page.build();
}

} // namespace toolbox_pkg
