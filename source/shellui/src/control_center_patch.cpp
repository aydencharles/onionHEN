/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Strict, same-size Hermes bytecode patch for 11.6 NPXS40003.  It turns the
 * stock `accessibility` control (registryId 7) into a generic extension host:
 * the tile keeps its native FunctionControl shell, while its label, icon and
 * popup become OnionHEN's and the popup renders an in-process WebView instead
 * of the stock system menu.  The stock customization reducer, drag ordering
 * and User.setInfo serialization keep owning tile state.
 *
 * Verified against the 11.6 NPXS40003 profile (HBC v89, fileLength 0x5e9350,
 * source hash ff55cd8f...48ab).  Four groups of writes, all same-size:
 *
 *   fn #35901 module 3478 render body, 80 bytes - rebuilt in place.
 *     Becomes jsx(FunctionControl, {label: #310, menuHeight: 640,
 *     icon: {uri: #13094}, getMenu: <closure #35903>}).
 *
 *   fn #35903, 689 bytes - module 3479's factory, reclaimed.  Module 3478
 *     requires 3479 and the stock #35901 closure is the bundle's only
 *     reference to it, so once the closure points at #35903's body directly
 *     the factory is dead and its whole slot is free.  Rebuilt as a bare
 *     native WebViewPS builder pointed at string #13209.
 *
 *   String storage - #310 (label), #13094 (icon carrier) and #13209 (WebView
 *     URI) are the three slots the two bodies read.  Repointing #13094 needs
 *     two identifier relocations first; see the kIconStorage note below.
 *
 *   fn #36469, 31 bytes - module 3525's reorder-row label memo, relocated
 *     into the 54-byte dead #35902 slot and rebuilt as a guarded body.  See
 *     the customization-page note below: the grid draws its own rows from
 *     module 193's descriptor strings, so neither the tile rewrite nor the
 *     #310 label write reaches it on their own.
 *
 * Scope note: 11.6 exposes five feature-gated tiles (registryId 2..6:
 * notifications, gamingLounge, music, voiceAgent, broadcast) and ten
 * unconditional ones.  This build claims accessibility (registryId 7), which
 * module 193's factory emits with no `isRevoked` member at all, so neither a
 * feature-gate rewrite nor an isRevoked rewrite is needed.  See
 * CONTROL_CENTER_SDK.md for how to claim the remaining slots.
 *
 * Historical note: an earlier revision also rewrote module 193's revoke
 * expressions for the Voice Agent tile (registryId 5) and module 3482's
 * component plus its #36000 getMenu body.  That half has been removed -- it
 * was only ever a side effect on a tile retail firmware reports disabled, and
 * the accessibility host never depended on any of it.
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

/*
 * The three string slots the rebuilt bodies read.  All of them are stock
 * strings belonging to other modules; the accessibility host only borrows
 * their *slots*, never a live code path:
 *
 *   #310  "msgid_voice"     module 3482's tile label   -> OnionHEN's label
 *   #13094 "voice_command"  module 3482's tile icon    -> repointed at
 *                                                         kIconStorage
 *   #13209 "pssettings:voiceagent?entry=voice_agent"   -> the WebView URI
 *
 * Module 3482 is the Voice Agent tile, which `enableVoiceAgentFC` keeps from
 * ever rendering on retail firmware, so overwriting its strings is inert.
 */
constexpr size_t kLabelStringStorage = 0x0d599b;      // string #310
constexpr size_t kLabelStringLengthByte = 0x0ae3ff;
/*
 * #310 and #311 ("msgid_voice_cc_description") share one storage base; #310
 * reads the label prefix and #311 the whole thing.  0x0d59b3 is where string
 * #15035 ("onGestureFinish") starts, and that identifier is live -- function
 * #18491 reads it and has nothing to do with the control center -- so the
 * shared region is 24 bytes, not #311's declared 26.  Only the label is
 * written now, and the SDK caps it at 11 bytes, so this is headroom.
 */
constexpr size_t kLabelStorageCapacity = 0x0d59b3 - kLabelStringStorage; // 24
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
 * The icon carrier needs a contiguous 33-byte storage slot, and the only one
 * available sits inside string #2902
 * ("psvoiceagent:main?sender=fc&actiontype=toggle", 45 bytes at 0x0e647e).
 * That string's single reference is function #35998 in module 3482 -- the
 * Voice Agent tile, which never renders -- so the region is safe to reuse.
 * Starting two bytes in leaves 43 contiguous bytes; the first 33 hold the icon
 * source, which is why kIconStorage is kIconStorageRelative 0x10fa0 and not
 * 0x10f9e.
 *
 * Reusing it clobbers two *live* identifiers whose storage Hermes had
 * compacted into the same bytes, so both are relocated into dead string #9487
 * ("go to voice command settings", 28 bytes at 0x11657e) first:
 *
 *   #16885 "sender"           storage 0x0e6490, inside the icon region
 *   #28944 "tooltipTitleProps" storage 0x137a19, its last two bytes overlap
 *                              the URI slot at 0x137a28
 *
 * Neither is a control-center string: #16885 is used as a property name by
 * functions #11770/#11771/#11897/#17901/#17909/#23091, #28944 by #11770 and
 * #11771, none of which this patch touches.  Writing them anywhere else would
 * corrupt unrelated modules.
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
/*
 * #13209 declares 39 bytes, but string #13210 ("turn off:ps vr") starts one
 * byte earlier than that, at 0x137a4e, so 38 is the widest write that stays
 * inside the entry.  (#13210/#13211 have no bytecode references, so a 39-byte
 * write would be survivable -- but silently mangling a neighbour is not a
 * property worth relying on.)  The loopback URL this host bakes in is 22
 * bytes, so the guard is headroom.
 */
constexpr size_t kUriCapacity = 0x137a4e - kUriStringStorage; // 38
constexpr size_t kUriStringLengthByte = 0x0bad8b;

/*
 * ---------------------------------------------------------------------------
 * Accessibility host slot (registryId 7)
 * ---------------------------------------------------------------------------
 * The tile renders module 3478's default export:
 *
 *     memo(() => jsx(FunctionControl, {label, icon, getMenu}))
 *
 * `getMenu` -- not `onPress` -- is what the popup container calls, so it is
 * the correct hook for an in-app panel.  We keep the stock `getMenu` property
 * name and only redirect the closure at #35903, which becomes a WebView
 * builder.
 *
 * #35903 was module 3479's factory (the AccessibilityMenu).  Module 3478 is
 * the bundle's only `require(3479)`, and the stock #35901 closure is the only
 * reference to #35903, so once the closure points at the body directly the
 * factory is dead and its 689 bytes are free.  The 54-byte #35902 body, which
 * used to be the tile's getMenu, is left untouched.
 */
constexpr size_t kAccessibilityRenderBody = 0x59bb20;   // Function #35901
constexpr size_t kAccessibilityRenderBodySize = 80;
constexpr size_t kAccessibilityMenuBody = 0x59bba6;     // Function #35903
constexpr size_t kAccessibilityMenuBodySize = 689;
/*
 * Popup geometry.
 *
 * The popup container's size does NOT come from what `getMenu` returns.  The
 * tile component hands its own props to FunctionControl (module 73), whose
 * getMenu method (function #1285) reads `props.menuWidth` / `props.menuHeight`
 * and forwards them to PopupContainer (module 1746, function #19001).  That
 * one passes them to usePopupStyle (module 2742, function #28065), which
 *
 *   width  = menuWidth === undefined ? DEFAULT_MENU_WIDTH (652) : menuWidth
 *   height = menuHeight (only attached to the style when one is supplied)
 *
 * and merges the result over module 796's `popup` style
 * `{minWidth: 652, maxWidth: 1536, minHeight: 216, maxHeight: 810,
 *   position: 'absolute', bottom: 190}`.
 *
 * Module 3478's stock render body passes neither property, so the popup keeps
 * its default width and collapses onto `minHeight: 216` -- and the WebView
 * inside it, styled `{width: '100%', height: '100%'}`, has no definite parent
 * height to resolve its percentage against.  That is the "too short" panel.
 *
 * Module 2765 (music, menuWidth 1040) and the broadcast tile (menuWidth 652,
 * menuHeight 292) are the stock precedents for driving the popup this way;
 * this host follows them.  640 sits comfortably inside the platform clamp and
 * leaves the `bottom: 190` anchor clear of the tile row.
 *
 * The width stays at the 652 default: the rebuilt render body is a fixed 80
 * bytes and menuHeight already takes it to 78, so there is no room for a
 * second property.  Widening the panel would mean growing the body past its
 * slot, which this patch deliberately never does.
 */
constexpr uint16_t kMenuHeight116 = 640;
/*
 * String #310 already carries registration.label ("OnionHEN") once the label
 * write below has run, so the render body reads it directly and drops the
 * stock I18nPS.t('msgid_accessibility') call -- `t()` would miss a key that is
 * not in the translation table.
 */
constexpr uint16_t kLabelStringId = 310;
/* Icon carrier: repointed at kIconStorage by the icon write below. */
constexpr uint16_t kIconCarrierStringId = 13094;
/* WebView source URI: the URI write below retargets #13209 at web_url. */
constexpr uint16_t kWebViewUriStringId = 13209;

/*
 * Every remaining operand of the two rebuilt bodies, named so the emitted
 * bytes can be read against the hasm listing line by line.  The ids are the
 * ones the stock instructions use (verified in
 * disasm/rnps-control-center.hasm: #35901 for the render body, #33252 for the
 * only WebViewPS call site in the whole bundle).
 */
constexpr uint16_t kJsxIdentifierId = 163;              // 'jsx'
constexpr uint16_t kDefaultIdentifierId = 115;          // 'default'
constexpr uint16_t kLabelIdentifierId = 165;            // 'label'
constexpr uint16_t kIconIdentifierId = 151;             // 'icon'
constexpr uint16_t kUriIdentifierId = 211;              // 'uri'
constexpr uint16_t kMenuHeightIdentifierId = 17339;     // 'menuHeight'
constexpr uint16_t kGetMenuIdentifierId = 24520;        // 'getMenu'
constexpr uint16_t kSourceIdentifierId = 229;           // 'source'
constexpr uint16_t kStyleIdentifierId = 77;             // 'style'
constexpr uint16_t kWidthIdentifierId = 254;            // 'width'
constexpr uint16_t kHeightIdentifierId = 150;           // 'height'
constexpr uint16_t kWebViewPsIdentifierId = 33316;      // 'WebViewPS'
constexpr uint16_t kIgnoreDimByModalIdentifierId = 27578;
constexpr uint16_t kSuppressDialogShowingIdentifierId = 33369;
constexpr uint16_t kBackgroundTransparencyIdentifierId = 20605;
constexpr uint16_t kStartInLoadingStateIdentifierId = 27783;
constexpr uint16_t kAccessibilitySpeakableIdentifierId = 75;
constexpr uint16_t kPercentStringId = 1527;             // '100%'
constexpr uint16_t kOnlySelfStringId = 11249;           // 'onlySelf'
constexpr uint16_t kMenuClosureFunctionId = 35903;
constexpr uint16_t kI18nPsIdentifierId = 28;            // 'I18nPS'
constexpr uint16_t kTranslateIdentifierId = 10;         // 't'

/*
 * ---------------------------------------------------------------------------
 * Customization ("reorder") page
 * ---------------------------------------------------------------------------
 * The control center's customize/reorder grid does not render module 3478's
 * tile at all.  It builds its own rows from module 193's descriptor list:
 *
 *   module 193   defaultOrderControls = descriptors.map(d => ({id, label,
 *                description}))           <- label/description only, no icon
 *   module 3522  getControlInfo(id)       <- resolves iconId at run time
 *   module 3525  the grid + its header    <- renders label / description
 *   module 3508  one grid cell
 *   module 3515  the cell's button        <- renders the icon
 *
 * so the accessibility row keeps showing the stock accessibility entry even
 * after the tile itself has been rewritten.  Both of its halves are resolved
 * through natives this bundle cannot retarget by string surgery:
 *
 *   label       every row's label is I18nPS.t(<descriptor label>), compiled
 *               into function #36469 (module 3525):
 *                   r0 = _closure2_slot0; return I18nPS.t(r0)
 *               #12801 ("msgid_accessibility") is the accessibility
 *               descriptor's own label carrier, and its storage is shared
 *               with a live identifier, so it cannot be overwritten the way
 *               #310 was; and no free slot in the bundle holds a string that
 *               t() would translate to registration.label.
 *
 *   icon        rows hand their iconId to IconPS.getImageSource(name, 56, 56)
 *               (function #36398) and to PUI Button's `icon` prop (function
 *               #36397).  All 39 observed getImageSource arguments in this
 *               bundle are bare system icon names -- never a path -- and the
 *               native library has no /system_ex/vsh_asset lookup at all, so
 *               no string id can make the row resolve OnionHEN's PNG.  Fixing
 *               the icon means rewriting the shared 1130-byte button
 *               component (module 3515) behind an `id === 'accessibility'`
 *               guard, which is deliberately out of scope here.
 *
 * The label half is fixed by relocating function #36469's body.  #36469 is
 * referenced exactly once in the whole bundle -- a CreateClosure inside module
 * 3525 -- and its own 31-byte slot has no room for a guard, so the body moves
 * into the 54-byte #35902 slot that the accessibility host already frees
 * (see the kAccessibilityMenuBody note).  The rebuilt body keeps the stock
 * t() path for every other row and short-circuits the accessibility row to
 * string #310, which the label write above has already filled with
 * registration.label -- so the grid row and the tile show the same text
 * without depending on t()'s behaviour for a key that is not in the
 * translation table.
 */
constexpr uint16_t kAccessibilityLabelStringId = 12801; // 'msgid_accessibility'
/* Function header table entry (16 bytes per function) for function #36469. */
constexpr size_t kLabelMemoHeader = 0x80 + 36469 * 16;
/* The dead #35902 body: 0x59bb20 + 80 == 0x59bb70, ending at 0x59bba6. */
constexpr size_t kLabelMemoSlot = kAccessibilityRenderBody +
                                  kAccessibilityRenderBodySize;
constexpr uint16_t kLabelMemoSize = 52;
constexpr uint16_t kLabelMemoFrameSize = 74;

/* Low/high byte of a 16-bit string or identifier operand. */
constexpr uint8_t lo8(uint16_t value) {
  return static_cast<uint8_t>(value & 0xffu);
}
constexpr uint8_t hi8(uint16_t value) {
  return static_cast<uint8_t>(value >> 8);
}

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
static_assert(kUriStringLengthByte == kSmallStringTable + 13209 * 4 + 3,
              "URI length byte must be string #13209's entry high byte");
static_assert(kAccessibilityMenuBody - kLabelMemoSlot == 54,
              "the reclaimed #35902 slot is 54 bytes");
static_assert(kLabelMemoSize <= kAccessibilityMenuBody - kLabelMemoSlot,
              "the relocated label memo must fit the dead #35902 slot");

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

void write_u32le(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
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

/*
 * Module 3478's render function (#35901), rebuilt at its exact 80-byte size.
 *
 * Stock body (hasm #35901):
 *   GetEnvironment r0, 0
 *   env[2].jsx, env[1].default, env[0].I18nPS.t('msgid_accessibility')
 *   NewObject props; props.label = t(...); props.icon = 'accessibility'
 *   CreateEnvironment; CreateClosure #35902; props.getMenu = closure
 *   Call3 jsx(undefined, FunctionControl, props); Ret
 *
 * Ours keeps the frame, the environment reads and the call shape identical,
 * and changes only the four values:
 *   string #310            -> props.label   (t() bypassed: #310 already holds
 *                                            registration.label)
 *   kMenuHeight116         -> props.menuHeight (see the geometry note above;
 *                                            this is what gives the popup, and
 *                                            therefore the WebView inside it,
 *                                            a definite height)
 *   {uri: string #13094}   -> props.icon    (PUI Button.icon takes a string as
 *                                            a named system icon, so a local
 *                                            PNG needs an ImageSource object)
 *   closure #35903         -> props.getMenu (property name unchanged, so the
 *                                            popup container keeps calling it)
 *
 * The stock CreateEnvironment/CreateClosure pair is preserved byte for byte:
 * #35901 has environmentSize 0, so the new environment is a bare parent link
 * and #35903's `GetEnvironment r0, 1` still lands on the module factory
 * environment exactly as the old #35902 did.
 *
 * Budget: the rebuilt body is 78 of the slot's 80 bytes, and the two left over
 * are a LoadConstUndefined pair, so the function stays exactly as long as the
 * stock one.
 */
static_assert(kMenuHeight116 >= 216 && kMenuHeight116 <= 810,
              "menuHeight must stay inside module 796's popup min/max clamp");

std::array<uint8_t, kAccessibilityRenderBodySize>
make_accessibility_render() {
  return {
      0x29, 0x00, 0x00,                   // GetEnvironment r0, 0
      0x2e, 0x01, 0x00, 0x02,             // LoadFromEnvironment r1, r0, 2
      0x34, 0x03, 0x01, 0x01, lo8(kJsxIdentifierId),
                                          // GetByIdShort r3, r1, #jsx
      0x2e, 0x01, 0x00, 0x01,             // LoadFromEnvironment r1, r0, 1
      0x34, 0x02, 0x01, 0x02, lo8(kDefaultIdentifierId),
                                          // GetByIdShort r2, r1, #default
      0x71, 0x00, lo8(kLabelStringId), hi8(kLabelStringId),
                                          // LoadConstString r0, #310
      0x03, 0x01,                         // NewObject r1 (props)
      0x3d, 0x01, 0x00, lo8(kLabelIdentifierId),
                                          // props.label = r0
      0x6d, 0x05,                         // LoadConstInt r5, kMenuHeight116
      lo8(kMenuHeight116), hi8(kMenuHeight116), 0x00, 0x00,
      0x3e, 0x01, 0x05, lo8(kMenuHeightIdentifierId),
      hi8(kMenuHeightIdentifierId),       // props.menuHeight = r5
      0x71, 0x06, lo8(kIconCarrierStringId), hi8(kIconCarrierStringId),
                                          // LoadConstString r6, #13094
      0x03, 0x04,                         // NewObject r4
      0x3d, 0x04, 0x06, lo8(kUriIdentifierId),
                                          // r4.uri = r6
      0x3d, 0x01, 0x04, lo8(kIconIdentifierId),
                                          // props.icon = r4
      0x32, 0x00,                         // CreateEnvironment r0
      0x62, 0x00, 0x00, lo8(kMenuClosureFunctionId),
      hi8(kMenuClosureFunctionId),        // CreateClosure r0, r0, #35903
      0x3e, 0x01, 0x00, lo8(kGetMenuIdentifierId),
      hi8(kGetMenuIdentifierId),          // props.getMenu = r0
      0x74, 0x00,                         // LoadConstUndefined r0
      0x52, 0x00, 0x03, 0x00, 0x02, 0x01, // Call3 r0, r3, r0, r2, r1
      0x5a, 0x00,                         // Ret r0
      /* Unreachable padding to the fixed 80-byte body. */
      0x74, 0x00};
}

/*
 * The popup body (#35903).  The popup container calls getMenu(props) and
 * renders whatever it returns, so returning a native WebView is all that is
 * needed for an in-app panel -- no LinkingPS.openURL involved.
 *
 * WebViewPS is not a module: it is a native component resolved out of the
 * registry that module 1 exposes (`require(149)('NiSiE5q6')`), which is also
 * where I18nPS comes from.  The bundle references the name 'WebViewPS' exactly
 * once, in function #33252, and the prop set below is that call site's, minus
 * the four event/ref props (this panel has no JS handlers to wire).
 *
 * #33252 lives inside module 3213's LiteFrame HOC and reads its jsx and native
 * registry out of environment slots 9 and 4.  We instead read slots 2 and 0 of
 * the module factory environment, which is what #35903's parent chain exposes:
 * the factory stores require(1) in slot 0 and require(3)/jsx in slot 2.
 *
 * Module 3213 wraps WebViewPS in its own HOC; we deliberately use the bare
 * native component so the popup keeps its own layout.
 */
std::array<uint8_t, kAccessibilityMenuBodySize> make_webview_menu() {
  static constexpr uint8_t kCode[] = {
      0x29, 0x00, 0x01,                   // GetEnvironment r0, 1
      0x2e, 0x01, 0x00, 0x02,             // LoadFromEnvironment r1, r0, 2
      0x34, 0x01, 0x01, 0x01, lo8(kJsxIdentifierId),
                                          // GetByIdShort r1, r1, #jsx
      0x2e, 0x02, 0x00, 0x00,             // LoadFromEnvironment r2, r0, 0
      0x35, 0x02, 0x02, 0x02, lo8(kWebViewPsIdentifierId),
      hi8(kWebViewPsIdentifierId),        // GetById r2, r2, #WebViewPS
      0x71, 0x03, lo8(kWebViewUriStringId), hi8(kWebViewUriStringId),
                                          // LoadConstString r3, #13209 (URI)
      0x71, 0x05, lo8(kPercentStringId), hi8(kPercentStringId),
                                          // LoadConstString r5, #1527 ('100%')
      0x71, 0x08, lo8(kOnlySelfStringId), hi8(kOnlySelfStringId),
                                          // LoadConstString r8, #11249
      0x03, 0x04,                         // NewObject r4 (source)
      0x3d, 0x04, 0x03, lo8(kUriIdentifierId),
                                          // r4.uri = r3
      0x03, 0x06,                         // NewObject r6 (style)
      0x3d, 0x06, 0x05, lo8(kWidthIdentifierId),
                                          // r6.width = r5
      0x3d, 0x06, 0x05, lo8(kHeightIdentifierId),
                                          // r6.height = r5
      0x76, 0x09,                         // LoadConstTrue r9
      0x76, 0x0a,                         // LoadConstTrue r10
      0x76, 0x0b,                         // LoadConstTrue r11
      0x77, 0x0c,                         // LoadConstFalse r12
      0x03, 0x07,                         // NewObject r7 (props)
      0x3d, 0x07, 0x04, lo8(kSourceIdentifierId),
                                          // props.source = r4
      0x3d, 0x07, 0x06, lo8(kStyleIdentifierId),
                                          // props.style = r6
      0x3e, 0x07, 0x08, lo8(kIgnoreDimByModalIdentifierId),
      hi8(kIgnoreDimByModalIdentifierId), // props.ignoreDimByModal = r8
      0x3e, 0x07, 0x09, lo8(kSuppressDialogShowingIdentifierId),
      hi8(kSuppressDialogShowingIdentifierId),
                                          // props.suppressDialogShowing = r9
      0x3e, 0x07, 0x0a, lo8(kBackgroundTransparencyIdentifierId),
      hi8(kBackgroundTransparencyIdentifierId),
                                          // props.backgroundTransparencyEnabled
      0x3e, 0x07, 0x0b, lo8(kStartInLoadingStateIdentifierId),
      hi8(kStartInLoadingStateIdentifierId),
                                          // props.startInLoadingState = r11
      0x3e, 0x07, 0x0c, lo8(kAccessibilitySpeakableIdentifierId),
      hi8(kAccessibilitySpeakableIdentifierId),
                                          // props.accessibilitySpeakable = r12
      0x74, 0x03,                         // LoadConstUndefined r3
      0x52, 0x00, 0x01, 0x03, 0x02, 0x07, // Call3 r0, r1, r3, r2, r7
      0x5a, 0x00,                         // Ret r0
  };
  static_assert(sizeof(kCode) <= kAccessibilityMenuBodySize,
                "WebView builder must fit the reclaimed function slot");
  static_assert(sizeof(kCode) % 2 == 1,
                "odd body keeps the LoadConstUndefined padding pair-aligned");

  std::array<uint8_t, kAccessibilityMenuBodySize> body{};
  memcpy(body.data(), kCode, sizeof(kCode));
  /* Unreachable filler keeps the fixed 689-byte body.  The trailing byte that
   * a pair cannot cover stays 0x00 (Unreachable); Ret already ran. */
  for (size_t i = sizeof(kCode); i + 1 < body.size(); i += 2) {
    body[i] = 0x74;                     // LoadConstUndefined r0
    body[i + 1] = 0x00;
  }
  return body;
}

/*
 * Module 3525's reorder-row label memo (function #36469), rebuilt at 52 of the
 * dead #35902 slot's 54 bytes.
 *
 * Stock body (hasm #36469, 31 bytes):
 *   GetEnvironment r0, 1
 *   env[1][2].I18nPS.t(env[0][0])    ; env[0][0] is the row's descriptor label
 *   Ret
 *
 * `env[0]` is the closure environment, which module 3525 shares between the
 * label and the description memos: slot 0 holds `label` and slot 1 holds
 * `description`, for every row of the grid.
 *
 * Ours keeps that t() path verbatim as the fall-through tail and prepends a
 * four-instruction guard:
 *
 *   if (label !== 'msgid_accessibility') goto <tail>
 *   return 'OnionHEN'                       ; string #310, written by the
 *                                           ; label write above
 *
 * so only the accessibility row is short-circuited; the other fifteen rows and
 * the synthetic "reset to default" entry keep their own t() lookups.  The
 * comparison is against the descriptor's own label id (#12801), which is what
 * module 193 emits for registryId 7, so no descriptor write is needed.
 *
 * Encoding notes, both verified against the hasm listing:
 *   JStrictNotEqual is [op, Addr8, Reg8, Reg8] and jumps when the two
 *   registers are NOT strictly equal; its target is the instruction's own
 *   address plus Addr8.  The branch sits at offset 11 and the tail at 21, so
 *   the displacement is 10.
 */
std::array<uint8_t, kLabelMemoSize> make_label_memo() {
  return {
      0x29, 0x00, 0x00,                   // GetEnvironment r0, 0
      0x2e, 0x00, 0x00, 0x00,             // LoadFromEnvironment r0, r0, 0
      0x71, 0x01, lo8(kAccessibilityLabelStringId),
      hi8(kAccessibilityLabelStringId),   // LoadConstString r1, #12801
      0xbc, 0x0a, 0x00, 0x01,             // JStrictNotEqual r0, r1, +10
      0x71, 0x00, lo8(kLabelStringId), hi8(kLabelStringId),
                                          // LoadConstString r0, #310
      0x5a, 0x00,                         // Ret r0
      0x29, 0x00, 0x01,                   // GetEnvironment r0, 1
      0x2e, 0x00, 0x00, 0x02,             // LoadFromEnvironment r0, r0, 2
      0x34, 0x02, 0x00, 0x01, lo8(kI18nPsIdentifierId),
                                          // GetByIdShort r2, r0, #I18nPS
      0x34, 0x01, 0x02, 0x02, lo8(kTranslateIdentifierId),
                                          // GetByIdShort r1, r2, #t
      0x29, 0x00, 0x00,                   // GetEnvironment r0, 0
      0x2e, 0x00, 0x00, 0x00,             // LoadFromEnvironment r0, r0, 0
      0x51, 0x00, 0x01, 0x02, 0x00,       // Call2 r0, r1, r2, r0
      0x5a, 0x00};                        // Ret r0
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
  char uri[24]{};
  const int uri_length = snprintf(uri, sizeof(uri), "OnionCC?invoke=%u",
                                  registration.registry_id);
  /*
   * #13209 is the popup's WebView source.  A host that runs a web UI supplies
   * web_url and the popup loads that; otherwise it falls back to the invoke
   * URI, which keeps the OnionCC?invoke=<id> dispatch path usable on its own.
   */
  const std::string web_uri =
      registration.web_url.empty()
          ? std::string(uri, static_cast<size_t>(uri_length > 0 ? uri_length : 0))
          : registration.web_url;
  if (registration.icon_source.size() > kIconCapacity ||
      registration.label.size() > kLabelStorageCapacity || uri_length <= 0 ||
      web_uri.size() > kUriCapacity) {
    LOG_ERROR("control_center_patch: '%s' does not fit the 11.6 host "
              "(icon %u/%u, label %u/%u, uri %u/%u)",
              registration.id.c_str(),
              static_cast<unsigned>(registration.icon_source.size()),
              static_cast<unsigned>(kIconCapacity),
              static_cast<unsigned>(registration.label.size()),
              static_cast<unsigned>(kLabelStorageCapacity),
              static_cast<unsigned>(web_uri.size()),
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

  static constexpr uint8_t kAccessibilityRenderExpected[] = {
      0x29, 0x00, 0x00, 0x2e, 0x01, 0x00, 0x02, 0x34};
  static constexpr uint8_t kAccessibilityMenuExpected[] = {
      0x32, 0x01, 0x6a, 0x02, 0x02, 0x6a, 0x06, 0x03, 0x74, 0x00};
  /* The stock #35902 body: function #35901's only CreateClosure target, and
   * therefore dead once the rebuilt render body points at #35903 directly. */
  static constexpr uint8_t kLabelMemoSlotExpected[] = {
      0x29, 0x00, 0x01, 0x2e, 0x02, 0x00, 0x03, 0x74};
  /* Function #36469's header: offset 0x2aaa52, paramCount 1, size 31,
   * frameSize 74 (the top byte of w1 carries unrelated function flags). */
  static constexpr uint8_t kLabelMemoHeaderExpected[] = {
      0x52, 0xaa, 0x2a, 0x02, 0x1f, 0x00, 0x25, 0x1d};
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

  /*
   * Every probe guards bytes this patch goes on to overwrite.  A mismatch
   * means the bundle is not the profile the offsets were derived from, so the
   * patch bails out before touching anything.
   */
  if (!expect(hbc, kLabelStringStorage,
              reinterpret_cast<const uint8_t *>(kLabelExpected),
              sizeof(kLabelExpected) - 1, "label string") ||
      !expect(hbc, kOriginalIconStringStorage,
              reinterpret_cast<const uint8_t *>(kIconExpected),
              sizeof(kIconExpected) - 1, "icon string") ||
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
              sizeof(kUriExpected) - 1, "uri string") ||
      !expect(hbc, kAccessibilityRenderBody, kAccessibilityRenderExpected,
              sizeof(kAccessibilityRenderExpected), "accessibility render") ||
      !expect(hbc, kAccessibilityMenuBody, kAccessibilityMenuExpected,
              sizeof(kAccessibilityMenuExpected), "accessibility menu body") ||
      !expect(hbc, kLabelMemoSlot, kLabelMemoSlotExpected,
              sizeof(kLabelMemoSlotExpected), "dead #35902 body") ||
      !expect(hbc, kLabelMemoHeader, kLabelMemoHeaderExpected,
              sizeof(kLabelMemoHeaderExpected), "label memo header")) {
    return;
  }

  /*
   * Identifier surgery first: the icon and URI writes below land on bytes that
   * Hermes had compacted into the storage of two identifiers other modules
   * still use, so both get moved into dead string #9487 before anything else
   * is written.
   */
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

  /* Icon carrier: point #13094 at the reclaimed 33-byte slot and fill it. */
  memcpy(hbc.data + kIconStorage, registration.icon_source.data(),
         registration.icon_source.size());
  write_small_string_entry(hbc, kIconStringEntry, kIconStorageRelative,
                           static_cast<uint8_t>(registration.icon_source.size()));

  /*
   * Tile label.  Only #310 is written; #311 ("msgid_voice_cc_description")
   * shares the same base but is referenced solely by module 3482's descriptor
   * literal, which never renders, so its length byte is left alone.
   */
  memcpy(hbc.data + kLabelStringStorage, registration.label.data(),
         registration.label.size());
  hbc.data[kLabelStringLengthByte] =
      static_cast<uint8_t>(registration.label.size());

  /* Popup WebView source. */
  replace_string(hbc, kUriStringStorage, kUriCapacity, kUriStringLengthByte,
                 web_uri);

  /*
   * Accessibility host: rebuild the render body and turn the reclaimed
   * function slot into a WebView builder.  The stock getMenu property name is
   * preserved, so the popup container keeps calling it.
   */
  const auto render = make_accessibility_render();
  memcpy(hbc.data + kAccessibilityRenderBody, render.data(), render.size());
  const auto menu = make_webview_menu();
  memcpy(hbc.data + kAccessibilityMenuBody, menu.data(), menu.size());

  /*
   * Customization page: the grid's label memo (#36469) is too small for a
   * guard, so the body moves into the dead #35902 slot and only the function
   * header is repointed.  paramCount, frameSize and the flag bits in the top
   * byte of w1 are carried over from the stock header; the offset field is 25
   * bits wide, which 0x59bb70 fits comfortably.
   */
  const auto label_memo = make_label_memo();
  memcpy(hbc.data + kLabelMemoSlot, label_memo.data(), label_memo.size());
  const uint32_t stock_header_w0 = read_u32le(hbc.data + kLabelMemoHeader);
  const uint32_t stock_header_w1 = read_u32le(hbc.data + kLabelMemoHeader + 4);
  write_u32le(hbc.data + kLabelMemoHeader,
              static_cast<uint32_t>(kLabelMemoSlot) |
                  (stock_header_w0 & 0xFE000000u));
  write_u32le(hbc.data + kLabelMemoHeader + 4,
              (stock_header_w1 & 0xFF000000u) |
                  static_cast<uint32_t>(kLabelMemoSize) |
                  (static_cast<uint32_t>(kLabelMemoFrameSize) << 15));

  update_footer(hbc);
  LOG_DEBUG("control_center_patch: activated '%s' registryId=%u "
            "at NPXS40003 hbc_base=0x%llx (accessibility host, webview %s, "
            "reorder row label 0x%llx)",
            registration.id.c_str(), registration.registry_id,
            static_cast<unsigned long long>(hbc.base), web_uri.c_str(),
            static_cast<unsigned long long>(kLabelMemoSlot));
#else
  (void)buffer;
  (void)size_ptr;
  (void)buffer_capacity;
#endif
}
