#pragma once

#include <onion/debug_settings_route_policy.hpp>
#include <onion/notify.h>
#include <onion/notify_i18n.h>
#include <onion/version.h>

#include "onion_cjson.hpp"

#include <string>
#include <string_view>

namespace onion::daemon {

inline std::string make_welcome_toast_json(std::string_view toolbox_uri) {
  const std::string message = std::string(ONIONHEN_VERSION) +
                              onion_notify_tr("notify.boot.made_by") + ONIONHEN_AUTHOR;
  const char *sub_message = onion_notify_tr("notify.boot.welcome");
  const char *action_name = onion_notify_tr("notify.boot.goto_toolbox");
  const std::string_view action_url =
      toolbox_uri.empty()
          ? std::string_view(
                onion::debug_settings_route::kStandardRoute.simple_uri)
          : toolbox_uri;

  cJSON *root = cJSON_CreateObject();
  cJSON *raw = root ? cJSON_AddObjectToObject(root, "rawData") : nullptr;
  cJSON *view = raw ? cJSON_AddObjectToObject(raw, "viewData") : nullptr;
  cJSON *icon = view ? cJSON_AddObjectToObject(view, "icon") : nullptr;
  cJSON *icon_params =
      icon ? cJSON_AddObjectToObject(icon, "parameters") : nullptr;
  cJSON *message_obj =
      view ? cJSON_AddObjectToObject(view, "message") : nullptr;
  cJSON *sub_message_obj =
      view ? cJSON_AddObjectToObject(view, "subMessage") : nullptr;
  cJSON *actions = view ? cJSON_AddArrayToObject(view, "actions") : nullptr;
  cJSON *action = cJSON_CreateObject();
  if (!root || !raw || !view || !icon || !icon_params || !message_obj ||
      !sub_message_obj || !actions || !action ||
      !cJSON_AddItemToArray(actions, action)) {
    cJSON_Delete(action);
    cJSON_Delete(root);
    return {};
  }
  action = nullptr; // owned by actions

  cJSON *action_obj = cJSON_GetArrayItem(actions, 0);
  cJSON *action_params =
      cJSON_AddObjectToObject(action_obj, "parameters");
  cJSON *platform = cJSON_AddObjectToObject(raw, "platformViews");
  cJSON *preview =
      platform ? cJSON_AddObjectToObject(platform, "previewDisabled") : nullptr;
  cJSON *preview_view =
      preview ? cJSON_AddObjectToObject(preview, "viewData") : nullptr;
  cJSON *preview_icon =
      preview_view ? cJSON_AddObjectToObject(preview_view, "icon") : nullptr;
  cJSON *preview_icon_params =
      preview_icon ? cJSON_AddObjectToObject(preview_icon, "parameters")
                   : nullptr;
  cJSON *preview_message =
      preview_view ? cJSON_AddObjectToObject(preview_view, "message") : nullptr;

  const std::string action_url_string(action_url);
  const bool ok = action_params && platform && preview && preview_view &&
      preview_icon && preview_icon_params && preview_message &&
      cJSON_AddStringToObject(raw, "viewTemplateType",
                             "InteractiveToastTemplateB") &&
      cJSON_AddStringToObject(raw, "channelType", "Downloads") &&
      cJSON_AddStringToObject(raw, "useCaseId", "IDC") &&
      cJSON_AddStringToObject(raw, "toastOverwriteType", "No") &&
      cJSON_AddBoolToObject(raw, "isImmediate", true) &&
      cJSON_AddNumberToObject(raw, "priority", 100) &&
      cJSON_AddStringToObject(icon, "type", "Url") &&
      cJSON_AddStringToObject(icon_params, "url", ONION_NOTIFY_ICON_PATH) &&
      cJSON_AddStringToObject(message_obj, "body", message.c_str()) &&
      cJSON_AddStringToObject(sub_message_obj, "body", sub_message) &&
      cJSON_AddStringToObject(action_obj, "actionName", action_name) &&
      cJSON_AddStringToObject(action_obj, "actionType", "DeepLink") &&
      cJSON_AddBoolToObject(action_obj, "defaultFocus", true) &&
      cJSON_AddStringToObject(action_params, "actionUrl",
                             action_url_string.c_str()) &&
      cJSON_AddStringToObject(preview_icon, "type", "Predefined") &&
      cJSON_AddStringToObject(preview_icon_params, "icon", "download") &&
      cJSON_AddStringToObject(preview_message, "body", message.c_str()) &&
      cJSON_AddStringToObject(root, "createdDateTime",
                             "2025-12-14T03:14:51.473Z") &&
      cJSON_AddStringToObject(root, "localNotificationId", "588193127");
  if (!ok) {
    cJSON_Delete(root);
    return {};
  }
  return onion_cjson::print_owned(root, true);
}

} // namespace onion::daemon
