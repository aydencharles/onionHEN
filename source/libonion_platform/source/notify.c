/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/notify.h>
#include <onion/notify_i18n.h>
#include <onion/log.h>

#include "cJSON.hpp"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Layout must total 0xC30 (same as elfldr: 45-byte header + 3075 payload).
 * message starts at offset 0x2D (immediately after use_icon_image_uri).
 */
typedef struct {
  int32_t type;             /* 0x00 */
  int32_t req_id;           /* 0x04 */
  int32_t priority;         /* 0x08 */
  int32_t msg_id;           /* 0x0C */
  int32_t target_id;        /* 0x10 */
  int32_t user_id;          /* 0x14 */
  int32_t unk1;             /* 0x18 */
  int32_t unk2;             /* 0x1C */
  int32_t app_id;           /* 0x20 */
  int32_t error_num;        /* 0x24 */
  int32_t unk3;             /* 0x28 */
  char use_icon_image_uri;  /* 0x2C */
  char message[1024];       /* 0x2D */
  char uri[1024];           /* 0x42D */
  char unkstr[1024];        /* 0x82D */
  char _pad_to_c30[3];      /* 0xC2D → 0xC30 */
} OrbisNotificationRequest;

_Static_assert(sizeof(OrbisNotificationRequest) == 0xC30,
               "OrbisNotificationRequest must be 0xC30");

static onion_notify_send_fn g_send = NULL;
static onion_notify_rich_send_fn g_rich_send = NULL;

void onion_notify_set_send(onion_notify_send_fn fn) { g_send = fn; }

void onion_notify_set_rich_send(onion_notify_rich_send_fn fn) {
  g_rich_send = fn;
}

void onion_notify_format(char *out, size_t out_sz, int show_watermark,
                         const char *fmt, va_list ap) {
  char buff[3075];

  /* notify.* key → onion_notify_tr → printf-style body. */
  vsnprintf(buff, sizeof(buff), onion_notify_tr(fmt), ap);
  if (show_watermark) {
    snprintf(out, out_sz, "[OnionHEN] %s", buff);
  } else {
    snprintf(out, out_sz, "%s", buff);
  }
}

static void onion_notify_send_request(OrbisNotificationRequest *req,
                                      int with_system_icon) {
  if (!req) {
    return;
  }

  req->type = 0;
  req->unk3 = 0;
  req->target_id = -1;
  if (with_system_icon) {
    req->use_icon_image_uri = 1;
    strncpy(req->uri, "cxml://psnotification/tex_icon_system",
            sizeof(req->uri) - 1);
  } else {
    /* Debug-style toast (SDK notify_debug): text only, no icon URI. */
    req->use_icon_image_uri = 0;
    req->uri[0] = '\0';
  }

  LOG_DEBUG("Notify%s: %s", with_system_icon ? "" : "(debug)",
            req->message);

  if (!g_send) {
    /* Never fall back to a direct CALL of sceKernelSendNotificationRequest —
     * that symbol is a data pointer in shellui/fps injectees. */
    LOG_ERROR("Notify: send fn not registered (onion_notify_set_send)");
    return;
  }
  (void)g_send(0, req, sizeof(*req), 0);
}

void onion_notify_v(int show_watermark, const char *fmt, va_list ap) {
  OrbisNotificationRequest req;
  memset(&req, 0, sizeof(req));
  onion_notify_format(req.message, sizeof(req.message), show_watermark, fmt, ap);
  onion_notify_send_request(&req, /*with_system_icon=*/1);
}

void onion_notify(int show_watermark, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  onion_notify_v(show_watermark, fmt, args);
  va_end(args);
}

void onion_notify_debug_v(const char *fmt, va_list ap) {
  OrbisNotificationRequest req;
  memset(&req, 0, sizeof(req));
  /* Same i18n path as onion_notify; watermark off for bare debug text. */
  onion_notify_format(req.message, sizeof(req.message), /*show_watermark=*/0,
                      fmt, ap);
  onion_notify_send_request(&req, /*with_system_icon=*/0);
}

void onion_notify_debug(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  onion_notify_debug_v(fmt, args);
  va_end(args);
}

void onion_notify_rich(const char *message, const char *sub_message,
                       const char *icon_url, const char *preview_icon,
                       const char *notification_id) {
  const char *msg = onion_notify_tr(message ? message : "notify.brand");
  const char *sub = onion_notify_tr(sub_message ? sub_message : "");
  const char *icon_url_value =
      icon_url ? icon_url : ONION_NOTIFY_ICON_PATH;
  const char *preview_icon_value = preview_icon ? preview_icon : "download";
  const char *notification_id_value =
      notification_id ? notification_id : "588193127";

  cJSON *root = cJSON_CreateObject();
  cJSON *raw = root ? cJSON_AddObjectToObject(root, "rawData") : NULL;
  cJSON *view = raw ? cJSON_AddObjectToObject(raw, "viewData") : NULL;
  cJSON *icon = view ? cJSON_AddObjectToObject(view, "icon") : NULL;
  cJSON *icon_params =
      icon ? cJSON_AddObjectToObject(icon, "parameters") : NULL;
  cJSON *message_obj =
      view ? cJSON_AddObjectToObject(view, "message") : NULL;
  cJSON *sub_message_obj =
      view ? cJSON_AddObjectToObject(view, "subMessage") : NULL;
  cJSON *platform = raw ? cJSON_AddObjectToObject(raw, "platformViews") : NULL;
  cJSON *preview =
      platform ? cJSON_AddObjectToObject(platform, "previewDisabled") : NULL;
  cJSON *preview_view =
      preview ? cJSON_AddObjectToObject(preview, "viewData") : NULL;
  cJSON *preview_icon_obj =
      preview_view ? cJSON_AddObjectToObject(preview_view, "icon") : NULL;
  cJSON *preview_icon_params = preview_icon_obj
      ? cJSON_AddObjectToObject(preview_icon_obj, "parameters")
      : NULL;
  cJSON *preview_message =
      preview_view ? cJSON_AddObjectToObject(preview_view, "message") : NULL;

  if (!root || !raw || !view || !icon || !icon_params || !message_obj ||
      !sub_message_obj || !platform || !preview || !preview_view ||
      !preview_icon_obj || !preview_icon_params || !preview_message ||
      !cJSON_AddStringToObject(raw, "viewTemplateType",
                               "InteractiveToastTemplateB") ||
      !cJSON_AddStringToObject(raw, "channelType", "Downloads") ||
      !cJSON_AddStringToObject(raw, "useCaseId", "IDC") ||
      !cJSON_AddStringToObject(raw, "toastOverwriteType", "No") ||
      !cJSON_AddBoolToObject(raw, "isImmediate", 1) ||
      !cJSON_AddNumberToObject(raw, "priority", 100) ||
      !cJSON_AddStringToObject(icon, "type", "Url") ||
      !cJSON_AddStringToObject(icon_params, "url", icon_url_value) ||
      !cJSON_AddStringToObject(message_obj, "body", msg) ||
      !cJSON_AddStringToObject(sub_message_obj, "body", sub) ||
      !cJSON_AddStringToObject(preview_icon_obj, "type", "Predefined") ||
      !cJSON_AddStringToObject(preview_icon_params, "icon",
                               preview_icon_value) ||
      !cJSON_AddStringToObject(preview_message, "body", sub[0] ? sub : msg) ||
      !cJSON_AddStringToObject(root, "createdDateTime",
                               "2025-12-14T03:14:51.473Z") ||
      !cJSON_AddStringToObject(root, "localNotificationId",
                               notification_id_value)) {
    LOG_ERROR("Rich notify: failed to build payload");
    cJSON_Delete(root);
    return;
  }

  char *payload = cJSON_Print(root);
  cJSON_Delete(root);
  if (!payload || strlen(payload) >= 4096) {
    LOG_ERROR("Rich notify: payload too large");
    cJSON_free(payload);
    return;
  }

  LOG_DEBUG("Rich notify: %s%s%s", msg, sub[0] ? " - " : "", sub);

  if (!g_rich_send) {
    LOG_ERROR("Rich notify: send fn not registered");
    cJSON_free(payload);
    return;
  }
  (void)g_rich_send(0xFE, true, payload);
  cJSON_free(payload);
}
