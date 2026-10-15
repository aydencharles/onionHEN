/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Strict, same-size Hermes bytecode patch for 11.6 NPXS40003.  It converts the
 * feature-gated Voice Agent control into one generic extension host.  The host
 * remains a normal native FunctionControl entry, so the stock customization
 * reducer, drag ordering and User.setInfo serialization continue to own state.
 *
 * Verified against the 11.6 NPXS40003 profile (HBC v89, fileLength 0x5e9350,
 * source hash ff55cd8f...48ab):
 *
 *   fn #2806  module 193 factory (redux slice), 1872 bytes, no jumps
 *     +0x2fb  Not r23, r25   runtime tile: isRevoked = !enableVoiceAgentFC
 *     +0x49e  Not r26, r25   custom  tile: isRevoked = !enableVoiceAgentFC
 *     +0x4b1  PutNewOwnById  custom tile property name, string #38907
 *     r3 at +0x2fb is GetGlobalObject (always truthy) -> isRevoked = false.
 *     r0 at +0x49e is LoadConstUndefined (falsy) -> isDefault = true.
 *
 *   fn #35992 module 3482 voice-agent Control Center component, 268 bytes
 *     +0xbd   CreateClosure fn #35999 (the onPress forwarder)
 *     +0xf6   props.onPress
 *     +0xfa   CreateClosure fn #36000 (getMenu), replaced by the URI trampoline
 *
 *   fn #36000 "getMenu" 107 bytes - body replaced by the openURL trampoline.
 *     Its parent environment slot 4 is the same `_closure1_slot4` that
 *     fn #35998 already uses as `LinkingPS.openURL`, so the trampoline's
 *     GetEnvironment r0, 1 + LoadFromEnvironment r0, r0, 4 is correct.
 *
 * Scope note: 11.6 exposes five feature-gated tiles (registryId 2..6:
 * notifications, gamingLounge, music, voiceAgent, broadcast).  This build
 * claims only voiceAgent, which is off on retail units, so nothing is lost.
 * See CONTROL_CENTER_SDK.md for how to claim the remaining four.
 */
#include "control_center_patch.hpp"

#if SHELLUI_CONTROL_CENTER_PATCH == 1

#include "control_center_sdk.hpp"
#include "defs.h"
#include "hooked_funcs.hpp"
#include "ipc.hpp"

#include <msg.hpp>
#include <onion/log.h>
#include <sha1.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::array<uint8_t, 8> kHermesMagic = {0xc6, 0x1f, 0xbc, 0x03,
                                                 0xc1, 0x03, 0x19, 0x1f};
constexpr std::array<uint8_t, 8> kRnpsMagic = {'R', 'N', 'P', 'S',
                                               'H', 'E', 'D', 'R'};
constexpr size_t kRnpsPayloadOffsetField = 0x1c;
constexpr size_t kVersionOffset = 0x08;
constexpr size_t kSourceHashOffset = 0x0c;
constexpr size_t kFileLengthOffset = 0x20;
constexpr size_t kFooterSha1Size = 20;

constexpr uint32_t kProfileVersion = 89;
constexpr size_t kProfileFileLength = 0x5e9350;
constexpr std::array<uint8_t, 20> kProfileSourceHash = {
    0xff, 0x55, 0xcd, 0x8f, 0x3a, 0xea, 0x18, 0xa0, 0xb1, 0x33,
    0x63, 0xa6, 0x40, 0xe5, 0x68, 0x46, 0xe2, 0x71, 0x48, 0xab};

/* HBC-relative offsets, independently checked against the profile hash. */
constexpr size_t kModuleFactoryBody = 0x227b64;      // Function #2806
constexpr size_t kRuntimeVoiceRevokeNot =
    kModuleFactoryBody + 0x2fb;                       // Not r23, r25
constexpr size_t kCustomVoiceRevokeNot =
    kModuleFactoryBody + 0x49e;                       // Not r26, r25
constexpr size_t kCustomVoiceProperty =
    kModuleFactoryBody + 0x4b1;                       // isRevoked write
constexpr size_t kVoiceRegistrySerializedInteger = 0x1d18de;
constexpr size_t kVoiceComponentOnPressClosure = 0x59dcd1;
constexpr size_t kVoiceComponentLabelLoad = 0x59dcee;
constexpr size_t kVoiceComponentIconBlock = 0x59dd02;
constexpr size_t kVoiceComponentIconBlockSize = 22;
constexpr size_t kVoiceActionBody = 0x59debb;         // Function #36000
constexpr size_t kVoiceActionBodySize = 107;

constexpr size_t kLabelStringStorage = 0x0d599b;      // string #310
constexpr size_t kLabelStringLengthByte = 0x0ae3ff;
constexpr size_t kDescriptionCapacity = 26;           // overlapping string #311
constexpr size_t kDescriptionStringLengthByte = 0x0ae403;
constexpr size_t kOriginalIconStringStorage = 0x136b75; // string #13094
constexpr size_t kSmallStringTable = 0x0adf24;
constexpr uint16_t kIconStringId = 13094;
constexpr size_t kIconStringEntry =
    kSmallStringTable + kIconStringId * sizeof(uint32_t);
constexpr uint16_t kSenderStringId = 16885;
constexpr size_t kSenderStringEntry =
    kSmallStringTable + kSenderStringId * sizeof(uint32_t);
constexpr uint16_t kTooltipTitlePropsStringId = 28944;
constexpr size_t kTooltipTitlePropsStringEntry =
    kSmallStringTable + kTooltipTitlePropsStringId * sizeof(uint32_t);
/*
 * String #2902 is dead after the onPress closure is redirected to #36000.
 * Starting two bytes into its compacted storage gives a contiguous 43-byte
 * region.  Relocate the shared `sender` substring into dead string #9487,
 * then use the first 33 bytes as the complete icon source.  The remaining
 * dead bytes also preserve `tooltipTitleProps`, whose old compacted suffix is
 * overwritten by the shorter invocation URI.  A single icon string ID serves
 * both the registry descriptor and live FC component.
 *
 * Small string table entries encode (offset_relative_to_string_storage << 1)
 * | (length << 24); kStringStorageBase is that storage base (derived from the
 * entries of #9487, #13094, #16885 and #2902, and cross-checked against
 * stringStorageSize in the header).
 */
constexpr size_t kStringStorageBase = 0xd54e0;
constexpr size_t kIconStorage = 0x0e6480;
constexpr size_t kIconCapacity = 33;
constexpr uint32_t kIconStorageRelative = 0x10fa0;
constexpr size_t kSenderRelocatedStorage = 0x11657f;
constexpr uint32_t kSenderRelocatedStorageRelative = 0x4109f;
constexpr size_t kTooltipTitlePropsRelocatedStorage = 0x116585;
constexpr uint32_t kTooltipTitlePropsRelocatedStorageRelative = 0x410a5;
constexpr size_t kUriStringStorage = 0x137a28;        // string #13209
constexpr size_t kUriCapacity = 39;
constexpr size_t kUriStringLengthByte = 0x0bad8b;

static_assert(kIconStorage == kStringStorageBase + kIconStorageRelative,
              "icon storage must match its string table entry offset");
static_assert(kSenderRelocatedStorage ==
                  kStringStorageBase + kSenderRelocatedStorageRelative,
              "sender relocation must match its string table entry offset");
static_assert(kTooltipTitlePropsRelocatedStorage ==
                  kStringStorageBase + kTooltipTitlePropsRelocatedStorageRelative,
              "tooltipTitleProps relocation must match its entry offset");
static_assert(kLabelStringLengthByte == kSmallStringTable + 310 * 4 + 3,
              "label length byte must be string #310's entry high byte");
static_assert(kDescriptionStringLengthByte == kSmallStringTable + 311 * 4 + 3,
              "description length byte must be string #311's entry high byte");
static_assert(kUriStringLengthByte == kSmallStringTable + 13209 * 4 + 3,
              "URI length byte must be string #13209's entry high byte");

struct HbcView {
  uint8_t *data;
  size_t size;
  size_t base;
};

uint32_t read_u32le(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

bool contains(size_t size, size_t offset, size_t length) {
  return offset <= size && length <= size - offset;
}

bool locate_hbc(uint8_t *buffer, size_t visible_size, HbcView *view) {
  if (!buffer || !view)
    return false;
  if (contains(visible_size, 0, kHermesMagic.size()) &&
      memcmp(buffer, kHermesMagic.data(), kHermesMagic.size()) == 0) {
    *view = {buffer, visible_size, 0};
    return true;
  }
  if (!contains(visible_size, 0, 0x20) ||
      memcmp(buffer, kRnpsMagic.data(), kRnpsMagic.size()) != 0)
    return false;
  const size_t payload = read_u32le(buffer + kRnpsPayloadOffsetField);
  if (!contains(visible_size, payload, kHermesMagic.size()) ||
      memcmp(buffer + payload, kHermesMagic.data(), kHermesMagic.size()) != 0)
    return false;
  *view = {buffer + payload, visible_size - payload, payload};
  return true;
}

bool has_marker(const HbcView &hbc, const char *marker) {
  const size_t length = strlen(marker);
  if (length == 0 || length > hbc.size)
    return false;
  for (size_t i = 0; i <= hbc.size - length; ++i) {
    if (memcmp(hbc.data + i, marker, length) == 0)
      return true;
  }
  return false;
}

bool profile_matches(const HbcView &hbc) {
  return contains(hbc.size, kFileLengthOffset, sizeof(uint32_t)) &&
         read_u32le(hbc.data + kVersionOffset) == kProfileVersion &&
         read_u32le(hbc.data + kFileLengthOffset) == kProfileFileLength &&
         hbc.size >= kProfileFileLength &&
         memcmp(hbc.data + kSourceHashOffset, kProfileSourceHash.data(),
                kProfileSourceHash.size()) == 0 &&
         has_marker(hbc, "NPXS40003") &&
         has_marker(hbc, "controlCenterFunctionControlSettings");
}

bool expect(const HbcView &hbc, size_t offset, const uint8_t *bytes,
            size_t size, const char *name) {
  if (!contains(hbc.size, offset, size) ||
      memcmp(hbc.data + offset, bytes, size) != 0) {
    LOG_ERROR("control_center_patch: %s mismatch at hbc+0x%llx", name,
              static_cast<unsigned long long>(offset));
    return false;
  }
  return true;
}

void replace_string(HbcView &hbc, size_t storage_offset, size_t capacity,
                    size_t length_byte_offset, const std::string &value) {
  if (value.size() > capacity)
    return;
  /* Do not clear the tail: Hermes compacts overlapping string substrings. */
  memcpy(hbc.data + storage_offset, value.data(), value.size());
  hbc.data[length_byte_offset] = static_cast<uint8_t>(value.size());
}

void write_small_string_entry(HbcView &hbc, size_t entry_offset,
                              uint32_t storage_offset, uint8_t length) {
  const uint32_t encoded = (storage_offset << 1) |
                           (static_cast<uint32_t>(length) << 24);
  hbc.data[entry_offset + 0] = static_cast<uint8_t>(encoded);
  hbc.data[entry_offset + 1] = static_cast<uint8_t>(encoded >> 8);
  hbc.data[entry_offset + 2] = static_cast<uint8_t>(encoded >> 16);
  hbc.data[entry_offset + 3] = static_cast<uint8_t>(encoded >> 24);
}

std::array<uint8_t, kVoiceComponentIconBlockSize>
make_icon_block(bool asset_path) {
  if (asset_path) {
    /*
     * PUI Button.icon treats a string as a named system icon.  Local files
     * must be an ImageSource object: {uri: <string #13094>}.
     */
    return {
        0x71, 0x06, 0x26, 0x33, // LoadConstString r6, #13094
        0x03, 0x04,             // NewObject r4
        0x3d, 0x04, 0x06, 0xd3, // PutNewOwnByIdShort r4.uri = r6
        0x3d, 0x00, 0x04, 0x97, // PutNewOwnByIdShort props.icon = r4
        0x3d, 0x00, 0x05, 0x20, // PutNewOwnByIdShort props.onPress = r5
        0x74, 0x04,             // LoadConstUndefined r4
        0x74, 0x06              // LoadConstUndefined r6; exact-size padding
    };
  }

  /* Named system icons retain the stock string representation. */
  return {
      0x71, 0x06, 0x26, 0x33, // LoadConstString r6, #13094
      0x3d, 0x00, 0x06, 0x97, // PutNewOwnByIdShort props.icon = r6
      0x3d, 0x00, 0x05, 0x20, // PutNewOwnByIdShort props.onPress = r5
      0x74, 0x04, 0x08, 0x04, 0x04, // undefined r4 + Mov r4, r4
      0x3e, 0x00, 0x04, 0xc8, 0x5f  // props.getMenu = undefined
  };
}

std::array<uint8_t, kVoiceActionBodySize> make_action_body(uint16_t uri_id) {
  std::array<uint8_t, kVoiceActionBodySize> body{};
  size_t cursor = 0;
  auto emit = [&](uint8_t value) { body[cursor++] = value; };

  /* LinkingPS.openURL(<patched string #13209>) through module env slot 4. */
  emit(0x29); emit(0x00); emit(0x01);                   // GetEnvironment r0, 1
  emit(0x2e); emit(0x00); emit(0x00); emit(0x04);       // Load env[4]
  emit(0x35); emit(0x02); emit(0x00); emit(0x01);       // GetById LinkingPS
  emit(0x0e); emit(0x4d);
  emit(0x35); emit(0x01); emit(0x02); emit(0x02);       // GetById openURL
  emit(0x35); emit(0x6b);
  emit(0x71); emit(0x03);                               // LoadConstString r3
  emit(static_cast<uint8_t>(uri_id));
  emit(static_cast<uint8_t>(uri_id >> 8));
  emit(0x51); emit(0x00); emit(0x01); emit(0x02);       // Call2
  emit(0x03);
  emit(0x5a); emit(0x00);                               // Ret r0

  /* Valid unreachable instructions keep the fixed 107-byte function body. */
  while (cursor + 3 <= body.size()) {
    emit(0x08); emit(0x00); emit(0x00);                 // Mov r0, r0
  }
  while (cursor < body.size())
    emit(0x74), emit(0x00);                             // exact remainder is 2
  return body;
}

void update_footer(HbcView &hbc) {
  const size_t footer = kProfileFileLength - kFooterSha1Size;
  SHA1_CTX context;
  SHA1Init(&context);
  SHA1Update(&context, hbc.data, static_cast<uint32_t>(footer));
  SHA1Final(hbc.data + footer, &context);
}

} // namespace

#endif

void patch_control_center(unsigned char *buffer, int *size_ptr,
                          int buffer_capacity) {
#if SHELLUI_CONTROL_CENTER_PATCH == 1
  (void)buffer_capacity; // The profile is deliberately same-size.
  if (!buffer || !size_ptr || *size_ptr <= 0)
    return;

  onion::control_center::SnapshotEntry registration{};
  if (!onion::control_center::visual_registration(&registration))
    return;

  /*
   * Resolve every size-dependent value before the first write.  Bailing out
   * after a partial write would leave the HBC mutated with a stale footer
   * SHA-1, and the loader rejects the whole bundle instead of ignoring it.
   */
  const bool asset_path = !registration.icon_source.empty() &&
                          registration.icon_source.front() == '/';
  const std::string description = registration.label + " control";
  char uri[24]{};
  const int uri_length = snprintf(uri, sizeof(uri), "OnionCC?invoke=%u",
                                  registration.registry_id);
  if (registration.icon_source.size() > kIconCapacity ||
      description.size() > kDescriptionCapacity || uri_length <= 0 ||
      static_cast<size_t>(uri_length) > kUriCapacity) {
    LOG_ERROR("control_center_patch: '%s' does not fit the 11.6 host "
              "(icon %u/%u, description %u/%u, uri %d/%u)",
              registration.id.c_str(),
              static_cast<unsigned>(registration.icon_source.size()),
              static_cast<unsigned>(kIconCapacity),
              static_cast<unsigned>(description.size()),
              static_cast<unsigned>(kDescriptionCapacity), uri_length,
              static_cast<unsigned>(kUriCapacity));
    return;
  }

  HbcView hbc{};
  if (!locate_hbc(buffer, static_cast<size_t>(*size_ptr), &hbc))
    return;
  if (!profile_matches(hbc)) {
    LOG_WARN("control_center_patch: skipped, no NPXS40003 11.6 profile");
    return;
  }

  static constexpr uint8_t kRuntimeRevokeExpected[] = {0x0b, 0x17, 0x19};
  static constexpr uint8_t kCustomRevokeExpected[] = {0x0b, 0x1a, 0x19};
  static constexpr uint8_t kCustomPropertyExpected[] = {0x3e, 0x19, 0x1a,
                                                         0xfb, 0x97};
  static constexpr uint8_t kRegistryExpected[] = {0x05, 0x00, 0x00, 0x00};
  static constexpr uint8_t kOnPressClosureExpected[] = {0x62, 0x01, 0x04,
                                                         0x9f, 0x8c};
  static constexpr uint8_t kLabelLoadExpected[] = {0x2e, 0x07, 0x00, 0x0a};
  static constexpr uint8_t kIconBlockExpected[] = {
      0x71, 0x06, 0x26, 0x33, 0x3d, 0x00, 0x06, 0x97,
      0x3d, 0x00, 0x05, 0x20, 0x62, 0x04, 0x04, 0xa0,
      0x8c, 0x3e, 0x00, 0x04, 0xc8, 0x5f};
  static constexpr uint8_t kActionPrefixExpected[] = {0x6a, 0x02, 0x01,
                                                       0x35, 0x05, 0x02};
  static constexpr uint8_t kIconEntryExpected[] = {0x2a, 0x2d, 0x0c, 0x0d};
  static constexpr uint8_t kSenderEntryExpected[] = {0x60, 0x1f, 0x02, 0x06};
  static constexpr uint8_t kTooltipTitlePropsEntryExpected[] = {
      0x72, 0x4a, 0x0c, 0x11};
  static constexpr char kLabelExpected[] = "msgid_voice";
  static constexpr char kIconExpected[] = "voice_command";
  static constexpr char kIconCarrierExpected[] =
      "psvoiceagent:main?sender=fc&actiontype=toggle";
  static constexpr char kSenderCarrierExpected[] =
      "go to voice command settings";
  static constexpr char kSender[] = "sender";
  static constexpr char kTooltipTitleProps[] = "tooltipTitleProps";
  static constexpr char kUriExpected[] =
      "pssettings:voiceagent?entry=voice_agent";

  if (!expect(hbc, kRuntimeVoiceRevokeNot, kRuntimeRevokeExpected,
              sizeof(kRuntimeRevokeExpected), "runtime voice revoke") ||
      !expect(hbc, kCustomVoiceRevokeNot, kCustomRevokeExpected,
              sizeof(kCustomRevokeExpected), "custom voice revoke") ||
      !expect(hbc, kCustomVoiceProperty, kCustomPropertyExpected,
              sizeof(kCustomPropertyExpected), "custom voice property") ||
      !expect(hbc, kVoiceRegistrySerializedInteger, kRegistryExpected,
              sizeof(kRegistryExpected), "voice registryId") ||
      !expect(hbc, kVoiceComponentOnPressClosure, kOnPressClosureExpected,
              sizeof(kOnPressClosureExpected), "voice onPress closure") ||
      !expect(hbc, kVoiceComponentLabelLoad, kLabelLoadExpected,
              sizeof(kLabelLoadExpected), "voice label load") ||
      !expect(hbc, kVoiceComponentIconBlock, kIconBlockExpected,
              sizeof(kIconBlockExpected), "voice icon/property block") ||
      !expect(hbc, kVoiceActionBody, kActionPrefixExpected,
              sizeof(kActionPrefixExpected), "voice action body") ||
      !expect(hbc, kLabelStringStorage,
              reinterpret_cast<const uint8_t *>(kLabelExpected),
              sizeof(kLabelExpected) - 1, "voice label") ||
      !expect(hbc, kOriginalIconStringStorage,
              reinterpret_cast<const uint8_t *>(kIconExpected),
              sizeof(kIconExpected) - 1, "voice icon") ||
      !expect(hbc, kIconStringEntry, kIconEntryExpected,
              sizeof(kIconEntryExpected), "icon string entry") ||
      !expect(hbc, kSenderStringEntry, kSenderEntryExpected,
              sizeof(kSenderEntryExpected), "sender string entry") ||
      !expect(hbc, kTooltipTitlePropsStringEntry,
              kTooltipTitlePropsEntryExpected,
              sizeof(kTooltipTitlePropsEntryExpected),
              "tooltipTitleProps string entry") ||
      !expect(hbc, kIconStorage - 2,
              reinterpret_cast<const uint8_t *>(kIconCarrierExpected),
              sizeof(kIconCarrierExpected) - 1, "icon carrier storage") ||
      !expect(hbc, kSenderRelocatedStorage - 1,
              reinterpret_cast<const uint8_t *>(kSenderCarrierExpected),
              sizeof(kSenderCarrierExpected) - 1,
              "sender relocation carrier") ||
      !expect(hbc, kUriStringStorage,
              reinterpret_cast<const uint8_t *>(kUriExpected),
              sizeof(kUriExpected) - 1, "voice URI")) {
    return;
  }

  /* Both runtime and customization descriptors become non-revoked. */
  hbc.data[kRuntimeVoiceRevokeNot + 2] = 0x03; // !global == false
  hbc.data[kCustomVoiceRevokeNot + 2] =
      registration.enabled_by_default ? 0x00 : 0x03;

  if (registration.enabled_by_default) {
    /* !undefined == true; store it as isDefault instead of isRevoked. */
    hbc.data[kCustomVoiceProperty + 3] = 0x81; // isDefault string #34689
    hbc.data[kCustomVoiceProperty + 4] = 0x87;
  }

  hbc.data[kVoiceRegistrySerializedInteger] = registration.registry_id;

  /* useCallback now closes over the dedicated action body (#36000). */
  hbc.data[kVoiceComponentOnPressClosure + 3] = 0xa0;
  hbc.data[kVoiceComponentOnPressClosure + 4] = 0x8c;
  static constexpr uint8_t kRawLabelLoad[] = {0x71, 0x07, 0x36, 0x01};
  memcpy(hbc.data + kVoiceComponentLabelLoad, kRawLabelLoad,
         sizeof(kRawLabelLoad));
  const auto icon_block = make_icon_block(asset_path);
  memcpy(hbc.data + kVoiceComponentIconBlock, icon_block.data(),
         icon_block.size());

  memcpy(hbc.data + kSenderRelocatedStorage, kSender,
         sizeof(kSender) - 1);
  write_small_string_entry(hbc, kSenderStringEntry,
                           kSenderRelocatedStorageRelative,
                           static_cast<uint8_t>(sizeof(kSender) - 1));
  memcpy(hbc.data + kTooltipTitlePropsRelocatedStorage, kTooltipTitleProps,
         sizeof(kTooltipTitleProps) - 1);
  write_small_string_entry(
      hbc, kTooltipTitlePropsStringEntry,
      kTooltipTitlePropsRelocatedStorageRelative,
      static_cast<uint8_t>(sizeof(kTooltipTitleProps) - 1));
  memcpy(hbc.data + kIconStorage, registration.icon_source.data(),
         registration.icon_source.size());
  write_small_string_entry(hbc, kIconStringEntry, kIconStorageRelative,
                           static_cast<uint8_t>(registration.icon_source.size()));

  const auto body = make_action_body(13209);
  memcpy(hbc.data + kVoiceActionBody, body.data(), body.size());

  memcpy(hbc.data + kLabelStringStorage, description.data(),
         description.size());
  hbc.data[kLabelStringLengthByte] =
      static_cast<uint8_t>(registration.label.size());
  hbc.data[kDescriptionStringLengthByte] =
      static_cast<uint8_t>(description.size());
  replace_string(hbc, kUriStringStorage, kUriCapacity, kUriStringLengthByte,
                 std::string(uri, static_cast<size_t>(uri_length)));

  update_footer(hbc);
  LOG_DEBUG("control_center_patch: activated '%s' registryId=%u "
            "at NPXS40003 hbc_base=0x%llx",
            registration.id.c_str(), registration.registry_id,
            static_cast<unsigned long long>(hbc.base));
#else
  (void)buffer;
  (void)size_ptr;
  (void)buffer_capacity;
#endif
}
