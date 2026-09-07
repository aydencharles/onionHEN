/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * PS5 Debug Settings XML fluent builder implementation.
 */

#include "ps5_settings_ui.hpp"

#include <sstream>

namespace ps5ui {
namespace {

const char* style_attr(Style style) {
  switch (style) {
  case Style::Center:
    return "center";
  case Style::Left:
    return "left";
  case Style::None:
  default:
    return nullptr;
  }
}

const char* kind_tag(Node::Kind kind) {
  switch (kind) {
  case Node::Kind::SettingList:
    return "setting_list";
  case Node::Kind::Toggle:
    return "toggle_switch";
  case Node::Kind::Button:
    return "button";
  case Node::Kind::Label:
    return "label";
  case Node::Kind::Link:
    return "link";
  case Node::Kind::List:
    return "list";
  case Node::Kind::ListItem:
    return "list_item";
  case Node::Kind::TextField:
    return "text_field";
  case Node::Kind::UserCustom:
    return "user_custom";
  case Node::Kind::Option:
    return "option";
  case Node::Kind::OptionItem:
    return "option_item";
  }
  return "unknown";
}

bool is_container(Node::Kind kind) {
  return kind == Node::Kind::SettingList || kind == Node::Kind::List ||
         kind == Node::Kind::Option;
}

void write_attr(std::ostringstream& out, const char* key, std::string_view value,
                bool path_escape) {
  out << ' ' << key << "=\""
      << (path_escape ? escape(value) : escape_xml(value)) << '"';
}

void write_open_tag(std::ostringstream& out, const Node& node, bool self_close) {
  out << '<' << kind_tag(node.kind);
  for (const auto &[key, value] : node_attributes(node))
    write_attr(out, key.c_str(), value, false);
  out << (self_close ? "/>\n" : ">\n");
}

void serialize_node(std::ostringstream& out, const Node& node) {
  if (is_container(node.kind)) {
    write_open_tag(out, node, /*self_close=*/false);
    for (const Node& child : node.children)
      serialize_node(out, child);
    out << "</" << kind_tag(node.kind) << ">\n";
  } else {
    write_open_tag(out, node, /*self_close=*/true);
  }
}

} // namespace

const char *node_tag(Node::Kind kind) { return kind_tag(kind); }

std::vector<std::pair<std::string, std::string>> node_attributes(const Node &node) {
  std::vector<std::pair<std::string, std::string>> result;
  const auto &a = node.attrs;
  result.emplace_back("id", a.id);
  if (node.kind != Node::Kind::UserCustom && !a.title.empty())
    result.emplace_back("title", a.title);
  const auto optional = [&](const char *key, const auto &value) {
    if (value) result.emplace_back(key, *value);
  };
  optional("second_title", a.second_title);
  optional("description", a.description);
  if (a.icon) {
    std::string path;
    for (char c : *a.icon) {
      path += c;
      if (c == '/') path += '/';
    }
    result.emplace_back("icon", std::move(path));
  }
  optional("file", a.file);
  optional("key", a.key);
  optional("keyboard_type", a.keyboard_type);
  optional("min_length", a.min_length);
  optional("max_length", a.max_length);
  optional("value", a.value);
  optional("confirm", a.confirm);
  optional("confirm_phrase", a.confirm_phrase);
  optional("initial_focus_to", a.initial_focus_to);
  optional("list_size", a.list_size);
  optional("list_highlight", a.list_highlight);
  optional("raw_title", a.raw_title);
  if (a.restorable)
    result.emplace_back("restorable", *a.restorable ? "true" : "false");
  if (const char *style = style_attr(a.style)) result.emplace_back("style", style);
  return result;
}

std::string escape_xml(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

std::string escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '/':
      out += "//";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

ListBuilder& ListBuilder::item(std::string id, std::string title,
                              std::string value,
                              std::optional<std::string> icon) {
  Node n;
  n.kind = Node::Kind::ListItem;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.value = std::move(value);
  n.attrs.icon = std::move(icon);
  list_node_.children.push_back(std::move(n));
  return *this;
}

Page::Page(std::string root_list_id, std::string root_title, std::string plugin)
    : plugin_(std::move(plugin)) {
  root_.kind = Node::Kind::SettingList;
  root_.attrs.id = std::move(root_list_id);
  root_.attrs.title = std::move(root_title);
  bind(&root_);
}

Page& Page::root_style(Style style) {
  root_.attrs.style = style;
  return *this;
}

Page& Page::root_focus(std::string id) {
  root_.attrs.initial_focus_to = std::move(id);
  return *this;
}

Page& Page::root_icon(std::string icon) {
  root_.attrs.icon = std::move(icon);
  return *this;
}

Page& Page::root_list_size(std::string size) {
  root_.attrs.list_size = std::move(size);
  return *this;
}

Page& Page::root_restorable(bool restorable) {
  root_.attrs.restorable = restorable;
  return *this;
}

std::string Page::build() const {
  return build_document(root_, plugin_);
}

std::string build_document(const Node &root, std::string_view plugin) {
  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
      << "<system_settings version=\"1.0\" plugin=\"" << escape_xml(plugin)
      << "\">\n";
  serialize_node(out, root);
  out << "</system_settings>\n";
  return out.str();
}

} // namespace ps5ui
