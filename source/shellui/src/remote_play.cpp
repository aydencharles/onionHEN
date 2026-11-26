/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Remote Play pairing page backed by the native PS5 Remote Play service.
 */

#include "account_activator.h"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ps5_settings_ui.hpp"
#include "remote_play.hpp"
#include "toolbox_i18n.hpp"

#include <onion/notify.h>

#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <sstream>
#include <string>

namespace {

constexpr int kRemotePlayEnableRegistry = 0x41810000;
constexpr char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::atomic<remote_play::PairingState> g_pairing_state{
    remote_play::PairingState::Idle};
bool g_confirm_thread_created = false;
pthread_t g_confirm_thread{};
MonoObject *g_remote_play_page = nullptr;
uint32_t g_remote_play_page_handle = 0;

void release_remote_play_page() {
  if (g_remote_play_page_handle && mono_gchandle_free)
    mono_gchandle_free(g_remote_play_page_handle);
  g_remote_play_page = nullptr;
  g_remote_play_page_handle = 0;
}

void invalidate_pin_registration(const char *reason) {
  if (!sceRemoteplayNotifyPinCodeError) {
    LOG_ERROR("remote_play: pin invalidation unavailable (%s)", reason);
    return;
  }

  const int error = sceRemoteplayNotifyPinCodeError(1);
  if (error != 0) {
    LOG_WARN("remote_play: pin invalidation failed error=0x%X (%s)", error,
             reason);
  } else {
    LOG_DEBUG("remote_play: pin registration invalidated (%s)", reason);
  }
}

void base64_encode(uint64_t input, char *output, size_t output_size) {
  if (!output || output_size < 13)
    return;

  unsigned char bytes[8]{};
  for (int i = 0; i < 8; ++i)
    bytes[i] = static_cast<unsigned char>((input >> (i * 8)) & 0xff);

  int out = 0;
  for (int i = 0; i < 8;) {
    const uint32_t a = bytes[i++];
    const uint32_t b = i < 8 ? bytes[i++] : 0;
    const uint32_t c = i < 8 ? bytes[i++] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;
    output[out++] = kBase64Table[(triple >> 18) & 0x3f];
    output[out++] = kBase64Table[(triple >> 12) & 0x3f];
    output[out++] = kBase64Table[(triple >> 6) & 0x3f];
    output[out++] = kBase64Table[triple & 0x3f];
  }
  output[11] = '=';
  output[12] = '\0';
}

void *confirm_registration_loop(void *) {
  int pair_status = -1;
  int pair_error = -1;
  int last_status = -1;
  int last_pair_error = -1;

  while (g_pairing_state.load(std::memory_order_acquire) ==
         remote_play::PairingState::Waiting) {
    if (!sceRemoteplayConfirmDeviceRegist) {
      if (remote_play::try_finish_pairing(
              g_pairing_state, remote_play::PairingState::Failed)) {
        onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.unavailable");
        invalidate_pin_registration("service_unavailable");
      }
      break;
    }

    const int error = sceRemoteplayConfirmDeviceRegist(&pair_status, &pair_error);
    if (pair_status != last_status || pair_error != last_pair_error) {
      LOG_DEBUG("Remote Play confirmation: error=0x%X status=%d pair_error=0x%X",
                error, pair_status, pair_error);
      last_status = pair_status;
      last_pair_error = pair_error;
    }
    if (error) {
      if (remote_play::try_finish_pairing(
              g_pairing_state, remote_play::PairingState::Failed)) {
        onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.pairing_failed_fmt", error);
        invalidate_pin_registration("confirmation_failed");
      }
      break;
    }
    if (pair_status == 2) {
      if (remote_play::try_finish_pairing(
              g_pairing_state, remote_play::PairingState::Paired)) {
        onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.paired");
        invalidate_pin_registration("pairing_succeeded");
      }
      break;
    }
  }

  return nullptr;
}

void join_confirm_registration_thread() {
  if (g_confirm_thread_created) {
    pthread_join(g_confirm_thread, nullptr);
    g_confirm_thread_created = false;
  }
}

uint32_t generate_pin_code() {
  if (!sceRemoteplayNotifyPinCodeError || !sceRemoteplayGeneratePinCode)
    return 0;

  (void)remote_play::try_finish_pairing(
      g_pairing_state, remote_play::PairingState::Cancelled);
  invalidate_pin_registration("new_pairing");
  join_confirm_registration_thread();

  uint32_t pin = 0;
  if (sceRemoteplayGeneratePinCode(&pin) != 0)
    return 0;

  g_pairing_state.store(remote_play::PairingState::Waiting,
                        std::memory_order_release);
  if (pthread_create(&g_confirm_thread, nullptr, confirm_registration_loop,
                     nullptr) != 0) {
    g_pairing_state.store(remote_play::PairingState::Failed,
                          std::memory_order_release);
    onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.pairing_start_failed");
    invalidate_pin_registration("thread_create_failed");
    return 0;
  }
  g_confirm_thread_created = true;
  onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.waiting");
  return pin;
}

void initialize_remote_play() {
  if (!sceRemoteplayInitialize) {
    onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.unavailable");
    return;
  }

  int enabled = 0;
  const int read_error = sceRegMgrGetInt_hook(kRemotePlayEnableRegistry, &enabled);
  if (read_error != 0) {
    onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.read_failed_fmt", read_error);
  } else if (enabled != 1) {
    const int write_error = sceRegMgrSetInt(kRemotePlayEnableRegistry, 1);
    if (write_error != 0)
      onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.enable_failed_fmt", write_error);
  }

  const int error = sceRemoteplayInitialize(nullptr, 0);
  if (error != 0)
    LOG_DEBUG("sceRemoteplayInitialize returned 0x%X; continuing because "
              "the native service may already be initialized",
              error);
}

} // namespace

std::string g_remote_play_info;

void remote_play_bind_page(MonoObject *page) {
  if (!page || page == g_remote_play_page)
    return;

  release_remote_play_page();
  if (!mono_gchandle_new)
    return;

  g_remote_play_page_handle = mono_gchandle_new(page, 1);
  if (!g_remote_play_page_handle) {
    LOG_ERROR("remote_play_xml: failed to root page");
    return;
  }
  g_remote_play_page = page;
}

bool remote_play_handle_popping(MonoObject *outgoing) {
  if (!outgoing || outgoing != g_remote_play_page)
    return false;

  release_remote_play_page();
  const bool cancelled = remote_play::try_finish_pairing(
      g_pairing_state, remote_play::PairingState::Cancelled);
  if (cancelled) {
    onion_notify_debug(/*show_watermark=*/0, "notify.remote_play.pairing_cancelled");
    invalidate_pin_registration("page_popped");
  }
  join_confirm_registration_thread();
  return true;
}

void generate_remote_play_xml(std::string &xml_buffer) {
  char account_id[16]{};
  uint64_t decoded_account_id = 0;
  ps5ui::Page page("remote_play_pin_display",
                   toolbox_i18n::tr("remote_play.details.title"));
  page.root_style(ps5ui::Style::Center);

  Activator activator(true);
  if (!activator.Valid() || activator.IsNotActivated()) {
    const char *message = !activator.Valid()
                              ? toolbox_i18n::tr("account.error.read")
                              : toolbox_i18n::tr("account.status.not_activated");
    page.label("id_remote_play_account_required", message,
               ps5ui::Style::Center);
    xml_buffer = page.build();
    return;
  }

  static bool initialized = false;
  if (!initialized) {
    initialize_remote_play();
    initialized = true;
  }

  base64_encode(activator.currentUser.accountID, account_id,
                sizeof(account_id));
  decoded_account_id = activator.currentUser.accountID;
  const uint32_t pin = generate_pin_code();

  const std::string pin_text = toolbox_i18n::format(
      "remote_play.pin_fmt", pin / 10000, pin % 10000);
  std::ostringstream details;
  details << toolbox_i18n::format("remote_play.account_fmt", account_id)
          << "\n"
          << toolbox_i18n::format("remote_play.decoded_account_fmt",
                                  decoded_account_id)
          << "\n"
          << pin_text;
  g_remote_play_info = details.str();

  page.label("id_remote_play_pin", pin_text, ps5ui::Style::Center)
      .label("id_remote_play_account",
             toolbox_i18n::format("remote_play.account_fmt", account_id),
             ps5ui::Style::Center);
  if (usbpath() != -1)
    page.button("id_save_rp_info", toolbox_i18n::tr("remote_play.save"),
                std::nullopt, std::nullopt, std::nullopt,
                ps5ui::Style::Center);
  xml_buffer = page.build();
}
