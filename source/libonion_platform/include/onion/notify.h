/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Shared system notification toast.
 *
 * Use onion_notify(show_wm, fmt, ...) everywhere for the watermark form.
 * ShellUI keeps a separate notify(const char*, ...) overload in shellui_notify.cpp
 * (string literals would be ambiguous with a bool-first overload).
 *
 * Pure C call sites may use:  #define notify(wm, ...) onion_notify((wm), __VA_ARGS__)
 *
 * IMPORTANT (ShellUI injectees):
 *   Do NOT call sceKernelSendNotificationRequest by name from this library.
 *   Injected payloads resolve that symbol as a *function pointer* via dlsym;
 *   a direct CALL would execute the pointer's storage as code (SIGSEGV/SIGILL).
 *   Hosts must register the real sender with onion_notify_set_send().
 */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <onion/notify_i18n.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Notification service reads this user-visible path. Config/payload stay
 * under /data/OnionHEN. */
#define ONION_NOTIFY_ICON_PATH "/user/data/OnionHEN/onionhen.png"

/** Kernel/userland toast send — matches sceKernelSendNotificationRequest. */
typedef int32_t (*onion_notify_send_fn)(int32_t device, void *req, size_t size,
                                        int32_t blocking);
typedef int32_t (*onion_notify_rich_send_fn)(int32_t user_id, bool is_logged,
                                             const char *payload);

/**
 * Install the send implementation for this process.
 * - util/daemon/bootstrapper: pass the real linked sceKernelSendNotificationRequest
 * - shellui: pass a trampoline that calls through the dlsym'd pointer
 * Must be called before any onion_notify / notify().
 */
void onion_notify_set_send(onion_notify_send_fn fn);
void onion_notify_set_rich_send(onion_notify_rich_send_fn fn);

void onion_notify_v(int show_watermark, const char *fmt, va_list ap);
void onion_notify(int show_watermark, const char *fmt, ...);
/**
 * Debug-style toast (SDK notify_debug): text only, no system icon.
 * Uses the same key + onion_notify_tr i18n path as onion_notify.
 * Pass stable notify.* keys from the i18n notifications catalog.
 */
void onion_notify_debug_v(const char *fmt, va_list ap);
void onion_notify_debug(const char *fmt, ...);
void onion_notify_rich(const char *message, const char *sub_message,
                       const char *icon_url, const char *preview_icon,
                       const char *notification_id);
/** Send a prebuilt rich JSON toast. Empty payload or send failure falls back
 *  to onion_notify(plain_key). Callers do not branch. */
void onion_notify_try_rich(const char *payload, const char *plain_key);

/**
 * Format a notification body: onion_notify_tr(key) then vsnprintf.
 * show_watermark non-zero prefixes "[OnionHEN] ".
 */
void onion_notify_format(char *out, size_t out_sz, int show_watermark,
                         const char *fmt, va_list ap);

#if !defined(__cplusplus) && !defined(ONION_NOTIFY_NO_LEGACY_MACRO)
/* Historical C name — not defined in C++ (avoids overload ambiguity). */
#define notify(show_wm, ...) onion_notify((show_wm), __VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif
