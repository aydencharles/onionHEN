#include "external_sprx_ui.hpp"

#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"

#include <onion/ipc_client.hpp>

#include <string_view>
#include <vector>

namespace onion::shellui::external_sprx {
namespace {

constexpr std::string_view kEnabledPrefix = "id_external_sprx_enabled_";
constexpr std::string_view kDeletePrefix = "id_external_sprx_delete_";

bool split_control(std::string_view control_id, std::string_view prefix,
                   std::string &id) {
  if (!control_id.starts_with(prefix)) return false;
  const std::string_view suffix = control_id.substr(prefix.size());
  if (suffix.empty() || suffix.size() >= 32) return false;
  id.assign(suffix);
  return true;
}

std::string status_for(const SprxInventoryItem &entry) {
  if (entry.loaded_for_current_target)
    return toolbox_i18n::tr("sprx.status.loaded");
  if (entry.matches_current_target)
    return toolbox_i18n::tr("sprx.status.matches");
  return toolbox_i18n::tr("sprx.status.inactive");
}

} // namespace

void append_inventory(ps5ui::Page &page) {
  std::vector<SprxInventoryItem> sprx;
  const bool loaded = IPC_Client::getInstance(false).ListSprx(sprx);
  if (loaded)
    g_ui.external_sprx = sprx;
  else
    g_ui.external_sprx.clear();

  page.group("id_external_sprx", toolbox_i18n::tr("sprx.group"),
             [&](ps5ui::Group &group) {
               if (!loaded) {
                 group.label("id_external_sprx_unavailable",
                             toolbox_i18n::tr("sprx.unavailable"));
                 return;
               }
               if (sprx.empty()) {
                 group.label("id_external_sprx_empty",
                             toolbox_i18n::tr("sprx.empty"));
                 return;
               }
               for (const SprxInventoryItem &entry : sprx) {
                 const std::string details = entry.id + " | " + entry.path;
                 group.group("id_external_sprx_" + entry.id, entry.id,
                             [&](ps5ui::Group &controls) {
                               controls.label("id_external_sprx_status_" + entry.id,
                                              status_for(entry));
                               controls.toggle(std::string(kEnabledPrefix) + entry.id,
                                               toolbox_i18n::tr("sprx.enabled"),
                                               entry.enabled,
                                               toolbox_i18n::tr("sprx.enabled.sub"));
                               controls.button(
                                   std::string(kDeletePrefix) + entry.id,
                                   toolbox_i18n::tr("sprx.delete"), std::nullopt,
                                   toolbox_i18n::tr("sprx.delete.sub"), std::nullopt,
                                   ps5ui::Style::None,
                                   toolbox_i18n::tr("sprx.delete.confirm"));
                             }, details);
               }
             },
             toolbox_i18n::tr("sprx.group.sub"));
}

DispatchResult dispatch(std::string_view control_id, std::string_view value) {
  DispatchResult result;
  IPC_Client &client = IPC_Client::getInstance(false);
  if (split_control(control_id, kEnabledPrefix, result.id)) {
    result.owned = true;
    result.action = Action::EnabledChanged;
    result.success = client.SetSprxEnabled(result.id,
                                           value == "1" || value == "true");
  } else if (split_control(control_id, kDeletePrefix, result.id)) {
    result.owned = true;
    result.action = Action::Deleted;
    result.success = client.DeleteSprx(result.id);
  }
  return result;
}

} // namespace onion::shellui::external_sprx
