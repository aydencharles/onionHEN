#include "test_harness.h"
#include "settings_page_update.hpp"
#include "plugin_sprx_pages.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"

#include <algorithm>

using namespace onion::shellui::settings;
using ps5ui::Node;

class FakePage final : public PageView {
public:
  explicit FakePage(const Node &root) : items(root.children), title(root.attrs.title) {}
  std::vector<Node> items;
  std::string title;
  std::vector<std::string> calls;
  int fail_at = -1;
  int attempts = 0;

  bool set_title(const std::string &value) override {
    if (fail()) return false;
    title = value;
    calls.push_back("title");
    return true;
  }

  bool remove(const std::string &id) override {
    if (fail()) return false;
    const auto found = find(id);
    if (found == items.end()) return false;
    items.erase(found);
    calls.push_back("remove:" + id);
    return true;
  }
  bool insert(const Node &node, const std::string &before) override {
    if (fail() || find(node.attrs.id) != items.end()) return false;
    auto position = before.empty() ? items.end() : find(before);
    if (!before.empty() && position == items.end()) return false;
    items.insert(position, node);
    calls.push_back("insert:" + node.attrs.id);
    return true;
  }
  bool update(const Node &, const Node &node) override {
    if (fail()) return false;
    const auto found = find(node.attrs.id);
    if (found == items.end() || found->kind != node.kind) return false;
    *found = node;
    calls.push_back("update:" + node.attrs.id);
    return true;
  }
private:
  bool fail() { return attempts++ == fail_at; }
  std::vector<Node>::iterator find(const std::string &id) {
    return std::find_if(items.begin(), items.end(),
        [&](const Node &node) { return node.attrs.id == id; });
  }
};

static Node model(std::initializer_list<const char *> ids) {
  ps5ui::Page page("root", "Inventory");
  for (const auto *id : ids) page.link(id, id, std::string(id) + ".xml");
  return page.root();
}

static int test_status_changes_reuse_elements() {
  toolbox_i18n::set_lang(toolbox_i18n::Lang::En);
  std::vector<PluginInventoryItem> entries = {
      {.plugin_id = "FTPS00001", .name = "FTP", .running = false, .auto_start = false}};
  ps5ui::Page first("root", "Plugins");
  onion::shellui::plugin_pages::append_plugin_list_links(first, entries);
  Node current = first.root();
  FakePage view(current);
  entries[0].running = true;
  entries[0].auto_start = true;
  ps5ui::Page second("root", "Plugins");
  onion::shellui::plugin_pages::append_plugin_list_links(second, entries);
  TEST_ASSERT_TRUE(reconcile(view, current, second.root()));
  TEST_ASSERT_EQ_INT(1, view.calls.size());
  TEST_ASSERT_TRUE(view.calls[0].starts_with("update:"));
  TEST_ASSERT_TRUE(view.items == second.root().children);
  TEST_ASSERT_TRUE(current == second.root());
  return 0;
}

static int test_structure_and_order() {
  Node current = model({"a", "b", "c"});
  FakePage view(current);
  Node next = model({"c", "d", "a"});
  TEST_ASSERT_TRUE(reconcile(view, current, next));
  TEST_ASSERT_TRUE(view.items == next.children);
  TEST_ASSERT_TRUE(current == next);
  TEST_ASSERT_TRUE(std::find(view.calls.begin(), view.calls.end(), "remove:a") == view.calls.end());
  const auto count = view.calls.size();
  TEST_ASSERT_TRUE(reconcile(view, current, next));
  TEST_ASSERT_EQ_INT(count, view.calls.size());
  next = model({});
  TEST_ASSERT_TRUE(reconcile(view, current, next));
  TEST_ASSERT_TRUE(view.items.empty());
  next = model({"new"});
  TEST_ASSERT_TRUE(reconcile(view, current, next));
  TEST_ASSERT_TRUE(view.items == next.children);
  return 0;
}

static int test_failure_then_retry() {
  for (int fail_at = 0; fail_at < 4; ++fail_at) {
    Node current = model({"a", "b", "c"});
    FakePage view(current);
    view.fail_at = fail_at;
    const Node next = model({"c", "d", "a"});
    TEST_ASSERT_TRUE(!reconcile(view, current, next));
    TEST_ASSERT_TRUE(view.items == current.children);
    view.fail_at = -1;
    TEST_ASSERT_TRUE(reconcile(view, current, next));
    TEST_ASSERT_TRUE(view.items == next.children);
  }
  return 0;
}

static int test_all_orders_and_types() {
  Node base = model({"a", "b", "c", "d"});
  std::vector<Node> desired = base.children;
  do {
    Node current = base;
    FakePage view(current);
    Node next = base;
    next.children = desired;
    TEST_ASSERT_TRUE(reconcile(view, current, next));
    TEST_ASSERT_TRUE(view.items == desired);
  } while (std::next_permutation(desired.begin(), desired.end(),
      [](const Node &a, const Node &b) { return a.attrs.id < b.attrs.id; }));
  FakePage view(base);
  Node next = base;
  next.children[0].kind = Node::Kind::Label;
  TEST_ASSERT_TRUE(reconcile(view, base, next));
  TEST_ASSERT_TRUE(view.items == next.children);
  return 0;
}

static int test_live_toggle_overrides_interaction() {
  ps5ui::Page page("root", "Settings");
  page.toggle("enabled", "Enabled", false);
  Node current = page.root();
  FakePage view(current);
  // A user changed the native widget, but the authoritative source is still false.
  view.items[0].attrs.value = "1";
  TEST_ASSERT_TRUE(reconcile(view, current, page.root()));
  TEST_ASSERT_TRUE(view.items == page.root().children);
  TEST_ASSERT_EQ_INT(1, view.calls.size());
  return 0;
}

static int test_invalid_models_do_not_mutate() {
  Node current = model({"a"});
  FakePage view(current);
  Node next = model({"a", "a"});
  TEST_ASSERT_TRUE(!reconcile(view, current, next));
  next = model({"b"});
  next.children[0].kind = Node::Kind::SettingList;
  TEST_ASSERT_TRUE(!reconcile(view, current, next));
  next = model({"a"});
  next.attrs.id = "different-page";
  TEST_ASSERT_TRUE(!reconcile(view, current, next));
  TEST_ASSERT_TRUE(view.calls.empty());
  return 0;
}

static int test_title_and_removed_description() {
  Node current = model({"a"});
  current.children[0].attrs.second_title = "Old status";
  FakePage view(current);
  Node next = model({"a"});
  next.attrs.title = "Updated name";
  TEST_ASSERT_TRUE(reconcile(view, current, next));
  TEST_ASSERT_STREQ("Updated name", view.title.c_str());
  TEST_ASSERT_TRUE(view.items == next.children);
  TEST_ASSERT_TRUE(current == next);
  return 0;
}

static int test_nested_return_context() {
  using toolbox::Page;
  ToolboxUiState state;
  state.set_active_page(Page::Plugins);
  state.set_active_page(Page::PluginConfig);
  state.active_plugin = "FTPS00001";
  state.set_active_page(Page::DynamicPlugin);
  state.set_active_page(Page::DynamicPlugin);
  state.active_plugin = "temporary-child-context";
  state.leave_page(Page::DynamicPlugin);
  TEST_ASSERT_TRUE(state.active_page == Page::PluginConfig);
  TEST_ASSERT_TRUE(state.parent_page == Page::Plugins);
  TEST_ASSERT_STREQ("FTPS00001", state.active_plugin.c_str());
  state.leave_page(Page::PluginConfig);
  TEST_ASSERT_TRUE(state.active_page == Page::Plugins);
  TEST_ASSERT_TRUE(state.parent_pages.empty());
  state.set_active_page(Page::PluginConfig);
  state.set_active_page(Page::DebugSettings);
  TEST_ASSERT_TRUE(state.parent_pages.empty());
  return 0;
}

extern "C" int test_settings_page_update_suite() {
  int fails = 0;
  fails += onion_test_run("settings.status_reuses_elements", test_status_changes_reuse_elements);
  fails += onion_test_run("settings.structure_order", test_structure_and_order);
  fails += onion_test_run("settings.retry", test_failure_then_retry);
  fails += onion_test_run("settings.permutations", test_all_orders_and_types);
  fails += onion_test_run("settings.interactive_value", test_live_toggle_overrides_interaction);
  fails += onion_test_run("settings.invalid_model", test_invalid_models_do_not_mutate);
  fails += onion_test_run("settings.title_description", test_title_and_removed_description);
  fails += onion_test_run("settings.nested_return", test_nested_return_context);
  return fails;
}
