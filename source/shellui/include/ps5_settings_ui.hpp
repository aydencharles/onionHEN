/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Fluent builder for PS5 Debug Settings (system_settings) XML.
 * Business code describes widgets; build() returns the full document string.
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps5ui {

enum class Style { None, Center, Left };

/**
 * Escape for filesystem path attrs (icon): & < > " and / → //.
 * ShellUI resolves icon paths only when slashes are doubled.
 */
std::string escape(std::string_view text);

/**
 * Escape for display attrs (title/second_title/description): & < > " only.
 * Do not double '/' — UI shows // literally in path text.
 */
std::string escape_xml(std::string_view text);

struct Attrs {
  std::string id;
  std::string title;
  std::optional<std::string> second_title;
  std::optional<std::string> description;
  std::optional<std::string> icon;
  std::optional<std::string> file;
  std::optional<std::string> value;
  std::optional<std::string> confirm;
  std::optional<std::string> confirm_phrase;
  std::optional<std::string> keyboard_type;
  std::optional<std::string> min_length;
  std::optional<std::string> max_length;
  std::optional<std::string> key;
  std::optional<std::string> initial_focus_to;
  std::optional<std::string> list_size;
  std::optional<std::string> list_highlight;
  std::optional<std::string> raw_title;
  std::optional<bool> restorable;
  Style style = Style::None;
  bool operator==(const Attrs &) const = default;
};

struct Node {
  enum class Kind {
    SettingList,
    Toggle,
    Button,
    Label,
    Link,
    List,
    ListItem,
    TextField,
    UserCustom,
    Option,
    OptionItem,
  } kind = Kind::SettingList;

  Attrs attrs;
  std::vector<Node> children;
  bool operator==(const Node &) const = default;
};

const char *node_tag(Node::Kind kind);
std::vector<std::pair<std::string, std::string>> node_attributes(const Node &node);
std::string build_document(const Node &root,
                           std::string_view plugin = "debug_settings_plugin");

class ListBuilder {
public:
  ListBuilder& item(std::string id, std::string title, std::string value,
                    std::optional<std::string> icon = std::nullopt);

private:
  template <typename>
  friend class GroupT;
  explicit ListBuilder(Node& list_node) : list_node_(list_node) {}
  Node& list_node_;
};

class Group; // nested scopes always use Group

/**
 * CRTP base so Page::toggle(...).build() type-checks (returns Derived&).
 */
template <typename Derived>
class GroupT {
public:
  Derived& label(std::string id, std::string title, Style style = Style::None,
                 std::optional<std::string> icon = std::nullopt);

  Derived& button(std::string id, std::string title,
                  std::optional<std::string> second_title = std::nullopt,
                  std::optional<std::string> description = std::nullopt,
                  std::optional<std::string> icon = std::nullopt,
                  Style style = Style::None,
                  std::optional<std::string> confirm = std::nullopt,
                  std::optional<std::string> confirm_phrase = std::nullopt);

  Derived& toggle(std::string id, std::string title, bool on,
                  std::optional<std::string> second_title = std::nullopt,
                  std::optional<std::string> description = std::nullopt,
                  std::optional<std::string> icon = std::nullopt,
                  std::optional<std::string> confirm = std::nullopt);

  Derived& link(std::string id, std::string title, std::string file,
                std::optional<std::string> second_title = std::nullopt,
                std::optional<std::string> icon = std::nullopt,
                std::optional<std::string> confirm = std::nullopt,
                std::optional<std::string> confirm_phrase = std::nullopt);

  Derived& list(std::string id, std::string title,
                const std::function<void(ListBuilder&)>& body,
                std::optional<std::string> second_title = std::nullopt,
                std::optional<std::string> value = std::nullopt,
                std::optional<std::string> confirm = std::nullopt,
                std::optional<std::string> confirm_phrase = std::nullopt,
                std::optional<std::string> icon = std::nullopt);

  Derived& text_field(std::string id, std::string title,
                      std::optional<std::string> second_title = std::nullopt,
                      std::optional<std::string> keyboard_type = std::nullopt,
                      std::optional<std::string> min_length = std::nullopt,
                      std::optional<std::string> max_length = std::nullopt,
                      std::optional<std::string> key = std::nullopt,
                      std::optional<std::string> confirm = std::nullopt,
                      std::optional<std::string> confirm_phrase = std::nullopt,
                      std::optional<std::string> value = std::nullopt,
                      std::optional<std::string> icon = std::nullopt);

  /** Legacy Settings placeholder whose real Panel is appended during Reset. */
  Derived& user_custom(std::string id);

  /** Options-button context menu container (page level). */
  Derived& option(std::string id,
                  const std::function<void(Group&)>& body,
                  std::optional<std::string> list_highlight = std::nullopt);

  /** Options-button context menu entry. */
  Derived& option_item(std::string id, std::string title,
                       std::optional<std::string> raw_title = std::nullopt);

  Derived& group(std::string id, std::string title,
                 const std::function<void(Group&)>& body,
                 std::optional<std::string> second_title = std::nullopt,
                 std::optional<std::string> icon = std::nullopt,
                 std::optional<std::string> initial_focus_to = std::nullopt,
                 Style style = Style::None);

  Derived& add(Node node);

protected:
  GroupT() = default;
  void bind(Node* node) { node_ = node; }
  Node* node_ = nullptr;

  Derived& self() { return static_cast<Derived&>(*this); }
};

/** Nested setting_list scope (and the type accepted by group() callbacks). */
class Group : public GroupT<Group> {
public:
  explicit Group(Node* node) { bind(node); }
};

/**
 * Root document: one root setting_list under system_settings.
 * Call build() to get the full XML string.
 */
class Page : public GroupT<Page> {
public:
  Page(std::string root_list_id, std::string root_title,
       std::string plugin = "debug_settings_plugin");

  Page& root_style(Style style);
  Page& root_focus(std::string id);
  Page& root_icon(std::string icon);
  Page& root_list_size(std::string size);
  Page& root_restorable(bool restorable);

  /** Serialize to a complete system_settings document. */
  [[nodiscard]] std::string build() const;
  const Node &root() const { return root_; }

private:
  std::string plugin_;
  Node root_;
};

// ---- template method definitions ----

template <typename Derived>
Derived& GroupT<Derived>::label(std::string id, std::string title, Style style,
                                std::optional<std::string> icon) {
  Node n;
  n.kind = Node::Kind::Label;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.style = style;
  n.attrs.icon = std::move(icon);
  return add(std::move(n));
}

template <typename Derived>
Derived& GroupT<Derived>::button(std::string id, std::string title,
                                 std::optional<std::string> second_title,
                                 std::optional<std::string> description,
                                 std::optional<std::string> icon, Style style,
                                 std::optional<std::string> confirm,
                                 std::optional<std::string> confirm_phrase) {
  Node n;
  n.kind = Node::Kind::Button;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.second_title = std::move(second_title);
  n.attrs.description = std::move(description);
  n.attrs.icon = std::move(icon);
  n.attrs.style = style;
  n.attrs.confirm = std::move(confirm);
  n.attrs.confirm_phrase = std::move(confirm_phrase);
  return add(std::move(n));
}

template <typename Derived>
Derived& GroupT<Derived>::toggle(std::string id, std::string title, bool on,
                                 std::optional<std::string> second_title,
                                 std::optional<std::string> description,
                                 std::optional<std::string> icon,
                                 std::optional<std::string> confirm) {
  Node n;
  n.kind = Node::Kind::Toggle;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.second_title = std::move(second_title);
  n.attrs.description = std::move(description);
  n.attrs.icon = std::move(icon);
  n.attrs.confirm = std::move(confirm);
  n.attrs.value = on ? "1" : "0";
  return add(std::move(n));
}

template <typename Derived>
Derived& GroupT<Derived>::link(std::string id, std::string title, std::string file,
                               std::optional<std::string> second_title,
                               std::optional<std::string> icon,
                               std::optional<std::string> confirm,
                               std::optional<std::string> confirm_phrase) {
  Node n;
  n.kind = Node::Kind::Link;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.file = std::move(file);
  n.attrs.second_title = std::move(second_title);
  n.attrs.icon = std::move(icon);
  n.attrs.confirm = std::move(confirm);
  n.attrs.confirm_phrase = std::move(confirm_phrase);
  return add(std::move(n));
}

template <typename Derived>
Derived& GroupT<Derived>::list(std::string id, std::string title,
                               const std::function<void(ListBuilder&)>& body,
                               std::optional<std::string> second_title,
                               std::optional<std::string> value,
                               std::optional<std::string> confirm,
                               std::optional<std::string> confirm_phrase,
                               std::optional<std::string> icon) {
  Node n;
  n.kind = Node::Kind::List;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.second_title = std::move(second_title);
  n.attrs.value = std::move(value);
  n.attrs.confirm = std::move(confirm);
  n.attrs.confirm_phrase = std::move(confirm_phrase);
  n.attrs.icon = std::move(icon);
  node_->children.push_back(std::move(n));
  ListBuilder lb(node_->children.back());
  if (body)
    body(lb);
  return self();
}

template <typename Derived>
Derived& GroupT<Derived>::text_field(std::string id, std::string title,
                                     std::optional<std::string> second_title,
                                     std::optional<std::string> keyboard_type,
                                     std::optional<std::string> min_length,
                                     std::optional<std::string> max_length,
                                     std::optional<std::string> key,
                                     std::optional<std::string> confirm,
                                     std::optional<std::string> confirm_phrase,
                                     std::optional<std::string> value,
                                     std::optional<std::string> icon) {
  Node n;
  n.kind = Node::Kind::TextField;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.second_title = std::move(second_title);
  n.attrs.keyboard_type = std::move(keyboard_type);
  n.attrs.min_length = std::move(min_length);
  n.attrs.max_length = std::move(max_length);
  n.attrs.key = std::move(key);
  n.attrs.confirm = std::move(confirm);
  n.attrs.confirm_phrase = std::move(confirm_phrase);
  n.attrs.value = std::move(value);
  n.attrs.icon = std::move(icon);
  return add(std::move(n));
}

template <typename Derived>
Derived& GroupT<Derived>::user_custom(std::string id) {
  Node n;
  n.kind = Node::Kind::UserCustom;
  n.attrs.id = std::move(id);
  return add(std::move(n));
}

template <typename Derived>
Derived& GroupT<Derived>::option(std::string id,
                                 const std::function<void(Group&)>& body,
                                 std::optional<std::string> list_highlight) {
  Node n;
  n.kind = Node::Kind::Option;
  n.attrs.id = std::move(id);
  n.attrs.list_highlight = std::move(list_highlight);
  node_->children.push_back(std::move(n));
  Group sub(&node_->children.back());
  if (body)
    body(sub);
  return self();
}

template <typename Derived>
Derived& GroupT<Derived>::option_item(std::string id, std::string title,
                                      std::optional<std::string> raw_title) {
  Node n;
  n.kind = Node::Kind::OptionItem;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.raw_title = std::move(raw_title);
  return add(std::move(n));
}

template <typename Derived>
Derived& GroupT<Derived>::group(std::string id, std::string title,
                                const std::function<void(Group&)>& body,
                                std::optional<std::string> second_title,
                                std::optional<std::string> icon,
                                std::optional<std::string> initial_focus_to,
                                Style style) {
  Node n;
  n.kind = Node::Kind::SettingList;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.second_title = std::move(second_title);
  n.attrs.icon = std::move(icon);
  n.attrs.initial_focus_to = std::move(initial_focus_to);
  n.attrs.style = style;
  node_->children.push_back(std::move(n));
  Group sub(&node_->children.back());
  if (body)
    body(sub);
  return self();
}

template <typename Derived>
Derived& GroupT<Derived>::add(Node node) {
  node_->children.push_back(std::move(node));
  return self();
}

} // namespace ps5ui
