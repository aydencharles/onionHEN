/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details. */

#include "settings_bundle_patch.hpp"

#include "defs.h"
#include <onion/debug_settings_route_policy.hpp>
#include <onion/platform.h>
#include <sha1.hpp>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

constexpr unsigned char kHermesMagic[] = {0xc6, 0x1f, 0xbc, 0x03,
                                          0xc1, 0x03, 0x19, 0x1f};
constexpr unsigned char kLegacyBundleMagic[] = {0xe5, 0xd1, 0x0b, 0xfb};
constexpr unsigned char kRnpsMagic[] = {'R', 'N', 'P', 'S',
                                        'H', 'E', 'D', 'R'};
constexpr size_t kRnpsPayloadOffsetField = 0x1c;
constexpr size_t kRnpsFallbackPayloadOffset = 0xb20;
constexpr size_t kLegacyAlternatePayloadOffset = 0xb30;
constexpr size_t kHbcSourceHashOffset = 0x0c;
constexpr size_t kHbcFileLengthOffset = 0x20;
constexpr size_t kHbcFooterSha1Size = 20;
constexpr size_t kMaxHermesHeaderScan = 0x2000;

struct BundleView {
  unsigned char *data;
  size_t size;
};

struct LegacySettingsProfile {
  const char *name;
  size_t payload_size;
  size_t label_offset;
  size_t icon_offset;
};

static constexpr LegacySettingsProfile kLegacySettingsProfiles[] = {
    /* 3.00, 3.10, 3.20 and 3.21 NPXS40008 are byte-identical. */
    {"3.00/3.10/3.20/3.21 NPXS40008 Settings", 0x457210, 0x21142c,
     0x2379d6},
    /* 4.00, 4.02 and 4.03 NPXS40008 are byte-identical. */
    {"4.00/4.02/4.03 NPXS40008 Settings", 0x483280, 0x234a17,
     0x24db26},
    /* 4.50 and 4.51 NPXS40008 are byte-identical. */
    {"4.50/4.51 NPXS40008 Settings", 0x483fc0, 0x234f2d, 0x24e03c},
    /* 5.00 and 5.02 NPXS40008 are byte-identical. */
    {"5.00/5.02 NPXS40008 Settings", 0x4b8770, 0x242d07, 0x25c1dc},
    {"5.10 NPXS40008 Settings", 0x4b8ac0, 0x2436c2, 0x25cb97},
    {"5.50 NPXS40008 Settings", 0x4b9e50, 0x243cfa, 0x25d1cf},
    /* 6.00 and 6.02 NPXS40008 are byte-identical. */
    {"6.00/6.02 NPXS40008 Settings", 0x5524a0, 0x27f5dc, 0x299152},
    {"6.50 NPXS40008 Settings", 0x555280, 0x280ac0, 0x29a636},
    /* 7.00, 7.01 and 7.01.01 NPXS40008 are byte-identical. */
    {"7.00/7.01/7.01.01 NPXS40008 Settings", 0x5e7540, 0x2babdf,
     0x2d5634},
    {"7.20 NPXS40008 Settings", 0x5e7940, 0x2babdf, 0x2d5634},
    /* 7.40 and 7.61 NPXS40008 are byte-identical. */
    {"7.40/7.61 NPXS40008 Settings", 0x5e9d20, 0x2bac8c, 0x2d56e1},
    {"8.00 NPXS40008 Settings", 0x64bb80, 0x2e75c9, 0x302a6f},
    {"8.40 NPXS40008 Settings", 0x654af0, 0x2e62fd, 0x3017a3},
    {"8.60 NPXS40008 Settings", 0x6561e0, 0x2e6e72, 0x302318},
};

static constexpr unsigned char kLegacyOldLabel[] = {
    0xe2, 0x98, 0x85, 'D', 'e', 'b', 'u', 'g', ' ', 'S', 'e', 't', 't', 'i',
    'n', 'g', 's'};
static constexpr unsigned char kLegacyNewLabel[] = {
    0xe2, 0x98, 0x85, 'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N', ' ', 'T', 'o',
    'o', 'l', 's'};
static constexpr unsigned char kLegacyOldIcon[] = {
    'i', 'c', 'o', 'n', '_', 's', 'e', 't', 't', 'i', 'n', 'g'};

/* Both Hermes labels contain 15 UTF-16LE code units. */
static constexpr unsigned char kHermesOldLabel[] = {
    0x05, 0x26, 'D', 0x00, 'e', 0x00, 'b', 0x00, 'u', 0x00,
    'g',  0x00, ' ', 0x00, 'S', 0x00, 'e', 0x00, 't', 0x00,
    't',  0x00, 'i', 0x00, 'n', 0x00, 'g', 0x00, 's', 0x00,
};
static constexpr unsigned char kHermesNewLabel[] = {
    0x05, 0x26, 'O', 0x00, 'n', 0x00, 'i', 0x00, 'o', 0x00,
    'n',  0x00, 'H', 0x00, 'E', 0x00, 'N', 0x00, ' ', 0x00,
    'T',  0x00, 'o', 0x00, 'o', 0x00, 'l', 0x00, 's', 0x00,
};

static_assert(sizeof(kLegacyOldLabel) == sizeof(kLegacyNewLabel));
static_assert(sizeof(kHermesOldLabel) == sizeof(kHermesNewLabel));

static bool range_contains(size_t size, size_t offset, size_t length) {
  return offset <= size && length <= size - offset;
}

static uint32_t read_u32le(const unsigned char *data) {
  return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool bytes_at(const BundleView &bundle, size_t offset,
                     const unsigned char *expected, size_t length) {
  return range_contains(bundle.size, offset, length) &&
         memcmp(bundle.data + offset, expected, length) == 0;
}

static bool contains(const BundleView &bundle, const void *needle,
                     size_t needle_size) {
  if (!needle || needle_size == 0 || needle_size > bundle.size) {
    return false;
  }
  const unsigned char *needle_bytes =
      reinterpret_cast<const unsigned char *>(needle);
  for (size_t i = 0; i + needle_size <= bundle.size; ++i) {
    if (memcmp(bundle.data + i, needle_bytes, needle_size) == 0) {
      return true;
    }
  }
  return false;
}

static bool locate_hermes_payload(unsigned char *buffer, size_t size,
                                  BundleView *out) {
  const size_t scan_size = size < kMaxHermesHeaderScan
                               ? size
                               : kMaxHermesHeaderScan;
  for (size_t offset = 0;
       offset + sizeof(kHermesMagic) <= scan_size; ++offset) {
    if (memcmp(buffer + offset, kHermesMagic, sizeof(kHermesMagic)) == 0) {
      *out = {buffer + offset, size - offset};
      return true;
    }
  }
  return false;
}

static bool legacy_payload_at(unsigned char *buffer, size_t size,
                              size_t offset, BundleView *out) {
  if (!range_contains(size, offset, sizeof(kLegacyBundleMagic)) ||
      memcmp(buffer + offset, kLegacyBundleMagic,
             sizeof(kLegacyBundleMagic)) != 0) {
    return false;
  }
  *out = {buffer + offset, size - offset};
  return true;
}

static bool locate_legacy_payload(unsigned char *buffer, size_t size,
                                  BundleView *out) {
  if (legacy_payload_at(buffer, size, 0, out)) {
    return true;
  }
  if (!range_contains(size, 0, sizeof(kRnpsMagic)) ||
      memcmp(buffer, kRnpsMagic, sizeof(kRnpsMagic)) != 0) {
    return false;
  }

  size_t declared_offset = 0;
  if (range_contains(size, kRnpsPayloadOffsetField, sizeof(uint32_t))) {
    declared_offset = read_u32le(buffer + kRnpsPayloadOffsetField);
  }
  const size_t candidates[] = {declared_offset, kRnpsFallbackPayloadOffset,
                               kLegacyAlternatePayloadOffset};
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
    const size_t offset = candidates[i];
    if (offset > 0 && legacy_payload_at(buffer, size, offset, out)) {
      return true;
    }
  }
  return false;
}

static const LegacySettingsProfile *
find_legacy_profile(const BundleView &payload) {
  for (const LegacySettingsProfile &profile : kLegacySettingsProfiles) {
    if (payload.size == profile.payload_size &&
        (bytes_at(payload, profile.label_offset, kLegacyOldLabel,
                  sizeof(kLegacyOldLabel)) ||
         bytes_at(payload, profile.label_offset, kLegacyNewLabel,
                  sizeof(kLegacyNewLabel))) &&
        bytes_at(payload, profile.icon_offset, kLegacyOldIcon,
                 sizeof(kLegacyOldIcon))) {
      return &profile;
    }
  }
  return nullptr;
}

static int apply_equal_length_patch(const BundleView &bundle, size_t offset,
                                    const unsigned char *stock,
                                    const unsigned char *replacement,
                                    size_t size) {
  if (!bytes_at(bundle, offset, stock, size)) {
    return 0;
  }
  memcpy(bundle.data + offset, replacement, size);
  return 1;
}

static int replace_first(const BundleView &bundle, const unsigned char *stock,
                         const unsigned char *replacement, size_t size) {
  for (size_t offset = 0; offset + size <= bundle.size; ++offset) {
    if (memcmp(bundle.data + offset, stock, size) == 0) {
      memcpy(bundle.data + offset, replacement, size);
      return 1;
    }
  }
  return 0;
}

static bool validate_hermes_settings(const BundleView &hbc,
                                     size_t *file_length) {
  static constexpr char kSettingsUriPrefix[] = "pssettings:play";
  static constexpr char kDebugSettingsFunction[] = "debug_settings";
  if (!range_contains(hbc.size, kHbcFileLengthOffset, sizeof(uint32_t)) ||
      !range_contains(hbc.size, kHbcSourceHashOffset,
                      onion::debug_settings_route::kSourceHashLength)) {
    return false;
  }

  const uint32_t declared_length =
      read_u32le(hbc.data + kHbcFileLengthOffset);
  if (declared_length < kHbcFooterSha1Size || declared_length > hbc.size ||
      !onion::debug_settings_route::settings_bundle_is_supported(
          declared_length, hbc.data + kHbcSourceHashOffset) ||
      !contains(hbc, kSettingsUriPrefix, sizeof(kSettingsUriPrefix) - 1) ||
      !contains(hbc, kDebugSettingsFunction,
                sizeof(kDebugSettingsFunction) - 1)) {
    return false;
  }

  *file_length = declared_length;
  return true;
}

static void update_hermes_footer_sha1(const BundleView &hbc,
                                      size_t file_length) {
  const size_t footer_offset = file_length - kHbcFooterSha1Size;
  SHA1_CTX context;
  SHA1Init(&context);
  SHA1Update(&context, hbc.data, static_cast<uint32_t>(footer_offset));
  SHA1Final(hbc.data + footer_offset, &context);
}

} // namespace

void patch_settings_bundle(unsigned char *buffer, int size) {
  if (!buffer || size <= 0) {
    return;
  }

  BundleView legacy = {};
  if (locate_legacy_payload(buffer, static_cast<size_t>(size), &legacy)) {
    const LegacySettingsProfile *profile = find_legacy_profile(legacy);
    if (!profile) {
      return;
    }
    const int label_count = apply_equal_length_patch(
        legacy, profile->label_offset, kLegacyOldLabel, kLegacyNewLabel,
        sizeof(kLegacyOldLabel));
    LOG_DEBUG("settings_bundle_patch: activated '%s' label=%d "
              "(stock icon id preserved; URI is intercepted at runtime)",
              profile->name, label_count);
    return;
  }

  BundleView hbc = {};
  size_t file_length = 0;
  if (!locate_hermes_payload(buffer, static_cast<size_t>(size), &hbc) ||
      !validate_hermes_settings(hbc, &file_length)) {
    return;
  }

  const int label_count =
      replace_first(hbc, kHermesOldLabel, kHermesNewLabel,
                    sizeof(kHermesOldLabel));
  if (label_count != 0) {
    update_hermes_footer_sha1(hbc, file_length);
  }
  LOG_DEBUG("settings_bundle_patch: Hermes NPXS40008 label=%d footer=%s "
            "(stock icon id preserved)",
            label_count, label_count != 0 ? "updated" : "unchanged");
}
