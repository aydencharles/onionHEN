#include "settings_page_update.hpp"

#include <algorithm>
#include <set>

namespace onion::shellui::settings {

bool valid_live_model(const ps5ui::Node &root) {
  if (root.kind != ps5ui::Node::Kind::SettingList || root.attrs.id.empty())
    return false;
  std::set<std::string> ids;
  for (const auto &node : root.children) {
    using Kind = ps5ui::Node::Kind;
    if (!node.children.empty() || node.attrs.id.empty() ||
        !ids.insert(node.attrs.id).second ||
        (node.kind != Kind::Link && node.kind != Kind::Toggle &&
         node.kind != Kind::Label && node.kind != Kind::Button))
      return false;
  }
  return true;
}

bool reconcile(PageView &view, ps5ui::Node &current, const ps5ui::Node &next) {
  if (!valid_live_model(current) || !valid_live_model(next) ||
      current.attrs.id != next.attrs.id)
    return false;
  auto supported_root = current.attrs;
  supported_root.title = next.attrs.title;
  if (supported_root != next.attrs) return false;
  if (current.attrs.title != next.attrs.title) {
    if (!view.set_title(next.attrs.title)) return false;
    current.attrs.title = next.attrs.title;
  }
  auto &items = current.children;
  for (auto it = items.begin(); it != items.end();) {
    const bool keep = std::any_of(next.children.begin(), next.children.end(),
        [&](const auto &node) {
          return node.attrs.id == it->attrs.id && node.kind == it->kind;
        });
    if (keep) { ++it; continue; }
    if (!view.remove(it->attrs.id)) return false;
    it = items.erase(it);
  }
  for (size_t i = 0; i < next.children.size(); ++i) {
    const auto &wanted = next.children[i];
    auto found = std::find_if(items.begin() + i, items.end(),
        [&](const auto &node) { return node.attrs.id == wanted.attrs.id; });
    if (found != items.end() && found != items.begin() + i) {
      if (!view.remove(found->attrs.id)) return false;
      items.erase(found);
      found = items.end();
    }
    if (found == items.end()) {
      const std::string before = i < items.size() ? items[i].attrs.id : "";
      if (!view.insert(wanted, before)) return false;
      items.insert(items.begin() + i, wanted);
    } else if (*found != wanted || wanted.attrs.value) {
      // Interactive values may have changed in the widget since the snapshot.
      if (!view.update(*found, wanted)) return false;
      *found = wanted;
    }
  }
  return true;
}

} // namespace onion::shellui::settings
