#include "dynamic_ui_xml.hpp"

#include "ps5_settings_ui.hpp"

#include <algorithm>
#include <cstdio>

namespace onion::shellui::dynamic_ui {
namespace {

uint64_t fnv1a(std::string_view first, std::string_view second,
               std::string_view third) {
  uint64_t hash = 1469598103934665603ull;
  const auto append = [&hash](std::string_view text) {
    for (unsigned char c : text) {
      hash ^= c;
      hash *= 1099511628211ull;
    }
    hash ^= 0xff;
    hash *= 1099511628211ull;
  };
  append(first);
  append(second);
  append(third);
  return hash;
}

std::string token(const char *prefix, const plugin_ui::Document &document,
                  std::string_view local_id, const char *suffix) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%s%016llx%s", prefix,
                static_cast<unsigned long long>(
                    fnv1a(document.plugin_id, document.contribution_id, local_id)),
                suffix);
  return buffer;
}

bool is_child(const plugin_ui::Node &node, std::string_view parent_id) {
  return node.parent_id == parent_id;
}

bool bool_value(std::string_view value) {
  return value == "1" || value == "true";
}

ps5ui::Node render_node(const plugin_ui::Document &document,
                        const plugin_ui::Node &source,
                        const FirmwareProfile &profile, bool &supported) {
  ps5ui::Node node;
  if ((source.flags & ((1u << 0) | (1u << 2))) != 0) {
    supported = false;
    return node;
  }
  node.attrs.id = control_id(document, source.id);
  node.attrs.title = source.title;
  if (!source.description.empty()) node.attrs.description = source.description;
  if ((source.flags & (1u << 1)) != 0) node.attrs.confirm = "true";

  switch (source.kind) {
  case plugin_ui::NodeKind::Group:
    if (!profile.nested_groups) {
      supported = false;
      return node;
    }
    node.kind = ps5ui::Node::Kind::SettingList;
    break;
  case plugin_ui::NodeKind::Label:
    node.kind = ps5ui::Node::Kind::Label;
    break;
  case plugin_ui::NodeKind::Action:
    node.kind = ps5ui::Node::Kind::Button;
    break;
  case plugin_ui::NodeKind::Toggle:
    node.kind = ps5ui::Node::Kind::Toggle;
    node.attrs.value = bool_value(source.value) ? "1" : "0";
    break;
  case plugin_ui::NodeKind::List:
    node.kind = ps5ui::Node::Kind::List;
    node.attrs.value = source.value;
    break;
  case plugin_ui::NodeKind::ListItem:
    node.kind = ps5ui::Node::Kind::ListItem;
    node.attrs.value = source.value;
    break;
  case plugin_ui::NodeKind::Input:
    if (!profile.text_input) {
      supported = false;
      return node;
    }
    node.kind = ps5ui::Node::Kind::TextField;
    node.attrs.value = source.value;
    if (source.value_type == plugin_ui::ValueType::Integer)
      node.attrs.keyboard_type = "number";
    if (source.min_length != 0)
      node.attrs.min_length = std::to_string(source.min_length);
    if (source.max_length != 0)
      node.attrs.max_length = std::to_string(source.max_length);
    break;
  case plugin_ui::NodeKind::Menu:
    node.kind = ps5ui::Node::Kind::Link;
    node.attrs.file = resource_name(document, source.target_id);
    break;
  case plugin_ui::NodeKind::Page:
    supported = false;
    return node;
  }

  if (source.kind == plugin_ui::NodeKind::Group ||
      source.kind == plugin_ui::NodeKind::List) {
    for (const plugin_ui::Node &child : document.nodes) {
      if (!is_child(child, source.id)) continue;
      ps5ui::Node rendered = render_node(document, child, profile, supported);
      if (!supported) return node;
      node.children.push_back(std::move(rendered));
    }
  }
  return node;
}

bool resource_matches(std::string_view resource, std::string_view relative) {
  if (resource == relative) return true;
  return resource.size() > relative.size() && resource.ends_with(relative) &&
         resource[resource.size() - relative.size() - 1] == '.';
}

} // namespace

FirmwareProfile FirmwareProfile::for_system_version(uint32_t system_version) {
  if (system_version >= 0x02300000u && system_version <= 0x12ffffffu)
    return {"legacy-settings-2.x-12.x", true, true, true};
  return {};
}

std::string resource_name(const plugin_ui::Document &document,
                          std::string_view page_id) {
  return token("onion_ui_", document, page_id, ".xml");
}

std::string control_id(const plugin_ui::Document &document,
                       std::string_view node_id) {
  return token("id_onion_ui_", document, node_id, "");
}

const plugin_ui::ContributionSnapshot *resolve_resource(
    const plugin_ui::RegistrySnapshot &snapshot, std::string_view resource,
    std::string *out_page_id) {
  for (const plugin_ui::ContributionSnapshot &entry : snapshot.contributions) {
    if (!entry.document) continue;
    for (const plugin_ui::Node &node : entry.document->nodes) {
      if (node.kind != plugin_ui::NodeKind::Page) continue;
      if (resource_matches(resource, resource_name(*entry.document, node.id))) {
        if (out_page_id) *out_page_id = node.id;
        return &entry;
      }
    }
  }
  return nullptr;
}

const plugin_ui::Node *resolve_control(const plugin_ui::Document &document,
                                       std::string_view page_id,
                                       std::string_view xml_control_id) {
  const plugin_ui::Node *page = document.find_node(page_id);
  if (!page || page->kind != plugin_ui::NodeKind::Page) return nullptr;
  for (const plugin_ui::Node &node : document.nodes) {
    if (node.kind == plugin_ui::NodeKind::Page) continue;
    if (control_id(document, node.id) != xml_control_id) continue;
    const plugin_ui::Node *cursor = &node;
    while (!cursor->parent_id.empty()) {
      if (cursor->parent_id == page_id) return &node;
      cursor = document.find_node(cursor->parent_id);
      if (!cursor) break;
    }
  }
  return nullptr;
}

RenderResult render_page(const plugin_ui::Document &document,
                         std::string_view page_id,
                         const FirmwareProfile &profile) {
  if (!profile.supported)
    return {RenderStatus::UnsupportedFirmware, {}, "unsupported ShellUI firmware"};
  std::string validation_error;
  if (plugin_ui::validate_document(document, &validation_error) !=
      plugin_ui::Status::Ok) {
    return {RenderStatus::InvalidDocument, {}, std::move(validation_error)};
  }
  const plugin_ui::Node *page_node = document.find_node(page_id);
  if (!page_node || page_node->kind != plugin_ui::NodeKind::Page)
    return {RenderStatus::PageNotFound, {}, "UI page was not found"};

  ps5ui::Page page(control_id(document, page_node->id), page_node->title);
  bool supported = true;
  for (const plugin_ui::Node &child : document.nodes) {
    if (!is_child(child, page_id)) continue;
    ps5ui::Node rendered = render_node(document, child, profile, supported);
    if (!supported)
      return {RenderStatus::UnsupportedNode, {}, "firmware cannot render a UI node"};
    page.add(std::move(rendered));
  }
  return {RenderStatus::Ok, page.build(), {}};
}

} // namespace onion::shellui::dynamic_ui
