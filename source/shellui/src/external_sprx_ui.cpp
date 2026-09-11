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

using plugin_pages::kSprxDeletePrefix;
using plugin_pages::kSprxEnabledPrefix;
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
    return true;
  }
  plugin_pages::append_sprx_list_links(page, g_ui.external_sprx);
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
  if (split_control_id(control_id, kSprxEnabledPrefix, result.id, 1, 31)) {
    result.owned = true;
    result.action = Action::EnabledChanged;
    const bool enabled = value == "1" || value == "true";
    result.success = client.SetSprxEnabled(result.id, enabled);
    if (result.success)
      if (SprxInventoryItem *entry = find_sprx(result.id))
        entry->enabled = enabled;
  } else if (split_control_id(control_id, kSprxDeletePrefix, result.id, 1,
                              31)) {
    result.owned = true;
    result.action = Action::Deleted;
    result.success = client.DeleteSprx(result.id);
    if (result.success)
      std::erase_if(g_ui.external_sprx, [&](const SprxInventoryItem &entry) {
        return entry.id == result.id;
      });
  }
  return result;
}

} // namespace onion::shellui::external_sprx
