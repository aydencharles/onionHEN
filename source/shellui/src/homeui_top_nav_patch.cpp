/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "homeui_top_nav_patch.hpp"

#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1

#include "defs.h"
#include "detour.h"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include <sha1.hpp>

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <string.h>

namespace {

constexpr unsigned char kHermesMagic[] = {0xc6, 0x1f, 0xbc, 0x03,
                                          0xc1, 0x03, 0x19, 0x1f};
constexpr unsigned char kLegacyRnpsBundleMagic[] = {0xe5, 0xd1, 0x0b, 0xfb};
constexpr unsigned char kPlainJsBundlePrefix[] = {
    '/', '*', '!', ' ', 'F', 'o', 'r', ' ', 'l', 'i', 'c', 'e', 'n', 's', 'e',
    ' ', 'i', 'n', 'f', 'o', 'r', 'm', 'a', 't', 'i', 'o', 'n'};
constexpr unsigned char kRnpsMagic[] = {'R', 'N', 'P', 'S',
                                        'H', 'E', 'D', 'R'};

constexpr size_t kRnpsPayloadOffsetField = 0x1c;
constexpr size_t kRnpsFallbackPayloadOffset = 0xb20;
constexpr size_t kHbcVersionOffset = 0x08;
constexpr size_t kHbcSourceHashOffset = 0x0c;
constexpr size_t kHbcSourceHashSize = 20;
constexpr size_t kHbcFileLengthOffset = 0x20;
constexpr size_t kHbcFooterSha1Size = 20;
constexpr const char *kOnionHenTopNavIconPath =
    "/system_ex/vsh_asset/onionhen.png";

static const unsigned char kLegacyOldIconOrder[] = {
    '[', '"', 'F', 'p', 's', '"', ',', '"', 'S', 'e', 'a', 'r', 'c', 'h',
    '"', ',', '"', 'S', 'e', 't', 't', 'i', 'n', 'g', 's', '"', ',', '"',
    'P', 'r', 'o', 'f', 'i', 'l', 'e', '"', ']'};
static const unsigned char kLegacyNewIconOrder[] = {
    '[', '"', 'S', 'e', 'a', 'r', 'c', 'h', '"', ',', '"', 'A', 'p', 'p',
    '"', ',', '"', 'S', 'e', 't', 't', 'i', 'n', 'g', 's', '"', ',', '"',
    'P', 'r', 'o', 'f', 'i', 'l', 'e', '"', ']'};
static const unsigned char kLegacyOldExportAlias[] = {
    't', '.', 'F', 'p', 's', '=', 'P'};
static const unsigned char kLegacyNewExportAlias[] = {
    't', '.', 'A', 'p', 'p', '=', 'h'};
static const char kLegacyOldAppErrorSource[] =
    "var h=(0,u().memo)((function(){var e=(0,m.default)().sendClientApplicationErrorEvent;return u().default.createElement(d.default,{iconId:\"download_error\",onPress:function(){var t=new Error(\"homeui ApplicationErrorEvent test\");e({errorMessage:t.message,stack:t.stack,severity:\"info\"})},title:\"Trigger AppError\",__source:{fileName:_,lineNumber:80}})}));t.ApplicationErrorEventTrigger=h;";
/* Module 231 forwards iconId directly to PUI Button.icon. */
static const char kLegacyNewAppErrorSourcePrefix[] =
    "var h=(0,u().memo)((function(){var e=(0,f.useInteractivePress)({link:\"OnionHEN?NavUI=1\"});return u().default.createElement(d.default,{iconId:{uri:\"/system_ex/vsh_asset/onionhen.png\"},onPress:e,title:\"\",__source:{fileName:_,lineNumber:80}})}));t.ApplicationErrorEventTrigger=h;";
/* 2.30/2.50 predate Fps/AppError system icons; reuse a debug-only Startup slot. */
static const unsigned char kLegacy2xOldIconOrder[] =
    "[\"StartupAnimation\",\"HiddenStartupAnimation\",\"Search\",\"Settings\",\"Profile\"]";
static const unsigned char kLegacy2xNewIconOrder[] =
    "[\"OnionHEN\",\"Search\",\"Settings\",\"Profile\"]"
    "                                 ";
static const unsigned char kLegacy2xOldExportAlias[] =
    "t.StartupAnimation=_;";
static const unsigned char kLegacy2xNewExportAlias[] =
    "t.OnionHEN=_        ;";
static const char kLegacy2xOldButtonSource[] =
    "var _=(0,s().memo)((function(){var e=(0,s().useContext)(o().AppConfigContext).appConfig,t=(0,c.useInteractivePress)({action:\"click ani\",link:\"pshomeui:navigateToHome?intro=login\"});return e.isEnabled(\"showStartupButton\")?s().default.createElement(l.default,{iconId:\"trophy_rarity1_ultrarare\",onPress:function(){t()},title:\"Trigger Startup\",__source:{fileName:f,lineNumber:48}}):null}));";
static const char kLegacy2xNewButtonSourcePrefix[] =
    "var _=(0,s().memo)((function(){var e=(0,c.useInteractivePress)({link:\"OnionHEN?NavUI=1\"});return s().default.createElement(l.default,{iconId:{uri:\"/system_ex/vsh_asset/onionhen.png\"},onPress:e,title:\"\",__source:{fileName:f,lineNumber:48}})}));";
/* 3.00 through 3.21 use different minified identifiers than 4.x. */
static const unsigned char kLegacy3xOldExportAlias[] = {
    't', '.', 'F', 'p', 's', '=', 'S'};
static const unsigned char kLegacy3xNewExportAlias[] = {
    't', '.', 'A', 'p', 'p', '=', 'E'};
static const char kLegacy3xOldAppErrorSource[] =
    "var E=(0,s().memo)((function(){var e=(0,c.default)().sendClientApplicationErrorEvent;return s().default.createElement(f.default,{iconId:\"download_error\",onPress:function(){var t=new Error(\"homeui ApplicationErrorEvent test\");e({errorMessage:t.message,stack:t.stack,severity:\"info\"})},title:\"Trigger AppError\",__source:{fileName:d,lineNumber:77}})}));t.ApplicationErrorEventTrigger=E;";
static const char kLegacy3xNewAppErrorSourcePrefix[] =
    "var E=(0,s().memo)((function(){var e=(0,l.useInteractivePress)({link:\"OnionHEN?NavUI=1\"});return s().default.createElement(f.default,{iconId:{uri:\"/system_ex/vsh_asset/onionhen.png\"},onPress:e,title:\"\",__source:{fileName:d,lineNumber:77}})}));t.ApplicationErrorEventTrigger=E;";
/* 5.10 through 7.61 share this minified SystemIcon module shape. */
static const unsigned char kLegacy5x7xOldExportAlias[] = {
    't', '.', 'F', 'p', 's', '=', 'h'};
static const unsigned char kLegacy5x7xNewExportAlias[] = {
    't', '.', 'A', 'p', 'p', '=', 'b'};
static const char kLegacy5x7xOldAppErrorSource[] =
    "var b=(0,u().memo)((function(){var e=(0,f.default)().sendClientApplicationErrorEvent;return u().default.createElement(p.default,{iconId:\"download_error\",onPress:function(){var t=new Error(\"homeui ApplicationErrorEvent test\");e({errorMessage:t.message,stack:t.stack,severity:\"info\"})},title:\"Trigger AppError\",__source:{fileName:v,lineNumber:80,columnNumber:10}})}));t.ApplicationErrorEventTrigger=b;";
static const char kLegacy5x7xNewAppErrorSourcePrefix[] =
    "var b=(0,u().memo)((function(){var e=(0,c.useInteractivePress)({link:\"OnionHEN?NavUI=1\"});return u().default.createElement(p.default,{iconId:{uri:\"/system_ex/vsh_asset/onionhen.png\"},onPress:e,title:\"\",__source:{fileName:v,lineNumber:80,columnNumber:10}})}));t.ApplicationErrorEventTrigger=b;";

static const unsigned char kPlainJsOldExportAlias[] = {
    't', '.', 'F', 'p', 's', '=', 'I'};
static const unsigned char kPlainJsNewExportAlias[] = {
    't', '.', 'A', 'p', 'p', '=', 'b'};
static const char kPlainJsOldAppErrorSource[] =
    "var b=(0,a.memo)((function(){var e=(0,l.default)().sendClientApplicationErrorEvent;return(0,f.jsx)(m.default,{iconId:\"download_error\",onPress:function(){var t=new Error(\"homeui ApplicationErrorEvent test\");e({errorMessage:t.message,stack:t.stack,severity:\"info\"})},title:\"Trigger AppError\"})}));t.ApplicationErrorEventTrigger=b;";
static const char kPlainJsNewAppErrorSourcePrefix[] =
    "var b=(0,a.memo)((function(){var e=(0,d.useInteractivePress)({link:\"OnionHEN?NavUI=1\"});return(0,f.jsx)(m.default,{iconId:{uri:\"/system_ex/vsh_asset/onionhen.png\"},onPress:e,title:\"\"})}));t.ApplicationErrorEventTrigger=b;";

enum class SourceBundleKind {
  LegacyRnps,
  PlainJs,
};

struct SourcePatchStrategy {
  const unsigned char *old_export_alias;
  const unsigned char *new_export_alias;
  size_t export_alias_size;
  const char *old_app_error_source;
  const char *new_app_error_source_prefix;
  size_t source_block_size;
  size_t new_source_prefix_size;
};

struct SourceHomeUiOffsets {
  size_t title_id;
  size_t app_error_event_trigger;
  size_t navigate_to_home;
  size_t icon_order;
  size_t app_error_source;
  size_t export_alias;
};

struct SourceHomeUiProfile {
  const char *name;
  SourceBundleKind kind;
  size_t payload_size;
  SourceHomeUiOffsets offsets;
  const SourcePatchStrategy *strategy;
};

static const SourcePatchStrategy kLegacy2xSourceStrategy = {
    kLegacy2xOldExportAlias,
    kLegacy2xNewExportAlias,
    sizeof(kLegacy2xOldExportAlias) - 1,
    kLegacy2xOldButtonSource,
    kLegacy2xNewButtonSourcePrefix,
    sizeof(kLegacy2xOldButtonSource) - 1,
    sizeof(kLegacy2xNewButtonSourcePrefix) - 1,
};

static const SourcePatchStrategy kLegacy3xSourceStrategy = {
    kLegacy3xOldExportAlias,
    kLegacy3xNewExportAlias,
    sizeof(kLegacy3xOldExportAlias),
    kLegacy3xOldAppErrorSource,
    kLegacy3xNewAppErrorSourcePrefix,
    sizeof(kLegacy3xOldAppErrorSource) - 1,
    sizeof(kLegacy3xNewAppErrorSourcePrefix) - 1,
};

static const SourcePatchStrategy kLegacy4xSourceStrategy = {
    kLegacyOldExportAlias,
    kLegacyNewExportAlias,
    sizeof(kLegacyOldExportAlias),
    kLegacyOldAppErrorSource,
    kLegacyNewAppErrorSourcePrefix,
    sizeof(kLegacyOldAppErrorSource) - 1,
    sizeof(kLegacyNewAppErrorSourcePrefix) - 1,
};

static const SourcePatchStrategy kLegacy5x7xSourceStrategy = {
    kLegacy5x7xOldExportAlias,
    kLegacy5x7xNewExportAlias,
    sizeof(kLegacy5x7xOldExportAlias),
    kLegacy5x7xOldAppErrorSource,
    kLegacy5x7xNewAppErrorSourcePrefix,
    sizeof(kLegacy5x7xOldAppErrorSource) - 1,
    sizeof(kLegacy5x7xNewAppErrorSourcePrefix) - 1,
};

static const SourcePatchStrategy kPlainJsSourceStrategy = {
    kPlainJsOldExportAlias,
    kPlainJsNewExportAlias,
    sizeof(kPlainJsOldExportAlias),
    kPlainJsOldAppErrorSource,
    kPlainJsNewAppErrorSourcePrefix,
    sizeof(kPlainJsOldAppErrorSource) - 1,
    sizeof(kPlainJsNewAppErrorSourcePrefix) - 1,
};

/*
 * 2.x-7.x contain a pre-Hermes RNPS bundle. 8.x contains plain minified JS.
 * The patch operation is identical; only the bundle prefix and source shape
 * differ. Keep offsets together so adding a firmware is a single table edit.
 */
static const SourceHomeUiProfile kSourceHomeUiProfiles[] = {
    {
        /* name */ "2.30/2.50 NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x1631e0,
        /* offsets */ {
            /* title_id */ 0x157069,
            /* app_error_event_trigger */ 0x0,
            /* navigate_to_home */ 0x4b8b4,
            /* icon_order */ 0xc0a75,
            /* app_error_source */ 0x11261c,
            /* export_alias */ 0x11279e,
        },
        /* strategy */ &kLegacy2xSourceStrategy,
    },
    {
        /* name */ "3.00/3.10/3.20/3.21 NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x150130,
        /* offsets */ {
            /* title_id */ 0x6b460,
            /* app_error_event_trigger */ 0xa7fbb,
            /* navigate_to_home */ 0x3ce58,
            /* icon_order */ 0xa7ef4,
            /* app_error_source */ 0x102baf,
            /* export_alias */ 0x102e83,
        },
        /* strategy */ &kLegacy3xSourceStrategy,
    },
    {
        /* name */ "4.00/4.02/4.03/4.50/4.51 NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x152990,
        /* offsets */ {
            /* title_id */ 0x6ae31,
            /* app_error_event_trigger */ 0xa6bb1,
            /* navigate_to_home */ 0x40386,
            /* icon_order */ 0xa6aea,
            /* app_error_source */ 0x1021c9,
            /* export_alias */ 0x10249d,
        },
        /* strategy */ &kLegacy4xSourceStrategy,
    },
    {
        /* name */ "5.00/5.02 NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x17a690,
        /* offsets */ {
            /* title_id */ 0x6b123,
            /* app_error_event_trigger */ 0xae18e,
            /* navigate_to_home */ 0x3b28b,
            /* icon_order */ 0xae0c7,
            /* app_error_source */ 0x119be8,
            /* export_alias */ 0x119edc,
        },
        /* strategy */ &kLegacy5x7xSourceStrategy,
    },
    {
        /* name */ "5.10/5.50 NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x185c30,
        /* offsets */ {
            /* title_id */ 0x6e217,
            /* app_error_event_trigger */ 0xb12de,
            /* navigate_to_home */ 0x3da05,
            /* icon_order */ 0xb1217,
            /* app_error_source */ 0x124d28,
            /* export_alias */ 0x12501c,
        },
        /* strategy */ &kLegacy5x7xSourceStrategy,
    },
    {
        /*
         * 5.50 ShellUI loads the OTA /mnt/rnps bundle, which is a distinct
         * legacy bundle from the /system_ex file above (0x185ad0 vs 0x185c30).
         * Same minified 5x/7x shape, so it reuses kLegacy5x7xSourceStrategy.
         */
        /* name */ "5.50 OTA NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x185ad0,
        /* offsets */ {
            /* title_id */ 0x6e1c0,
            /* app_error_event_trigger */ 0xb1242,
            /* navigate_to_home */ 0x3d9ea,
            /* icon_order */ 0xb117b,
            /* app_error_source */ 0x124c47,
            /* export_alias */ 0x124f3b,
        },
        /* strategy */ &kLegacy5x7xSourceStrategy,
    },
    {
        /* name */ "6.00/6.02 NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x185d00,
        /* offsets */ {
            /* title_id */ 0x6c51d,
            /* app_error_event_trigger */ 0xaece9,
            /* navigate_to_home */ 0x385a9,
            /* icon_order */ 0xaec22,
            /* app_error_source */ 0x11ec20,
            /* export_alias */ 0x11ef14,
        },
        /* strategy */ &kLegacy5x7xSourceStrategy,
    },
    {
        /* name */ "7.40/7.61 NPXS40002 legacy HomeUI",
        /* kind */ SourceBundleKind::LegacyRnps,
        /* payload_size */ 0x19dc10,
        /* offsets */ {
            /* title_id */ 0x75314,
            /* app_error_event_trigger */ 0xbf1a4,
            /* navigate_to_home */ 0x3d8ed,
            /* icon_order */ 0xbf0dd,
            /* app_error_source */ 0x135ed5,
            /* export_alias */ 0x1361c9,
        },
        /* strategy */ &kLegacy5x7xSourceStrategy,
    },
    {
        /* name */ "8.00/8.40 NPXS40002 plain-JS HomeUI",
        /* kind */ SourceBundleKind::PlainJs,
        /* payload_size */ 0x158070,
        /* offsets */ {
            /* title_id */ 0x5a8b3,
            /* app_error_event_trigger */ 0x9e910,
            /* navigate_to_home */ 0x325d1,
            /* icon_order */ 0x9e84b,
            /* app_error_source */ 0x100183,
            /* export_alias */ 0x1003e4,
        },
        /* strategy */ &kPlainJsSourceStrategy,
    },
};

static_assert(sizeof(kLegacyOldIconOrder) == sizeof(kLegacyNewIconOrder));
static_assert(sizeof(kLegacy2xOldIconOrder) ==
              sizeof(kLegacy2xNewIconOrder));
static_assert(sizeof(kLegacy2xOldExportAlias) ==
              sizeof(kLegacy2xNewExportAlias));
static_assert(sizeof(kLegacy3xOldExportAlias) ==
              sizeof(kLegacy3xNewExportAlias));
static_assert(sizeof(kLegacyOldExportAlias) == sizeof(kLegacyNewExportAlias));
static_assert(sizeof(kLegacy5x7xOldExportAlias) ==
              sizeof(kLegacy5x7xNewExportAlias));
static_assert(sizeof(kPlainJsOldExportAlias) ==
              sizeof(kPlainJsNewExportAlias));
static_assert(sizeof(kLegacyNewAppErrorSourcePrefix) <=
              sizeof(kLegacyOldAppErrorSource));
static_assert(sizeof(kLegacy2xNewButtonSourcePrefix) <=
              sizeof(kLegacy2xOldButtonSource));
static_assert(sizeof(kLegacy3xNewAppErrorSourcePrefix) <=
              sizeof(kLegacy3xOldAppErrorSource));
static_assert(sizeof(kLegacy5x7xNewAppErrorSourcePrefix) <=
              sizeof(kLegacy5x7xOldAppErrorSource));
static_assert(sizeof(kPlainJsNewAppErrorSourcePrefix) <=
              sizeof(kPlainJsOldAppErrorSource));

#if SHELL_DEBUG == 1
/* Latches so an unsupported HomeUI is reported once, not every reload. */
std::atomic<bool> g_logged_non_homeui_skip{false};
std::atomic<bool> g_logged_unsupported_homeui_skip{false};
#endif
void (*g_react_button_set_icon_source_orig)(MonoObject *instance,
                                            MonoObject *source) = nullptr;
void (*g_react_button_set_inverted_icon_source)(MonoObject *instance,
                                                MonoObject *source) = nullptr;

struct HbcView {
  unsigned char *data;
  size_t size;
  size_t base_offset;
};

struct SourceBundleView {
  unsigned char *data;
  size_t size;
  size_t base_offset;
};

struct SourceBundleFormat {
  SourceBundleKind kind;
  const unsigned char *prefix;
  size_t prefix_size;
};

static const SourceBundleFormat kSourceBundleFormats[] = {
    {SourceBundleKind::LegacyRnps, kLegacyRnpsBundleMagic,
     sizeof(kLegacyRnpsBundleMagic)},
    {SourceBundleKind::PlainJs, kPlainJsBundlePrefix,
     sizeof(kPlainJsBundlePrefix)},
};

struct BytePatch {
  const char *name;
  size_t offset;
  const unsigned char *expected;
  const unsigned char *alternate_expected;
  const unsigned char *second_alternate_expected;
  const unsigned char *replacement;
  size_t size;
};

/*
 * Patch widths. Every firmware uses the same widths, so making them the array
 * bounds in HomeUiPatchBytes lets the compiler reject a mistyped blob. This
 * replaces the pairwise static_asserts the previous layout needed.
 */
constexpr size_t kIconOrderSize = 9;
constexpr size_t kFpsFactorySize = 5;
constexpr size_t kObjectTableValueSize = 2;
constexpr size_t kButtonBodySize = 77;
constexpr size_t kAppErrorHelperBodySize = 76;

/* Per-firmware byte patterns. Field order matches the patch list below. */
struct HomeUiPatchBytes {
  unsigned char old_icon_order[kIconOrderSize];
  /* Prior strategy: [Search, Fps, Settings, Profile] — migrate away. */
  unsigned char legacy_fps_slot_icon_order[kIconOrderSize];
  unsigned char legacy_aliased_icon_order[kIconOrderSize];
  /* Target: [Search, ApplicationErrorEventTrigger, Settings, Profile]. */
  unsigned char app_error_icon_order[kIconOrderSize];
  unsigned char original_fps_factory[kFpsFactorySize];
  unsigned char legacy_aliased_fps_factory[kFpsFactorySize];
  unsigned char old_custom_icon_value[kObjectTableValueSize];
  unsigned char new_custom_icon_value[kObjectTableValueSize];
  unsigned char old_custom_title_value[kObjectTableValueSize];
  unsigned char old_fps_body_prefix[kButtonBodySize];
  /* Only some firmwares ever shipped an aliased button body. */
  bool has_legacy_button_body;
  unsigned char legacy_onion_hen_button_body[kButtonBodySize];
  unsigned char onion_hen_button_body[kButtonBodySize];
  unsigned char stock_app_error_body[kButtonBodySize];
  /* Older PUI Button.icon requires {uri: path}, not a raw path string. */
  bool requires_image_source_object;
};

struct HomeUiPatchOffsets {
  size_t title_id;
  size_t app_error_event_trigger;
  size_t navigate_to_home;
  size_t home_icon_order;
  size_t fps_factory;
  size_t download_error_string;
  size_t custom_icon_value;
  size_t custom_icon_uri;
  size_t top_nav_link_uri;
  size_t custom_title_value;
  size_t fps_body;
  /* ApplicationErrorEventTrigger function start (77-byte button host). */
  size_t app_error_body;
  /* Dead stock onPress function reused as a props factory when non-zero. */
  size_t app_error_props_helper_body;
};

struct HomeUiPatchProfile {
  const char *name;
  uint32_t hbc_version;
  size_t file_length;
  unsigned char source_hash[kHbcSourceHashSize];
  HomeUiPatchOffsets offsets;
  HomeUiPatchBytes bytes;
};

/* Firmware-independent patch bytes (identical across every profile). */
static const unsigned char kLegacyTopNavPressUri[] = {
    'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N', '?', 'N', 'a', 'v', 'U', 'I'};
static const unsigned char kStockDownloadErrorString[] = {
    'd', 'o', 'w', 'n', 'l', 'o', 'a', 'd', '_', 'e', 'r', 'r', 'o', 'r'};
static const unsigned char kOldCustomIconUri[] = {
    'h', 'o', 'm', 'e', 'u', 'i', ' ', 'A', 'p', 'p', 'l',
    'i', 'c', 'a', 't', 'i', 'o', 'n', 'E', 'r', 'r', 'o',
    'r', 'E', 'v', 'e', 'n', 't', ' ', 't', 'e', 's', 't'};
static const unsigned char kNewCustomIconUri[] = {
    '/', 's', 'y', 's', 't', 'e', 'm', '_', 'e', 'x', '/',
    'v', 's', 'h', '_', 'a', 's', 's', 'e', 't', '/', 'o',
    'n', 'i', 'o', 'n', 'h', 'e', 'n', '.', 'p', 'n', 'g'};
static const unsigned char kOldTopNavLinkUri[] = {
    'T', 'r', 'i', 'g', 'g', 'e', 'r', ' ',
    'A', 'p', 'p', 'E', 'r', 'r', 'o', 'r'};
static const unsigned char kLegacyBlankTopNavLinkUri[] = {
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
static const unsigned char kLegacyPaddedTopNavLinkUri[] = {
    'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N',
    '?', 'N', 'a', 'v', 'U', 'I', ' ', ' '};
static const unsigned char kNewTopNavLinkUri[] = {
    'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N',
    '?', 'N', 'a', 'v', 'U', 'I', '=', '1'};
static const unsigned char kNewCustomTitleValue[] = {0xff, 0x00};

/*
 * Reserved fallback for a firmware whose PUI requires {uri: path} instead of
 * a raw icon path. Reusing the adjacent AppError onPress function changes a
 * second executable function and must only be enabled after device validation.
 */
static const unsigned char kImageSourceOnionHenButtonBody[] = {
    0x29, 0x00, 0x00, 0x2e, 0x01, 0x00, 0x09, 0x34, 0x02, 0x01, 0x02,
    0x9d, 0x2e, 0x01, 0x00, 0x08, 0x34, 0x01, 0x01, 0x01, 0x6f, 0x62,
    0x04, 0x00, 0x9a, 0x18, 0x74, 0x03, 0x4f, 0x00, 0x04, 0x03, 0x52,
    0x00, 0x02, 0x03, 0x01, 0x00, 0x5a, 0x00, 0x60, 0x74, 0x00, 0x74,
    0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00,
    0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74,
    0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00};
static const unsigned char kStockAppErrorOnPressBody[] = {
    0x30, 0x00, 0x37, 0x02, 0x00, 0x01, 0x11, 0x00, 0x34, 0x00, 0x02,
    0x02, 0xc8, 0x68, 0x01, 0x00, 0x02, 0x71, 0x04, 0x43, 0x0d, 0x08,
    0x05, 0x01, 0x4e, 0x00, 0x02, 0x02, 0x69, 0x00, 0x01, 0x00, 0x29,
    0x01, 0x00, 0x2e, 0x02, 0x01, 0x00, 0x34, 0x03, 0x00, 0x03, 0x2d,
    0x34, 0x00, 0x00, 0x04, 0xde, 0x03, 0x01, 0x3d, 0x01, 0x03, 0x03,
    0x3d, 0x01, 0x00, 0xde, 0x71, 0x00, 0x9d, 0x12, 0x3d, 0x01, 0x00,
    0xd9, 0x74, 0x00, 0x51, 0x01, 0x02, 0x00, 0x01, 0x5a, 0x00};
static const unsigned char kImageSourceIconPropsHelperBody[] = {
    0x29, 0x00, 0x00, 0x2e, 0x01, 0x00, 0x05, 0x35, 0x04, 0x01, 0x01,
    0x88, 0x1d, 0x03, 0x01, 0x71, 0x02, 0x7c, 0x09, 0x3e, 0x01, 0x02,
    0xdf, 0x14, 0x74, 0x03, 0x51, 0x05, 0x04, 0x03, 0x01, 0x03, 0x01,
    0x03, 0x02, 0x71, 0x03, 0x43, 0x0d, 0x3d, 0x02, 0x03, 0xf1, 0x3d,
    0x01, 0x02, 0x95, 0x3d, 0x01, 0x05, 0xb9, 0x71, 0x02, 0xff, 0x00,
    0x3d, 0x01, 0x02, 0x2b, 0x5a, 0x01, 0x60, 0x74, 0x00, 0x74, 0x00,
    0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00, 0x74, 0x00};

static_assert(sizeof(kImageSourceOnionHenButtonBody) == kButtonBodySize);
static_assert(sizeof(kStockAppErrorOnPressBody) == kAppErrorHelperBodySize);
static_assert(sizeof(kImageSourceIconPropsHelperBody) ==
              kAppErrorHelperBodySize);

#include "homeui_top_nav_profiles.inc"

static bool range_contains(size_t size, size_t offset, size_t len) {
  return offset <= size && len <= size - offset;
}

static bool bytes_equal(const unsigned char *lhs, const unsigned char *rhs,
                        size_t len) {
  return memcmp(lhs, rhs, len) == 0;
}

static uint32_t read_u32le(const unsigned char *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool has_magic(const unsigned char *data, size_t size,
                      const unsigned char *magic, size_t magic_size) {
  return range_contains(size, 0, magic_size) &&
         bytes_equal(data, magic, magic_size);
}

static bool hbc_at(unsigned char *buffer, size_t available, size_t offset,
                   HbcView *out) {
  if (!range_contains(available, offset, sizeof(kHermesMagic)) ||
      !has_magic(buffer + offset, available - offset, kHermesMagic,
                 sizeof(kHermesMagic))) {
    return false;
  }

  out->data = buffer + offset;
  out->size = available - offset;
  out->base_offset = offset;
  return true;
}

static bool locate_hbc(unsigned char *buffer, size_t visible_size,
                       size_t capacity, HbcView *out) {
  const size_t available = capacity > visible_size ? capacity : visible_size;

  if (hbc_at(buffer, available, 0, out)) {
    return true;
  }

  if (!has_magic(buffer, available, kRnpsMagic, sizeof(kRnpsMagic))) {
    return false;
  }

  size_t rnps_payload_offset = kRnpsFallbackPayloadOffset;
  if (range_contains(available, kRnpsPayloadOffsetField, sizeof(uint32_t))) {
    const uint32_t declared_offset =
        read_u32le(buffer + kRnpsPayloadOffsetField);
    if (declared_offset > 0 && declared_offset < available) {
      rnps_payload_offset = declared_offset;
    }
  }

  if (hbc_at(buffer, available, rnps_payload_offset, out)) {
    return true;
  }

  if (rnps_payload_offset != kRnpsFallbackPayloadOffset &&
      hbc_at(buffer, available, kRnpsFallbackPayloadOffset, out)) {
    return true;
  }

  return false;
}

static bool source_bundle_at(unsigned char *buffer, size_t available,
                             size_t offset, const SourceBundleFormat &format,
                             SourceBundleView *out) {
  if (!range_contains(available, offset, format.prefix_size) ||
      !has_magic(buffer + offset, available - offset, format.prefix,
                 format.prefix_size)) {
    return false;
  }

  out->data = buffer + offset;
  out->size = available - offset;
  out->base_offset = offset;
  return true;
}

static bool locate_source_bundle(unsigned char *buffer, size_t visible_size,
                                 size_t capacity,
                                 const SourceBundleFormat &format,
                                 SourceBundleView *out) {
  const size_t available = capacity > visible_size ? capacity : visible_size;
  if (source_bundle_at(buffer, available, 0, format, out)) {
    return true;
  }
  if (!has_magic(buffer, available, kRnpsMagic, sizeof(kRnpsMagic))) {
    return false;
  }

  size_t payload_offset = kRnpsFallbackPayloadOffset;
  if (range_contains(available, kRnpsPayloadOffsetField, sizeof(uint32_t))) {
    const uint32_t declared_offset =
        read_u32le(buffer + kRnpsPayloadOffsetField);
    if (declared_offset > 0 && declared_offset < available) {
      payload_offset = declared_offset;
    }
  }

  if (source_bundle_at(buffer, available, payload_offset, format, out)) {
    return true;
  }
  return payload_offset != kRnpsFallbackPayloadOffset &&
         source_bundle_at(buffer, available, kRnpsFallbackPayloadOffset, format,
                          out);
}

static bool source_bytes_at(const SourceBundleView &bundle, size_t offset,
                            const unsigned char *expected, size_t len) {
  return range_contains(bundle.size, offset, len) &&
         bytes_equal(bundle.data + offset, expected, len);
}

static bool source_is_target(const SourceBundleView &bundle,
                             const SourceHomeUiProfile &profile) {
  const SourcePatchStrategy &strategy = *profile.strategy;
  const size_t source_offset = profile.offsets.app_error_source;
  if (!range_contains(bundle.size, source_offset,
                      strategy.source_block_size) ||
      !source_bytes_at(
          bundle, source_offset,
          reinterpret_cast<const unsigned char *>(
              strategy.new_app_error_source_prefix),
          strategy.new_source_prefix_size)) {
    return false;
  }

  const unsigned char *source = bundle.data + source_offset;
  for (size_t i = strategy.new_source_prefix_size;
       i < strategy.source_block_size; ++i) {
    if (source[i] != ' ') {
      return false;
    }
  }
  return true;
}

static bool validate_source(const SourceBundleView &bundle,
                            const SourceHomeUiProfile &profile,
                            bool *already_applied) {
  const SourcePatchStrategy &strategy = *profile.strategy;
  const size_t source_offset = profile.offsets.app_error_source;
  if (!range_contains(bundle.size, source_offset,
                      strategy.source_block_size)) {
#if SHELL_DEBUG == 1
    LOG_DEBUG("homeui_top_nav_patch: source AppError block out of range");
#endif
    return false;
  }

  if (source_is_target(bundle, profile)) {
    *already_applied = true;
    return true;
  }

  if (!source_bytes_at(
          bundle, source_offset,
          reinterpret_cast<const unsigned char *>(strategy.old_app_error_source),
          strategy.source_block_size)) {
#if SHELL_DEBUG == 1
    LOG_WARN("homeui_top_nav_patch: source AppError mismatch for '%s'; "
             "skip",
             profile.name);
#endif
    return false;
  }

  *already_applied = false;
  return true;
}

static void apply_source(const SourceBundleView &bundle,
                         const SourceHomeUiProfile &profile) {
  const SourcePatchStrategy &strategy = *profile.strategy;
  unsigned char *target = bundle.data + profile.offsets.app_error_source;
  memcpy(target, strategy.new_app_error_source_prefix,
         strategy.new_source_prefix_size);
  memset(target + strategy.new_source_prefix_size, ' ',
         strategy.source_block_size - strategy.new_source_prefix_size);
}

static bool read_hbc_file_length(const HbcView &hbc, size_t *file_length) {
  if (!range_contains(hbc.size, kHbcFileLengthOffset, sizeof(uint32_t))) {
    return false;
  }

  const uint32_t declared_file_length =
      read_u32le(hbc.data + kHbcFileLengthOffset);
  if (declared_file_length <= kHbcFooterSha1Size ||
      declared_file_length > hbc.size) {
    return false;
  }

  *file_length = declared_file_length;
  return true;
}

static bool read_hbc_version(const HbcView &hbc, uint32_t *version) {
  if (!range_contains(hbc.size, kHbcVersionOffset, sizeof(uint32_t))) {
    return false;
  }

  *version = read_u32le(hbc.data + kHbcVersionOffset);
  return true;
}

static bool hbc_bytes_at(const HbcView &hbc, size_t offset,
                         const char *expected) {
  const size_t len = strlen(expected);
  return range_contains(hbc.size, offset, len) &&
         bytes_equal(hbc.data + offset,
                     reinterpret_cast<const unsigned char *>(expected), len);
}

static bool hbc_source_hash_matches(const HbcView &hbc,
                                    const unsigned char *source_hash) {
  return range_contains(hbc.size, kHbcSourceHashOffset, kHbcSourceHashSize) &&
         bytes_equal(hbc.data + kHbcSourceHashOffset, source_hash,
                     kHbcSourceHashSize);
}

#if SHELL_DEBUG == 1
static void format_hbc_source_hash(const HbcView &hbc, char *out,
                                   size_t out_size) {
  static const char kHex[] = "0123456789abcdef";
  if (!out || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (out_size < kHbcSourceHashSize * 2 + 1 ||
      !range_contains(hbc.size, kHbcSourceHashOffset, kHbcSourceHashSize)) {
    return;
  }

  const unsigned char *hash = hbc.data + kHbcSourceHashOffset;
  for (size_t i = 0; i < kHbcSourceHashSize; ++i) {
    out[i * 2] = kHex[hash[i] >> 4];
    out[i * 2 + 1] = kHex[hash[i] & 0x0f];
  }
  out[kHbcSourceHashSize * 2] = '\0';
}
#endif

static bool validate_homeui_profile_markers(const HbcView &hbc,
                                            const HomeUiPatchProfile &profile) {
  return hbc_bytes_at(hbc, profile.offsets.title_id, "NPXS40002") &&
         hbc_bytes_at(hbc, profile.offsets.app_error_event_trigger,
                      "ApplicationErrorEventTrigger") &&
         hbc_bytes_at(hbc, profile.offsets.navigate_to_home,
                      "pshomeui:navigateToHome");
}

static const HomeUiPatchProfile *
find_homeui_patch_profile(const HbcView &hbc, size_t file_length,
                          uint32_t hbc_version) {
  for (size_t i = 0;
       i < sizeof(kHomeUiPatchProfiles) / sizeof(kHomeUiPatchProfiles[0]);
       ++i) {
    const HomeUiPatchProfile &profile = kHomeUiPatchProfiles[i];
    if (file_length == profile.file_length &&
        hbc_version == profile.hbc_version &&
        hbc_source_hash_matches(hbc, profile.source_hash) &&
        validate_homeui_profile_markers(hbc, profile)) {
      return &profile;
    }
  }

  return nullptr;
}

#if SHELL_DEBUG == 1
static bool looks_like_profiled_homeui(const HbcView &hbc) {
  for (size_t i = 0;
       i < sizeof(kHomeUiPatchProfiles) / sizeof(kHomeUiPatchProfiles[0]);
       ++i) {
    const HomeUiPatchProfile &profile = kHomeUiPatchProfiles[i];
    if (validate_homeui_profile_markers(hbc, profile)) {
      return true;
    }
  }

  return false;
}
#endif

static bool validate_patch(const HbcView &hbc, const BytePatch &patch,
                           bool *already_applied) {
  if (!range_contains(hbc.size, patch.offset, patch.size)) {
#if SHELL_DEBUG == 1
    LOG_DEBUG("homeui_top_nav_patch: %s out of range at hbc+0x%llx",
                patch.name, (unsigned long long)patch.offset);
#endif
    return false;
  }

  const unsigned char *current = hbc.data + patch.offset;
  if (bytes_equal(current, patch.replacement, patch.size)) {
    *already_applied = true;
    return true;
  }

  if (bytes_equal(current, patch.expected, patch.size) ||
      (patch.alternate_expected &&
       bytes_equal(current, patch.alternate_expected, patch.size)) ||
      (patch.second_alternate_expected &&
       bytes_equal(current, patch.second_alternate_expected, patch.size))) {
    *already_applied = false;
    return true;
  }

#if SHELL_DEBUG == 1
  LOG_WARN("homeui_top_nav_patch: %s mismatch at hbc+0x%llx; skip",
              patch.name, (unsigned long long)patch.offset);
#endif
  return false;
}

static void apply_patch(const HbcView &hbc, const BytePatch &patch) {
  const unsigned char *current = hbc.data + patch.offset;
  if (!bytes_equal(current, patch.replacement, patch.size)) {
    memcpy(hbc.data + patch.offset, patch.replacement, patch.size);
  }
}

static bool validate_patch(const SourceBundleView &bundle,
                           const BytePatch &patch, bool *already_applied) {
  if (!range_contains(bundle.size, patch.offset, patch.size)) {
#if SHELL_DEBUG == 1
    LOG_DEBUG("homeui_top_nav_patch: source %s out of range at payload+0x%llx",
              patch.name, (unsigned long long)patch.offset);
#endif
    return false;
  }

  const unsigned char *current = bundle.data + patch.offset;
  if (bytes_equal(current, patch.replacement, patch.size)) {
    *already_applied = true;
    return true;
  }

  if (bytes_equal(current, patch.expected, patch.size) ||
      (patch.alternate_expected &&
       bytes_equal(current, patch.alternate_expected, patch.size)) ||
      (patch.second_alternate_expected &&
       bytes_equal(current, patch.second_alternate_expected, patch.size))) {
    *already_applied = false;
    return true;
  }

#if SHELL_DEBUG == 1
  LOG_WARN("homeui_top_nav_patch: source %s mismatch at payload+0x%llx; skip",
           patch.name, (unsigned long long)patch.offset);
#endif
  return false;
}

static void apply_patch(const SourceBundleView &bundle,
                        const BytePatch &patch) {
  const unsigned char *current = bundle.data + patch.offset;
  if (!bytes_equal(current, patch.replacement, patch.size)) {
    memcpy(bundle.data + patch.offset, patch.replacement, patch.size);
  }
}

static void update_hbc_footer_sha1(HbcView &hbc, size_t file_length) {
  const size_t footer_offset = file_length - kHbcFooterSha1Size;
  SHA1_CTX ctx;

  SHA1Init(&ctx);
  SHA1Update(&ctx, hbc.data, (uint32_t)footer_offset);
  SHA1Final(hbc.data + footer_offset, &ctx);
}

/*
 * Focused top-nav buttons use invertedIconSource. Named system icons (Search,
 * Settings) resolve both states from iconId; a custom file URI only fills the
 * normal source unless we mirror it into invertedIcon.
 *
 * Crash history was from Fps body hijack on game-close remount, not from this
 * mirror itself. Still harden: path filter, exception-safe ToString, re-entry
 * guard (SetinvertedIconSource must not re-enter SetIconSource).
 */
static bool is_homeui_top_nav_icon_source(MonoObject *source) {
  if (!source || !mono_object_to_string) {
    return false;
  }

  MonoObject *exception = nullptr;
  MonoString *text = mono_object_to_string(source, &exception);
  if (exception || !text) {
    return false;
  }

  return Mono_to_String(text).find(kOnionHenTopNavIconPath) !=
         std::string::npos;
}

static void ReactButtonShadowNode_SetIconSource_Hook(MonoObject *instance,
                                                     MonoObject *source) {
  thread_local int depth = 0;

  if (g_react_button_set_icon_source_orig) {
    g_react_button_set_icon_source_orig(instance, source);
  }

  if (depth > 0 || !shellui_hooks_are_ready() || !instance || !source ||
      !g_react_button_set_inverted_icon_source ||
      !is_homeui_top_nav_icon_source(source)) {
    return;
  }

  ++depth;
  g_react_button_set_inverted_icon_source(instance, source);
  --depth;
#if SHELL_DEBUG == 1
  LOG_DEBUG("homeui_top_nav_patch: mirrored OnionHEN icon to invertedIcon");
#endif
}

static const SourceHomeUiProfile *
find_source_homeui_profile(const SourceBundleView &bundle,
                           SourceBundleKind kind) {
  for (const SourceHomeUiProfile &profile : kSourceHomeUiProfiles) {
    const SourceHomeUiOffsets &offsets = profile.offsets;
    if (profile.kind != kind || bundle.size != profile.payload_size ||
        !source_bytes_at(
            bundle, offsets.title_id,
            reinterpret_cast<const unsigned char *>("NPXS40002"),
            sizeof("NPXS40002") - 1) ||
        (offsets.app_error_event_trigger != 0 &&
         !source_bytes_at(
             bundle, offsets.app_error_event_trigger,
             reinterpret_cast<const unsigned char *>(
                 "ApplicationErrorEventTrigger"),
             sizeof("ApplicationErrorEventTrigger") - 1)) ||
        !source_bytes_at(
            bundle, offsets.navigate_to_home,
            reinterpret_cast<const unsigned char *>("pshomeui:navigateToHome"),
            sizeof("pshomeui:navigateToHome") - 1)) {
      continue;
    }
    return &profile;
  }
  return nullptr;
}

static bool patch_source_homeui_top_nav(const SourceBundleView &bundle,
                                        SourceBundleKind kind) {
  const SourceHomeUiProfile *profile =
      find_source_homeui_profile(bundle, kind);
  if (!profile) {
#if SHELL_DEBUG == 1
    LOG_WARN("homeui_top_nav_patch: skip unknown source HomeUI");
#endif
    return false;
  }

  const SourcePatchStrategy &strategy = *profile->strategy;
  const bool uses_legacy2x_layout =
      profile->strategy == &kLegacy2xSourceStrategy;
  const unsigned char *old_icon_order =
      uses_legacy2x_layout ? kLegacy2xOldIconOrder : kLegacyOldIconOrder;
  const unsigned char *new_icon_order =
      uses_legacy2x_layout ? kLegacy2xNewIconOrder : kLegacyNewIconOrder;
  const size_t icon_order_size =
      uses_legacy2x_layout ? sizeof(kLegacy2xOldIconOrder) - 1
                           : sizeof(kLegacyOldIconOrder);
  const BytePatch patches[] = {
      {"source home icon order", profile->offsets.icon_order,
       old_icon_order, nullptr, nullptr, new_icon_order, icon_order_size},
      {"source App export alias", profile->offsets.export_alias,
       strategy.old_export_alias, nullptr, nullptr, strategy.new_export_alias,
       strategy.export_alias_size},
  };

  bool source_already_applied = false;
  if (!validate_source(bundle, *profile, &source_already_applied)) {
    return false;
  }

  bool any_change = !source_already_applied;
  for (const BytePatch &patch : patches) {
    bool already_applied = false;
    if (!validate_patch(bundle, patch, &already_applied)) {
      return false;
    }
    any_change = any_change || !already_applied;
  }

  if (!any_change) {
#if SHELL_DEBUG == 1
    LOG_WARN("homeui_top_nav_patch: source HomeUI already applied "
             "(rnps_base=0x%llx)",
             (unsigned long long)bundle.base_offset);
#endif
    return true;
  }

  for (const BytePatch &patch : patches) {
    apply_patch(bundle, patch);
  }
  apply_source(bundle, *profile);

#if SHELL_DEBUG == 1
  LOG_DEBUG("homeui_top_nav_patch: activated '%s' (rnps_base=0x%llx)",
            profile->name, (unsigned long long)bundle.base_offset);
#endif
  return true;
}

} // namespace

#endif /* SHELLUI_HOMEUI_TOP_NAV_PATCH == 1 */

void install_homeui_top_nav_hooks(MonoImage *react_pui) {
#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1
  if (!react_pui) {
    LOG_ERROR("homeui_top_nav_patch: ReactNative.PUI image missing");
    return;
  }

  g_react_button_set_inverted_icon_source =
      reinterpret_cast<void (*)(MonoObject *, MonoObject *)>(
          Get_Address_of_Method(react_pui, "ReactNative.Views.UI3.View",
                                "ReactButtonShadowNode",
                                "SetinvertedIconSource", 1));
  if (!g_react_button_set_inverted_icon_source) {
    LOG_ERROR("homeui_top_nav_patch: SetinvertedIconSource missing; "
                "focused icon may be blank");
  }

  const uint64_t set_icon_source =
      Get_Address_of_Method(react_pui, "ReactNative.Views.UI3.View",
                            "ReactButtonShadowNode", "SetIconSource", 1);
  if (!set_icon_source) {
    LOG_ERROR("homeui_top_nav_patch: SetIconSource missing");
    return;
  }

  const bool installed = InstallDetour(
      set_icon_source,
      reinterpret_cast<void *>(&ReactButtonShadowNode_SetIconSource_Hook),
      reinterpret_cast<void **>(&g_react_button_set_icon_source_orig));
  LOG_DEBUG(installed ? "homeui_top_nav_patch: SetIconSource hooked "
                          "(invertedIcon mirror for OnionHEN)"
                        : "homeui_top_nav_patch: SetIconSource detour failed");
#else
  (void)react_pui;
#endif
}

void patch_homeui_top_nav(unsigned char *buffer, int *size_ptr,
                          int buffer_capacity) {
#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1
  if (!buffer || !size_ptr || *size_ptr <= 0) {
    return;
  }

  HbcView hbc = {};
  const size_t visible_size = (size_t)*size_ptr;
  const size_t capacity =
      buffer_capacity > *size_ptr ? (size_t)buffer_capacity : visible_size;

  for (const SourceBundleFormat &format : kSourceBundleFormats) {
    SourceBundleView source_bundle = {};
    if (locate_source_bundle(buffer, visible_size, capacity, format,
                             &source_bundle)) {
      patch_source_homeui_top_nav(source_bundle, format.kind);
      return;
    }
  }

  if (!locate_hbc(buffer, visible_size, capacity, &hbc)) {
#if SHELL_DEBUG == 1
    const unsigned char b0 = visible_size > 0 ? buffer[0] : 0;
    const unsigned char b1 = visible_size > 1 ? buffer[1] : 0;
    const unsigned char b2 = visible_size > 2 ? buffer[2] : 0;
    const unsigned char b3 = visible_size > 3 ? buffer[3] : 0;
    LOG_DEBUG("homeui_top_nav_patch: no HBC candidate (size=%d capacity=%d "
                "head=%02x %02x %02x %02x)",
                *size_ptr, buffer_capacity, b0, b1, b2, b3);
#endif
    return;
  }

  size_t hbc_file_length = 0;
  if (!read_hbc_file_length(hbc, &hbc_file_length)) {
#if SHELL_DEBUG == 1
    LOG_ERROR("homeui_top_nav_patch: invalid HBC length at base+0x%llx",
                (unsigned long long)hbc.base_offset);
#endif
    return;
  }

  uint32_t hbc_version = 0;
  if (!read_hbc_version(hbc, &hbc_version)) {
#if SHELL_DEBUG == 1
    LOG_ERROR("homeui_top_nav_patch: invalid HBC version at base+0x%llx",
                (unsigned long long)hbc.base_offset);
#endif
    return;
  }

  const HomeUiPatchProfile *profile =
      find_homeui_patch_profile(hbc, hbc_file_length, hbc_version);
  if (!profile) {
#if SHELL_DEBUG == 1
    if (looks_like_profiled_homeui(hbc)) {
      if (!g_logged_unsupported_homeui_skip.exchange(
              true, std::memory_order_relaxed)) {
        char source_hash[kHbcSourceHashSize * 2 + 1];
        format_hbc_source_hash(hbc, source_hash, sizeof(source_hash));
        LOG_WARN("homeui_top_nav_patch: unsupported HomeUI HBC "
                    "(hbc_base=0x%llx version=%u file_length=0x%llx "
                    "source_hash=%s)",
                    (unsigned long long)hbc.base_offset, hbc_version,
                    (unsigned long long)hbc_file_length, source_hash);
      }
    } else if (!g_logged_non_homeui_skip.exchange(true,
                                                   std::memory_order_relaxed)) {
      LOG_WARN("homeui_top_nav_patch: skip non-HomeUI HBC "
                  "(hbc_base=0x%llx version=%u file_length=0x%llx)",
                  (unsigned long long)hbc.base_offset, hbc_version,
                  (unsigned long long)hbc_file_length);
    }
#endif
    return;
  }

#if SHELL_DEBUG == 1
  LOG_DEBUG("homeui_top_nav_patch: matched profile '%s' hbc_base=0x%llx "
              "version=%u hbc_file_length=0x%llx size=%d capacity=%d",
              profile->name,
              (unsigned long long)hbc.base_offset,
              hbc_version, (unsigned long long)hbc_file_length, *size_ptr,
              buffer_capacity);
#endif

  /*
   * Root cause of ShellUI SIGSEGV on game close (SceRnJs-rnps-home):
   *
   * Earlier builds rewrote the Fps *function body* (131-byte showFps debug
   * component) into a useInteractivePress button and put Fps in the top-nav
   * array. Fps remounts on BIG_APP → home focus restore and crashed the RN JS
   * executor.
   *
   * Safe design (keeps the toolbox top-nav entry):
   *
   *   top-nav order: [Search, ApplicationErrorEventTrigger, Settings, Profile]
   *   host function: ApplicationErrorEventTrigger (already a 77-byte button)
   *   body:          full 77-byte useInteractivePress OnionHEN button
   *   Fps:           restore/leave stock showFps implementation
   *   focus icon:    SetIconSource hook mirrors onionhen.png → invertedIcon
   *
   * Object table still retargets:
   *   iconId string → /system_ex/vsh_asset/onionhen.png
   *   title id      → empty
   *   Trigger AppError string slot → OnionHEN?NavUI=1 (hook_boot → toolbox)
   *
   * Accept prior in-memory shapes (Fps-in-array + Fps body rewrite, factory
   * alias) and repair them toward this layout.
   */

  const HomeUiPatchBytes &bytes = profile->bytes;
  const unsigned char *legacy_button_body =
      bytes.has_legacy_button_body ? bytes.legacy_onion_hen_button_body
                                   : nullptr;
  const unsigned char *onion_hen_button_body =
      bytes.requires_image_source_object ? kImageSourceOnionHenButtonBody
                                         : bytes.onion_hen_button_body;

  const BytePatch kPatches[] = {
      /*
       * Order: stock / prior Fps-slot strategy / aliased → AppError slot.
       * replacement is ApplicationErrorEventTrigger between Search & Settings.
       */
      {"home icon order", profile->offsets.home_icon_order,
       bytes.old_icon_order, bytes.legacy_fps_slot_icon_order,
       bytes.legacy_aliased_icon_order, bytes.app_error_icon_order,
       kIconOrderSize},
      {"legacy Fps factory repair", profile->offsets.fps_factory,
       bytes.legacy_aliased_fps_factory, nullptr, nullptr,
       bytes.original_fps_factory, kFpsFactorySize},
      {"download_error string repair", profile->offsets.download_error_string,
       kLegacyTopNavPressUri, nullptr, nullptr, kStockDownloadErrorString,
       sizeof(kStockDownloadErrorString)},
      {"custom icon value", profile->offsets.custom_icon_value,
       bytes.old_custom_icon_value, nullptr, nullptr,
       bytes.new_custom_icon_value, kObjectTableValueSize},
      {"custom icon uri", profile->offsets.custom_icon_uri, kOldCustomIconUri,
       nullptr, nullptr, kNewCustomIconUri, sizeof(kOldCustomIconUri)},
      {"top-nav link uri", profile->offsets.top_nav_link_uri,
       kOldTopNavLinkUri, kLegacyBlankTopNavLinkUri,
       kLegacyPaddedTopNavLinkUri,
       kNewTopNavLinkUri, sizeof(kOldTopNavLinkUri)},
      {"custom title value", profile->offsets.custom_title_value,
       bytes.old_custom_title_value, nullptr, nullptr, kNewCustomTitleValue,
       kObjectTableValueSize},
      /*
       * Repair prior Fps-body hijack back to stock showFps (prefix only).
       * expected = onion body still sitting on Fps; replacement = stock Fps.
       */
      {"Fps body repair", profile->offsets.fps_body,
       bytes.onion_hen_button_body, legacy_button_body,
       nullptr, bytes.old_fps_body_prefix, kButtonBodySize},
      /*
       * Host OnionHEN on ApplicationErrorEventTrigger (exact 77-byte replace).
       */
      {"AppError OnionHEN body", profile->offsets.app_error_body,
       bytes.stock_app_error_body, bytes.onion_hen_button_body,
       legacy_button_body, onion_hen_button_body,
       kButtonBodySize},
  };

  const BytePatch kImageSourceHelperPatch = {
      "AppError ImageSource props helper",
      profile->offsets.app_error_props_helper_body,
      kStockAppErrorOnPressBody,
      nullptr,
      nullptr,
      kImageSourceIconPropsHelperBody,
      kAppErrorHelperBodySize,
  };

  bool any_change = false;
  for (size_t i = 0; i < sizeof(kPatches) / sizeof(kPatches[0]); ++i) {
    bool already_applied = false;
    if (!validate_patch(hbc, kPatches[i], &already_applied)) {
      return;
    }
    any_change = any_change || !already_applied;
  }

  if (bytes.requires_image_source_object) {
    bool helper_already_applied = false;
    if (profile->offsets.app_error_props_helper_body == 0 ||
        !validate_patch(hbc, kImageSourceHelperPatch,
                        &helper_already_applied)) {
      return;
    }
    any_change = any_change || !helper_already_applied;
  }

  if (!any_change) {
#if SHELL_DEBUG == 1
    LOG_WARN("homeui_top_nav_patch: already applied profile='%s' "
                "(hbc_base=0x%llx)",
                profile->name, (unsigned long long)hbc.base_offset);
#endif
    return;
  }

  for (size_t i = 0; i < sizeof(kPatches) / sizeof(kPatches[0]); ++i) {
    apply_patch(hbc, kPatches[i]);
  }
  if (bytes.requires_image_source_object) {
    apply_patch(hbc, kImageSourceHelperPatch);
  }
  update_hbc_footer_sha1(hbc, hbc_file_length);

#if SHELL_DEBUG == 1
  LOG_DEBUG("homeui_top_nav_patch: activated OnionHEN top-nav slot "
              "profile='%s' "
              "(hbc_base=0x%llx)",
              profile->name, (unsigned long long)hbc.base_offset);
#endif
#else
  (void)buffer;
  (void)size_ptr;
  (void)buffer_capacity;
#endif
}
