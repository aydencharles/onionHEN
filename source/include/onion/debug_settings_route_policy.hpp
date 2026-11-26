#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace onion::debug_settings_route {

enum class UriKind {
  WithMode,
  Simple,
};

enum class RouteVariant {
  Standard,
  Old,
};

inline constexpr uint32_t kVersionMin = 0x00000000;
inline constexpr uint32_t kVersionMax = 0xffffffff;
inline constexpr size_t kSourceHashLength = 20;

struct RouteDefinition {
  RouteVariant variant;
  const char *with_mode_uri;
  const char *simple_uri;
};

struct SettingsBundleFingerprint {
  uint32_t hbc_file_length;
  uint8_t source_hash[kSourceHashLength];
};

struct CompatibilityProfile {
  const char *name;
  uint32_t min_version;
  uint32_t max_version;
  RouteVariant variant;
  const SettingsBundleFingerprint *settings_bundles;
  size_t settings_bundle_count;
};

inline constexpr RouteDefinition kStandardRoute{
    RouteVariant::Standard,
    "pssettings:play?mode=settings&function=debug_settings",
    "pssettings:play?function=debug_settings",
};

inline constexpr RouteDefinition kOldRoute{
    RouteVariant::Old,
    "pssettings:play?mode=settings&function=debug_settings_old",
    "pssettings:play?function=debug_settings_old",
};

inline constexpr SettingsBundleFingerprint kStandardSettingsBundles[] = {
    {
        0x4b1934, // 9.00 NPXS40008
        {0x72, 0x18, 0x8b, 0x52, 0xb1, 0x2b, 0xad, 0x6a, 0xf9, 0x0c,
         0x90, 0xa8, 0x48, 0xb7, 0xfd, 0x76, 0xe5, 0xaf, 0x10, 0x2d},
    },
    {
        0x4ba2c0, // 9.40 NPXS40008
        {0x4f, 0x1a, 0xe4, 0xb6, 0x78, 0x6c, 0xc9, 0x66, 0x46, 0xe1,
         0x4e, 0xec, 0x92, 0x3d, 0x09, 0xc3, 0xc0, 0x31, 0xe9, 0x80},
    },
    {
        0x4ba1f0, // 9.60 NPXS40008
        {0x59, 0x50, 0x6d, 0x7b, 0x5c, 0x59, 0x51, 0x96, 0xb2, 0x16,
         0x61, 0x26, 0x4b, 0xc9, 0xc1, 0xb4, 0x87, 0xd2, 0x1b, 0x51},
    },
    {
        0x4dda8c, // 10.01 NPXS40008
        {0xad, 0x6c, 0xf2, 0xd6, 0xf8, 0x97, 0x4c, 0xcd, 0x34, 0xb1,
         0x4e, 0x69, 0xbb, 0x6e, 0x34, 0x0e, 0x8d, 0xec, 0x5d, 0xc5},
    },
    {
        0x4e0954, // 10.6 NPXS40008
        {0x31, 0x65, 0x1a, 0x18, 0x8d, 0x49, 0xb2, 0x3b, 0x76, 0x35,
         0xaf, 0xa4, 0x49, 0x39, 0x5e, 0x0f, 0xbd, 0x9f, 0x68, 0x2a},
    },
    {
        // 10.2 and 10.4 NPXS40008 dumps are byte-identical.
        0x4e089c, // 10.2/10.4 NPXS40008
        {0xab, 0xb8, 0xfd, 0xf5, 0xa8, 0x94, 0xce, 0x6f, 0xd1, 0xe9,
         0x93, 0x81, 0xd0, 0x86, 0x6b, 0x33, 0xf2, 0x79, 0xc7, 0xb9},
    },
};

inline constexpr SettingsBundleFingerprint kOldRouteSettingsBundles[] = {
    {
        0x4fa540, // 11.0 NPXS40008
        {0x18, 0x24, 0xc9, 0xfb, 0x56, 0x2e, 0x31, 0xee, 0xf6, 0x51,
         0xbb, 0x38, 0x74, 0xc1, 0xc7, 0x3f, 0x7f, 0x6e, 0x24, 0xb0},
    },
    {
        0x4f45b8, // 11.2 NPXS40008
        {0xd0, 0x34, 0x62, 0xa9, 0x12, 0xc4, 0xb5, 0xb8, 0xdb, 0x4a,
         0x98, 0xd0, 0x44, 0xb9, 0xd4, 0x88, 0xa2, 0xdf, 0xfc, 0x7a},
    },
    {
        0x4f45c4, // 11.4 NPXS40008
        {0xa7, 0xb7, 0x31, 0x57, 0x1f, 0x84, 0xb6, 0xcd, 0xaf, 0x7c,
         0x42, 0x27, 0xa9, 0x80, 0xba, 0x5e, 0xe2, 0x00, 0x04, 0xa8},
    },
    {
        0x4f4bfc, // 11.6 NPXS40008
        {0x92, 0x56, 0x61, 0x24, 0xb6, 0xcf, 0xe0, 0xb0, 0xa7, 0xc8,
         0x12, 0xfc, 0x8a, 0x3b, 0xbf, 0xcf, 0x32, 0xac, 0x46, 0x83},
    },
    {
        // 12.0 and 12.02 NPXS40008 dumps are byte-identical.
        0x4e7bec, // 12.0/12.02 NPXS40008
        {0xfc, 0x7c, 0x4f, 0x15, 0xaf, 0x42, 0x92, 0x9e, 0x1d, 0x52,
         0x42, 0x0c, 0x2d, 0x17, 0x49, 0x44, 0xb4, 0xa8, 0x80, 0x43},
    },
    {
        0x4e9028, // 12.6 NPXS40008
        {0x75, 0x74, 0x7b, 0xb5, 0xfa, 0x7e, 0x3a, 0x4e, 0x22, 0xd5,
         0x57, 0x88, 0x2f, 0x52, 0x81, 0xe4, 0xd1, 0xf1, 0x29, 0x59},
    },
    {
        0x4e9048, // 12.7 NPXS40008
        {0x44, 0x5d, 0xa8, 0xbc, 0xba, 0x93, 0xda, 0x16, 0x54, 0x73,
         0xd3, 0xda, 0x49, 0x1d, 0x9b, 0x13, 0xf9, 0x63, 0x16, 0xcd},
    },
    {
        // 12.4 and 12.20 NPXS40008 dumps are byte-identical.
        0x4e8e54, // 12.4/12.20 NPXS40008
        {0x5d, 0x44, 0x61, 0x85, 0x8b, 0x0a, 0x38, 0xfc, 0x6e, 0x7b,
         0x08, 0x6d, 0xbf, 0xda, 0xb6, 0x19, 0x51, 0x59, 0x08, 0x0e},
    },
    {
        0x4eb9b4, // 13.0 NPXS40008
        {0x5f, 0xb0, 0x07, 0xc0, 0x0f, 0x7b, 0xa3, 0x45, 0x41, 0x03,
         0x2b, 0x29, 0x97, 0xbd, 0xab, 0xd1, 0x9d, 0x53, 0x4e, 0x78},
    },
    {
        // 13.2, 13.4 and 13.42 NPXS40008 dumps are byte-identical.
        0x4ebe08, // 13.2/13.4/13.42 NPXS40008
        {0xfb, 0x05, 0x75, 0x14, 0x64, 0x29, 0x17, 0x61, 0xde, 0x1f,
         0xdc, 0xd9, 0x57, 0x6f, 0xc2, 0x6a, 0x02, 0x93, 0x0e, 0x10},
    },
    {
        0x4ebc8c, // 13.6 NPXS40008
        {0xa4, 0x00, 0xa5, 0xca, 0x93, 0x2e, 0xfc, 0x80, 0xcb, 0x65,
         0x12, 0x61, 0x09, 0x03, 0x10, 0x0f, 0xa0, 0x3a, 0x87, 0x17},
    },
};

inline constexpr CompatibilityProfile kCompatibilityProfiles[] = {
    {
        "standard-through-10.6",
        kVersionMin,
        0x1006ffff,
        RouteVariant::Standard,
        kStandardSettingsBundles,
        sizeof(kStandardSettingsBundles) / sizeof(kStandardSettingsBundles[0]),
    },
    {
        "old-route-11.x-plus",
        0x11000000,
        kVersionMax,
        RouteVariant::Old,
        kOldRouteSettingsBundles,
        sizeof(kOldRouteSettingsBundles) / sizeof(kOldRouteSettingsBundles[0]),
    },
};

class DebugSettingsRoutePolicy {
public:
  constexpr DebugSettingsRoutePolicy()
      : variant_(RouteVariant::Standard) {}

  constexpr explicit DebugSettingsRoutePolicy(RouteVariant variant)
      : variant_(variant) {}

  static constexpr DebugSettingsRoutePolicy for_system_version(
      uint32_t system_version) {
    return DebugSettingsRoutePolicy(resolve_variant(system_version));
  }

  constexpr RouteVariant variant() const {
    return variant_;
  }

  constexpr bool uses_old_route() const {
    return variant_ == RouteVariant::Old;
  }

  constexpr const char *toolbox_uri(UriKind kind) const {
    const RouteDefinition &definition = route_definition();
    return kind == UriKind::WithMode ? definition.with_mode_uri
                                     : definition.simple_uri;
  }

  std::string rewrite(std::string_view uri) const {
    if (!uses_old_route())
      return std::string(uri);
    return rewrite_function_param(std::string(uri), "debug_settings_old");
  }

private:
  static constexpr RouteVariant resolve_variant(uint32_t system_version) {
    for (const CompatibilityProfile &profile : kCompatibilityProfiles) {
      if (system_version >= profile.min_version &&
          system_version <= profile.max_version) {
        return profile.variant;
      }
    }
    return RouteVariant::Standard;
  }

  constexpr const RouteDefinition &route_definition() const {
    return uses_old_route() ? kOldRoute : kStandardRoute;
  }

  static std::string rewrite_function_param(std::string uri,
                                            std::string_view new_value) {
    const size_t query_begin = uri.find('?');
    if (query_begin == std::string::npos)
      return uri;

    const size_t fragment_begin = uri.find('#', query_begin + 1);
    const size_t query_end =
        fragment_begin == std::string::npos ? uri.size() : fragment_begin;

    size_t part_begin = query_begin + 1;
    while (part_begin <= query_end) {
      size_t part_end = uri.find('&', part_begin);
      if (part_end == std::string::npos || part_end > query_end)
        part_end = query_end;

      const size_t equals = uri.find('=', part_begin);
      if (equals != std::string::npos && equals < part_end) {
        const std::string_view key(uri.data() + part_begin,
                                   equals - part_begin);
        const size_t value_begin = equals + 1;
        const std::string_view value(uri.data() + value_begin,
                                     part_end - value_begin);
        if (key == "function" && value == "debug_settings") {
          uri.replace(value_begin, value.size(), new_value.data(),
                      new_value.size());
          return uri;
        }
      }

      if (part_end == query_end)
        break;
      part_begin = part_end + 1;
    }
    return uri;
  }

  RouteVariant variant_;
};

inline bool source_hash_equals(
    const uint8_t *source_hash,
    const SettingsBundleFingerprint &fingerprint) {
  if (!source_hash)
    return false;
  for (size_t i = 0; i < kSourceHashLength; ++i) {
    if (source_hash[i] != fingerprint.source_hash[i])
      return false;
  }
  return true;
}

inline bool settings_bundle_is_supported(uint32_t hbc_file_length,
                                         const uint8_t *source_hash) {
  for (const CompatibilityProfile &profile : kCompatibilityProfiles) {
    for (size_t i = 0; i < profile.settings_bundle_count; ++i) {
      const SettingsBundleFingerprint &fingerprint = profile.settings_bundles[i];
      if (hbc_file_length == fingerprint.hbc_file_length &&
          source_hash_equals(source_hash, fingerprint)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace onion::debug_settings_route
