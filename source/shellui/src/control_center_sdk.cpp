/* Copyright (C) 2025 OnionHEN / LightningMods */
#include "control_center_sdk.hpp"

#include "control_center_patch.hpp"
#include "defs.h"
#include "debug_settings_route_runtime.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "onion_web_server.hpp"

#include <msg.hpp>
#include <onion/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::control_center {
namespace {

constexpr char kRegistryDirectory[] = "/user/data/OnionHEN";
constexpr char kRegistryPath[] =
    "/user/data/OnionHEN/control_center_registry.bin";
constexpr char kRegistryTempPath[] =
    "/user/data/OnionHEN/control_center_registry.tmp";
constexpr std::array<uint8_t, 8> kManifestMagic = {'O', 'N', 'C', 'C',
                                                   'R', 'E', 'G', '1'};
constexpr uint16_t kManifestVersion = 1;

#pragma pack(push, 1)
struct ManifestHeader {
  uint8_t magic[8];
  uint16_t version;
  uint16_t record_count;
  uint32_t checksum;
};

struct ManifestRecord {
  uint8_t registry_id;
  uint8_t assigned;
  uint8_t id_length;
  uint8_t reserved;
  char id[kMaxIdBytes + 1];
};

struct ManifestFile {
  ManifestHeader header;
  ManifestRecord records[kPersistentExtensionCapacity];
};
#pragma pack(pop)

static_assert(sizeof(ManifestRecord) == 52);
static_assert(sizeof(ManifestFile) == 2304);

struct ActiveRegistration {
  Registration registration;
  uint8_t registry_id;
};

std::mutex g_mutex;
bool g_loaded = false;
std::array<ManifestRecord, kPersistentExtensionCapacity> g_records{};
std::vector<ActiveRegistration> g_active;
#if SHELLUI_CONTROL_CENTER_PATCH == 1
std::atomic<bool> g_reload_pending{false};
#endif

uint32_t fnv1a(const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

bool write_all(int fd, const void *data, size_t size) {
  const auto *cursor = static_cast<const uint8_t *>(data);
  while (size != 0) {
    const ssize_t written = write(fd, cursor, size);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0)
      return false;
    cursor += static_cast<size_t>(written);
    size -= static_cast<size_t>(written);
  }
  return true;
}

bool read_all(int fd, void *data, size_t size) {
  auto *cursor = static_cast<uint8_t *>(data);
  while (size != 0) {
    const ssize_t count = read(fd, cursor, size);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (count == 0)
      return false;
    cursor += static_cast<size_t>(count);
    size -= static_cast<size_t>(count);
  }
  return true;
}

void initialize_empty_records() {
  for (size_t i = 0; i < g_records.size(); ++i) {
    g_records[i] = {};
    g_records[i].registry_id =
        static_cast<uint8_t>(kFirstExtensionRegistryId + i);
  }
}

bool valid_record(const ManifestRecord &record, size_t index) {
  const uint8_t expected =
      static_cast<uint8_t>(kFirstExtensionRegistryId + index);
  if (record.registry_id != expected || record.assigned > 1 ||
      record.id_length > kMaxIdBytes || record.reserved != 0) {
    return false;
  }
  if (!record.assigned)
    return record.id_length == 0;
  return record.id_length != 0 && record.id[record.id_length] == '\0';
}

void load_manifest_locked() {
  if (g_loaded)
    return;
  g_loaded = true;
  initialize_empty_records();

  const int fd = open(kRegistryPath, O_RDONLY, 0);
  if (fd < 0)
    return;

  ManifestFile file{};
  const bool read_ok = read_all(fd, &file, sizeof(file));
  close(fd);
  if (!read_ok ||
      memcmp(file.header.magic, kManifestMagic.data(), kManifestMagic.size()) !=
          0 ||
      file.header.version != kManifestVersion ||
      file.header.record_count != kPersistentExtensionCapacity ||
      file.header.checksum !=
          fnv1a(file.records, sizeof(file.records))) {
    LOG_WARN("control_center_sdk: ignored invalid registry manifest");
    return;
  }

  for (size_t i = 0; i < g_records.size(); ++i) {
    if (!valid_record(file.records[i], i)) {
      LOG_WARN("control_center_sdk: invalid manifest record %u",
               static_cast<unsigned>(i));
      initialize_empty_records();
      return;
    }
  }
  std::copy(std::begin(file.records), std::end(file.records),
            g_records.begin());
}

bool save_manifest_locked() {
  (void)mkdir(kRegistryDirectory, 0777);

  ManifestFile file{};
  memcpy(file.header.magic, kManifestMagic.data(), kManifestMagic.size());
  file.header.version = kManifestVersion;
  file.header.record_count = kPersistentExtensionCapacity;
  std::copy(g_records.begin(), g_records.end(), std::begin(file.records));
  file.header.checksum = fnv1a(file.records, sizeof(file.records));

  const int fd = open(kRegistryTempPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    return false;
  const bool ok = write_all(fd, &file, sizeof(file)) && fsync(fd) == 0;
  const int saved_errno = errno;
  close(fd);
  if (!ok) {
    unlink(kRegistryTempPath);
    errno = saved_errno;
    return false;
  }
  if (rename(kRegistryTempPath, kRegistryPath) != 0) {
    unlink(kRegistryTempPath);
    return false;
  }
  return true;
}

bool valid_id(std::string_view id) {
  if (id.empty() || id.size() > kMaxIdBytes)
    return false;
  for (const unsigned char value : id) {
    const bool valid = (value >= 'a' && value <= 'z') ||
                       (value >= 'A' && value <= 'Z') ||
                       (value >= '0' && value <= '9') || value == '.' ||
                       value == '_' || value == '-';
    if (!valid)
      return false;
  }
  return true;
}

bool valid_registration(const Registration &registration) {
  return valid_id(registration.id) && !registration.label.empty() &&
         registration.label.size() <= kMaxLabelBytes116 &&
         !registration.icon_source.empty() &&
         registration.icon_source.size() <= kMaxIconSourceBytes116 &&
         registration.web_url.size() <= kMaxWebUrlBytes116 &&
         (registration.callback != nullptr || registration.c_callback != nullptr ||
          !registration.action_uri.empty() || !registration.web_url.empty());
}

ManifestRecord *record_for_id_locked(std::string_view id) {
  for (auto &record : g_records) {
    if (record.assigned && record.id_length == id.size() &&
        memcmp(record.id, id.data(), id.size()) == 0) {
      return &record;
    }
  }
  return nullptr;
}

ManifestRecord *allocate_record_locked(std::string_view id) {
  for (auto &record : g_records) {
    if (record.assigned)
      continue;
    record.assigned = 1;
    record.id_length = static_cast<uint8_t>(id.size());
    memcpy(record.id, id.data(), id.size());
    record.id[id.size()] = '\0';
    return &record;
  }
  return nullptr;
}

ActiveRegistration *active_for_id_locked(std::string_view id) {
  for (auto &active : g_active) {
    if (active.registration.id == id)
      return &active;
  }
  return nullptr;
}

bool parse_invoke_uri(std::string_view uri, uint8_t *registry_id) {
  constexpr std::string_view prefix = "OnionCC?invoke=";
  if (!uri.starts_with(prefix))
    return false;
  uri.remove_prefix(prefix.size());
  const size_t amp = uri.find('&');
  const std::string_view value = uri.substr(0, amp);
  if (value.empty() || value.size() > 2)
    return false;
  unsigned parsed = 0;
  for (const char c : value) {
    if (c < '0' || c > '9')
      return false;
    parsed = parsed * 10u + static_cast<unsigned>(c - '0');
  }
  if (parsed < kFirstExtensionRegistryId ||
      parsed > kLastExtensionRegistryId)
    return false;
  *registry_id = static_cast<uint8_t>(parsed);
  return true;
}

} // namespace

Result register_control(const Registration &registration,
                        uint8_t *registry_id) {
  if (!valid_registration(registration))
    return Result::InvalidArgument;

  uint8_t assigned_id = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    load_manifest_locked();

    ActiveRegistration *existing = active_for_id_locked(registration.id);
    if (!existing && g_active.size() >= kVisualHostCapacity116)
      return Result::VisualHostFull;

    ManifestRecord *record = record_for_id_locked(registration.id);
    if (!record) {
      record = allocate_record_locked(registration.id);
      if (!record)
        return Result::RegistryExhausted;
      if (!save_manifest_locked()) {
        *record = {};
        const size_t index = static_cast<size_t>(record - g_records.data());
        record->registry_id =
            static_cast<uint8_t>(kFirstExtensionRegistryId + index);
        return Result::PersistenceError;
      }
    }

    assigned_id = record->registry_id;
    if (existing) {
      existing->registration = registration;
    } else {
      g_active.push_back({registration, assigned_id});
    }
  }

  if (registry_id)
    *registry_id = assigned_id;
  request_reload();
  LOG_DEBUG("control_center_sdk: registered '%s' as registryId=%u",
            registration.id.c_str(), static_cast<unsigned>(assigned_id));
  return Result::Ok;
}

Result unregister_control(std::string_view id) {
  if (!valid_id(id))
    return Result::InvalidArgument;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    load_manifest_locked();
    const auto iterator =
        std::find_if(g_active.begin(), g_active.end(), [&](const auto &entry) {
          return entry.registration.id == id;
        });
    if (iterator == g_active.end())
      return Result::NotFound;
    g_active.erase(iterator);
  }
  request_reload();
  LOG_DEBUG("control_center_sdk: unregistered '%.*s' (slot tombstoned)",
            static_cast<int>(id.size()), id.data());
  return Result::Ok;
}

std::vector<SnapshotEntry> snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  load_manifest_locked();
  std::vector<SnapshotEntry> result;
  result.reserve(g_records.size());
  for (const auto &record : g_records) {
    if (!record.assigned)
      continue;
    const std::string_view id(record.id, record.id_length);
    const ActiveRegistration *active = nullptr;
    for (const auto &candidate : g_active) {
      if (candidate.registration.id == id) {
        active = &candidate;
        break;
      }
    }
    result.push_back({record.registry_id,
                      std::string(id),
                      active ? active->registration.label : std::string(),
                      active ? active->registration.icon_source
                             : std::string(),
                      active && active->registration.enabled_by_default,
                      active != nullptr});
  }
  return result;
}

bool visual_registration(SnapshotEntry *entry) {
  if (!entry)
    return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  /*
   * The 11.6 profile only ever has one visual host, so "the first active
   * registration" is unambiguous.  Raising kVisualHostCapacity116 means
   * picking the slot explicitly instead (one tile per active registration).
   */
  if (g_active.empty())
    return false;
  const auto &active = g_active.front();
  *entry = {active.registry_id,
            active.registration.id,
            active.registration.label,
            active.registration.icon_source,
            active.registration.enabled_by_default,
            true,
            active.registration.web_url};
  return true;
}

DispatchResult dispatch_uri(std::string_view uri) {
  uint8_t registry_id = 0;
  if (!parse_invoke_uri(uri, &registry_id))
    return {};

  Registration registration;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto iterator = std::find_if(
        g_active.begin(), g_active.end(), [&](const auto &candidate) {
          return candidate.registry_id == registry_id;
        });
    if (iterator == g_active.end())
      return {};
    registration = iterator->registration;
  }

  if (registration.callback) {
    const Invocation invocation{registry_id, registration.id};
    registration.callback(invocation, registration.callback_context);
  }
  if (registration.c_callback) {
    registration.c_callback(registry_id, registration.id.c_str(),
                            registration.callback_context);
  }
  return {true, registration.action_uri};
}

void request_reload() {
#if SHELLUI_CONTROL_CENTER_PATCH == 1
  g_reload_pending.store(true, std::memory_order_release);
#endif
}

void poll_reload() {
#if SHELLUI_CONTROL_CENTER_PATCH == 1
  if (!g_reload_pending.exchange(false, std::memory_order_acq_rel))
    return;
  LOG_DEBUG("control_center_sdk: reloading NPXS40003 on UI thread");
  ReloadRNPSApp("NPXS40003");
#endif
}

void initialize_builtin_control() {
  Registration registration;
  registration.id = "onionhen.toolbox";
  registration.label = "OnionHEN";
  registration.icon_source = "/system_ex/vsh_asset/onionhen.png";
  registration.enabled_by_default = false;
  registration.action_uri = shellui_debug_settings_toolbox_uri();
  /*
   * The 11.6 tile popup is a native WebViewPS, not the system URI handler, so
   * point it at the loopback web UI that prx.cpp starts before this call.  The
   * address is fixed, so the patch can bake it into the bundle once.
   */
  registration.web_url = onion::web_ui::url();
  const Result result = register_control(registration, nullptr);
  if (result != Result::Ok) {
    LOG_ERROR("control_center_sdk: built-in registration failed: %s",
              result_string(result));
  }
}

const char *result_string(Result result) {
  switch (result) {
  case Result::Ok:
    return "ok";
  case Result::InvalidArgument:
    return "invalid argument";
  case Result::IdConflict:
    return "id conflict";
  case Result::VisualHostFull:
    return "11.6 visual host full";
  case Result::RegistryExhausted:
    return "registry exhausted";
  case Result::PersistenceError:
    return "persistence error";
  case Result::NotFound:
    return "not found";
  }
  return "unknown";
}

} // namespace onion::control_center

extern "C" int onion_cc_register(const onion_cc_registration *registration,
                                  uint8_t *registry_id) {
  using namespace onion::control_center;
  if (!registration || registration->struct_size != sizeof(*registration) ||
      !registration->id || !registration->label ||
      !registration->icon_source ||
      ((!registration->action_uri || registration->action_uri[0] == '\0') &&
       !registration->callback)) {
    return static_cast<int>(Result::InvalidArgument);
  }

  Registration value;
  value.id = registration->id;
  value.label = registration->label;
  value.icon_source = registration->icon_source;
  value.enabled_by_default = registration->enabled_by_default;
  if (registration->action_uri)
    value.action_uri = registration->action_uri;
  value.callback_context = registration->callback_context;

  value.c_callback = registration->callback;
  return static_cast<int>(register_control(value, registry_id));
}

extern "C" int onion_cc_unregister(const char *id) {
  using namespace onion::control_center;
  if (!id)
    return static_cast<int>(Result::InvalidArgument);
  return static_cast<int>(unregister_control(id));
}
