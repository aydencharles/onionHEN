#include "dynamic_ui_runtime.hpp"

#include "dynamic_ui_xml.hpp"
#include "ps5_settings_ui.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace onion::shellui::dynamic_ui {
namespace {

struct RuntimeState {
  std::mutex mutex;
  FirmwareProfile profile;
  plugin_ui::RegistrySnapshot snapshot;
  struct ActivePage {
    plugin_ui::ContributionSnapshot contribution;
    std::string page_id;
  };
  std::vector<ActivePage> page_stack;
  ActionSink action_sink = nullptr;
  void *action_context = nullptr;
};

RuntimeState &state() {
  static RuntimeState runtime;
  return runtime;
}

bool interactive(const plugin_ui::Node &node) {
  return node.kind == plugin_ui::NodeKind::Action ||
         node.kind == plugin_ui::NodeKind::Toggle ||
         node.kind == plugin_ui::NodeKind::List ||
         node.kind == plugin_ui::NodeKind::Input;
}

} // namespace

void configure(uint32_t system_version) {
  RuntimeState &runtime = state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  runtime.profile = FirmwareProfile::for_system_version(system_version);
}

void replace_snapshot(plugin_ui::RegistrySnapshot snapshot) {
  RuntimeState &runtime = state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  runtime.snapshot = std::move(snapshot);
  std::erase_if(runtime.page_stack, [&](RuntimeState::ActivePage &page) {
    const plugin_ui::ContributionSnapshot *updated =
        runtime.snapshot.find(page.contribution.handle);
    if (!updated || !updated->document ||
        !updated->document->find_node(page.page_id))
      return true;
    page.contribution = *updated;
    return false;
  });
}

void set_action_sink(ActionSink sink, void *context) {
  RuntimeState &runtime = state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  runtime.action_sink = sink;
  runtime.action_context = context;
}

std::vector<PluginSettingsLink> plugin_settings_links() {
  RuntimeState &runtime = state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  std::vector<const plugin_ui::ContributionSnapshot *> entries;
  for (const plugin_ui::ContributionSnapshot &entry :
       runtime.snapshot.contributions) {
    if (entry.document &&
        entry.document->find_node(entry.document->root_page_id))
      entries.push_back(&entry);
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto *left, const auto *right) {
              if (left->document->priority != right->document->priority)
                return left->document->priority > right->document->priority;
              return left->document->plugin_id < right->document->plugin_id;
            });
  std::vector<PluginSettingsLink> links;
  links.reserve(entries.size());
  for (const plugin_ui::ContributionSnapshot *entry : entries) {
    const plugin_ui::Document &document = *entry->document;
    links.push_back({document.plugin_id,
                     control_id(document, document.root_page_id),
                     document.title,
                     resource_name(document, document.root_page_id),
                     document.description});
  }
  return links;
}

void append_plugin_links(ps5ui::Page &page,
                         const std::vector<std::string> &matched_link_ids) {
  append_plugin_links(page, plugin_settings_links(), matched_link_ids);
}

void append_plugin_links(
    ps5ui::Page &page, const std::vector<PluginSettingsLink> &links,
    const std::vector<std::string> &matched_link_ids) {
  for (const PluginSettingsLink &link : links) {
    if (std::find(matched_link_ids.begin(), matched_link_ids.end(),
                  link.control_id) != matched_link_ids.end())
      continue;
    page.link(link.control_id, link.title, link.resource,
              link.description.empty()
                  ? std::optional<std::string>{}
                  : std::optional<std::string>{link.description});
  }
}

bool render_resource(std::string_view resource, std::string &out_xml) {
  RuntimeState &runtime = state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  std::string page_id;
  const plugin_ui::ContributionSnapshot *entry =
      resolve_resource(runtime.snapshot, resource, &page_id);
  if (!entry || !entry->document) return false;
  RenderResult rendered = render_page(*entry->document, page_id, runtime.profile);
  if (rendered.status != RenderStatus::Ok) return false;
  out_xml = std::move(rendered.xml);
  const bool already_active = !runtime.page_stack.empty() &&
      runtime.page_stack.back().contribution.handle == entry->handle &&
      runtime.page_stack.back().page_id == page_id;
  if (!already_active)
    runtime.page_stack.push_back({*entry, std::move(page_id)});
  return true;
}

bool resolve_control_value(std::string_view xml_control_id,
                           std::string &out_value) {
  RuntimeState &runtime = state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (runtime.page_stack.empty() ||
      !runtime.page_stack.back().contribution.document)
    return false;
  const RuntimeState::ActivePage &page = runtime.page_stack.back();
  const plugin_ui::Node *node =
      resolve_control(*page.contribution.document, page.page_id, xml_control_id);
  if (!node || !interactive(*node)) return false;
  if (node->kind == plugin_ui::NodeKind::Toggle) {
    out_value = (node->value == "1" || node->value == "true") ? "1" : "0";
    return true;
  }
  if (node->kind == plugin_ui::NodeKind::List ||
      node->kind == plugin_ui::NodeKind::Input) {
    out_value = node->value;
    return true;
  }
  return false;
}

DispatchResult dispatch_control(std::string_view control_id_value,
                                std::string_view value) {
  plugin_ui::ContributionSnapshot active;
  ActionSink sink = nullptr;
  void *context = nullptr;
  const plugin_ui::Node *node = nullptr;
  {
    RuntimeState &runtime = state();
    std::lock_guard<std::mutex> lock(runtime.mutex);
    if (runtime.page_stack.empty() ||
        !runtime.page_stack.back().contribution.document)
      return DispatchResult::NotOwned;
    const RuntimeState::ActivePage &page = runtime.page_stack.back();
    node = resolve_control(*page.contribution.document, page.page_id,
                           control_id_value);
    if (!node || !interactive(*node)) return DispatchResult::NotOwned;
    active = page.contribution;
    sink = runtime.action_sink;
    context = runtime.action_context;
  }
  if (!sink) return DispatchResult::Unavailable;
  return sink(active.handle, *node, value, context)
             ? DispatchResult::Accepted
             : DispatchResult::Unavailable;
}

bool leave_active_page() {
  RuntimeState &runtime = state();
  std::lock_guard<std::mutex> lock(runtime.mutex);
  if (!runtime.page_stack.empty()) runtime.page_stack.pop_back();
  return !runtime.page_stack.empty();
}

} // namespace onion::shellui::dynamic_ui
