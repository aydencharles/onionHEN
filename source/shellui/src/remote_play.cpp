/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Remote Play pairing page backed by the native PS5 Remote Play service.
 */

#include "account_activator.h"
#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "toolbox_i18n.hpp"

#include <pthread.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

namespace {

constexpr int kRemotePlayEnableRegistry = 0x41810000;
constexpr char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool g_confirm_running = false;
pthread_t g_confirm_thread{};

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
  g_confirm_running = true;
  int pair_status = -1;
  int pair_error = -1;
  int last_status = -1;
  int last_pair_error = -1;

  while (g_confirm_running) {
    if (!sceRemoteplayConfirmDeviceRegist) {
      notify("notify.remote_play.unavailable");
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
      notify("notify.remote_play.pairing_failed_fmt", error);
      break;
    }
    if (pair_status == 2) {
      notify("notify.remote_play.paired");
      break;
    }
  }

  g_confirm_running = false;
  return nullptr;
}

void stop_confirm_registration_loop() {
  if (!g_confirm_running)
    return;
  g_confirm_running = false;
  pthread_join(g_confirm_thread, nullptr);
}

uint32_t generate_pin_code() {
  if (!sceRemoteplayNotifyPinCodeError || !sceRemoteplayGeneratePinCode)
    return 0;

  sceRemoteplayNotifyPinCodeError(1);
  uint32_t pin = 0;
  if (sceRemoteplayGeneratePinCode(&pin) != 0)
    return 0;

  stop_confirm_registration_loop();
  if (pthread_create(&g_confirm_thread, nullptr, confirm_registration_loop,
                     nullptr) != 0) {
    notify("notify.remote_play.pairing_start_failed");
    return 0;
  }
  notify("notify.remote_play.waiting");
  return pin;
}

void initialize_remote_play() {
  if (!sceRemoteplayInitialize) {
    notify("notify.remote_play.unavailable");
    return;
  }

  int enabled = 0;
  const int read_error = sceRegMgrGetInt_hook(kRemotePlayEnableRegistry, &enabled);
  if (read_error != 0) {
    notify("notify.remote_play.read_failed_fmt", read_error);
  } else if (enabled != 1) {
    const int write_error = sceRegMgrSetInt(kRemotePlayEnableRegistry, 1);
    if (write_error != 0)
      notify("notify.remote_play.enable_failed_fmt", write_error);
  }

  const int error = sceRemoteplayInitialize(nullptr, 0);
  if (error != 0)
    LOG_DEBUG("sceRemoteplayInitialize returned 0x%X; continuing because "
              "the native service may already be initialized",
              error);
}

} // namespace

std::string g_remote_play_info;

void generate_remote_play_xml(std::string &xml_buffer) {
  char account_id[16]{};
  uint64_t decoded_account_id = 0;

  xml_buffer =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
      "<system_settings version=\"1.0\" plugin=\"debug_settings_plugin\">"
      "<setting_list id=\"remote_play_pin_display\" title=\"" +
      std::string(toolbox_i18n::tr("remote_play.details.title")) +
      "\" style=\"center\">";

  static bool initialized = false;
  if (!initialized) {
    initialize_remote_play();
    initialized = true;
  }

  Activator activator(true);
  if (activator.IsNotActivated()) {
    activator.Activate();
    xml_buffer +=
      "<label id=\"id_remote_play_activation\" title=\"" +
      std::string(toolbox_i18n::tr("remote_play.activation")) +
      "\" style=\"center\"/>";
    xml_buffer += "</setting_list></system_settings>";
    return;
  }

  base64_encode(activator.currentUser.accountID, account_id,
                sizeof(account_id));
  decoded_account_id = activator.currentUser.accountID;
  const uint32_t pin = generate_pin_code();

  char pin_text[64]{};
    std::snprintf(pin_text, sizeof(pin_text),
      toolbox_i18n::tr("remote_play.pin_fmt"),
                pin / 10000, pin % 10000);
  std::ostringstream details;
    details << toolbox_i18n::format("remote_play.account_fmt", account_id)
      << "\n"
      << toolbox_i18n::format("remote_play.decoded_account_fmt",
            decoded_account_id)
      << "\n" << pin_text;
  g_remote_play_info = details.str();

  xml_buffer += "<label id=\"id_remote_play_pin\" title=\"" +
                std::string(pin_text) + "\" style=\"center\"/>";
    xml_buffer += "<label id=\"id_remote_play_account\" title=\"" +
      toolbox_i18n::format("remote_play.account_fmt", account_id) +
      "\" style=\"center\"/>";
  if (usbpath() != -1)
    xml_buffer +=
    "<button id=\"id_save_rp_info\" title=\"" +
    std::string(toolbox_i18n::tr("remote_play.save")) +
    "\" style=\"center\"/>";
  xml_buffer += "</setting_list></system_settings>";
}
