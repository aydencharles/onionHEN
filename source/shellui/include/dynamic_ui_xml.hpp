#pragma once

#include <onion/plugin_ui.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace onion::shellui::dynamic_ui {

struct FirmwareProfile {
  const char *name = "unsupported";
  bool supported = false;
  bool text_input = false;
  bool nested_groups = false;

  static FirmwareProfile for_system_version(uint32_t system_version);
};

enum class RenderStatus {
  Ok = 0,
  UnsupportedFirmware,
  PageNotFound,
  UnsupportedNode,
  InvalidDocument,
};

struct RenderResult {
  RenderStatus status = RenderStatus::InvalidDocument;
  std::string xml;
  std::string error;
};

std::string resource_name(const plugin_ui::Document &document,
                          std::string_view page_id);
std::string control_id(const plugin_ui::Document &document,
                       std::string_view node_id);
const plugin_ui::ContributionSnapshot *resolve_resource(
    const plugin_ui::RegistrySnapshot &snapshot, std::string_view resource,
    std::string *out_page_id = nullptr);
const plugin_ui::Node *resolve_control(const plugin_ui::Document &document,
                                       std::string_view page_id,
                                       std::string_view xml_control_id);
RenderResult render_page(const plugin_ui::Document &document,
                         std::string_view page_id,
                         const FirmwareProfile &profile);

} // namespace onion::shellui::dynamic_ui

