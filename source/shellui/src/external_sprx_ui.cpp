#include "external_sprx_ui.hpp"

#include "plugin_sprx_pages.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"
#include "settings_page_refresh.hpp"

#include <onion/ipc_client.hpp>

#include <algorithm>
#include <ranges>
#include <string_view>
#include <vector>

namespace onion::shellui::external_sprx {
namespace {

using plugin_pages::kSprxAutoStartPrefix;
using plugin_pages::kSprxBindAllPrefix;
using plugin_pages::kSprxBindCurrentPrefix;
using plugin_pages::kSprxDeletePrefix;
using plugin_pages::kSprxEnabledPrefix;
using plugin_pages::kSprxLoadPrefix;
using plugin_pages::kSprxReloadId;
using plugin_pages::split_control_id;

bool refresh_inventory() {
  std::vector<SprxInventoryItem> sprx;
  const bool loaded = IPC_Client::getInstance(false).ListSprx(sprx);
  if (loaded)
    g_ui.external_sprx = std::move(sprx);
  return loaded;
}

SprxInventoryItem *find_sprx(std::string_view id) {
  const auto it =
      std::ranges::find(g_ui.external_sprx, id, &SprxInventoryItem::id);
  return it == g_ui.external_sprx.end() ? nullptr : &*it;
}

} // namespace

bool append_inventory(ps5ui::Page &page) {
  if (!refresh_inventory()) {
    page.label("id_external_sprx_unavailable",
               toolbox_i18n::tr("sprx.unavailable"));
    return false;
  }
  if (g_ui.external_sprx.empty()) {
    page.label("id_external_sprx_empty", toolbox_i18n::tr("sprx.empty"));
  } else {
    plugin_pages::append_sprx_list_links(page, g_ui.external_sprx);
  }
  page.button(std::string(kSprxReloadId), toolbox_i18n::tr("sprx.reload"),
              std::nullopt, toolbox_i18n::tr("sprx.reload.sub"));
  return true;
}

static bool config_model(ps5ui::Node &model, std::string_view id) {
  const bool loaded = refresh_inventory();
  const SprxInventoryItem *found = loaded ? find_sprx(id) : nullptr;

  ps5ui::Page page("id_sprx_config", std::string(id));
  if (!loaded) {
    page.label("id_external_sprx_unavailable",
               toolbox_i18n::tr("sprx.unavailable"));
  } else if (!found) {
    page.label("id_external_sprx_missing", toolbox_i18n::tr("sprx.missing"));
  } else {
    plugin_pages::fill_sprx_config(page, *found);
  }
  model = page.root();
  return loaded;
}

void generate_config_xml(std::string &xml_buffer, std::string_view id) {
  ps5ui::Node model;
  config_model(model, id);
  xml_buffer = settings::publish(model, [key = std::string(id)](ps5ui::Node &next) {
    return config_model(next, key);
  });
}

DispatchResult dispatch(std::string_view control_id, std::string_view value) {
  DispatchResult result;
  IPC_Client &client = IPC_Client::getInstance(false);
  if (control_id == kSprxReloadId) {
    result.owned = true;
    result.action = Action::Reloaded;
    result.success = client.ReloadSprx(&result.error);
    if (result.success)
      refresh_inventory();
  } else if (split_control_id(control_id, kSprxLoadPrefix, result.id, 1, 31)) {
    result.owned = true;
    result.action = Action::LoadNow;
    result.success = client.LoadSprx(result.id, &result.error);
    if (result.success)
      refresh_inventory();
  } else if (split_control_id(control_id, kSprxEnabledPrefix, result.id, 1,
                              31)) {
    result.owned = true;
    result.action = Action::EnabledChanged;
    const bool enabled = value == "1" || value == "true";
    result.toggle_value = enabled;
    if (const SprxInventoryItem *existing = find_sprx(result.id))
      result.was_loaded = existing->loaded_for_current_target;
    result.success = client.SetSprxEnabled(result.id, enabled);
    if (result.success) {
      if (SprxInventoryItem *entry = find_sprx(result.id))
        entry->enabled = enabled;
    }
  } else if (split_control_id(control_id, kSprxAutoStartPrefix, result.id, 1,
                              31)) {
    result.owned = true;
    result.action = Action::AutoStartChanged;
    const bool auto_start = value == "1" || value == "true";
    result.toggle_value = auto_start;
    result.success = client.SetSprxAutoStart(result.id, auto_start);
    if (result.success) {
      if (SprxInventoryItem *entry = find_sprx(result.id))
        entry->auto_start = auto_start;
    }
  } else if (split_control_id(control_id, kSprxBindCurrentPrefix, result.id, 1,
                              31)) {
    result.owned = true;
    result.action = Action::BoundCurrent;
    result.success = client.BindSprxTarget(result.id, "current", &result.error);
    if (result.success)
      refresh_inventory();
  } else if (split_control_id(control_id, kSprxBindAllPrefix, result.id, 1,
                              31)) {
    result.owned = true;
    result.action = Action::BoundAll;
    result.success = client.BindSprxTarget(result.id, "all", &result.error);
    if (result.success)
      refresh_inventory();
  } else if (split_control_id(control_id, kSprxDeletePrefix, result.id, 1,
                              31)) {
    result.owned = true;
    result.action = Action::Deleted;
    result.success = client.DeleteSprx(result.id, &result.error);
    if (result.success)
      refresh_inventory();
  }
  return result;
}

} // namespace onion::shellui::external_sprx
