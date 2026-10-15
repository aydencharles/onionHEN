/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Control Center extension SDK for the 11.6 NPXS40003 bridge.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace onion::control_center {

constexpr uint8_t kFirstExtensionRegistryId = 16;
constexpr uint8_t kLastExtensionRegistryId = 59;
constexpr size_t kPersistentExtensionCapacity = 44;

/*
 * The 11.6 profile builds its tile list from a compile-time `new Array(16)`
 * and has no spare bytecode, so the list cannot grow without bytecode surgery.
 * It does, however, mark five of those sixteen tiles as feature-gated
 * (registryId 2..6: notifications, gamingLounge, music, voiceAgent,
 * broadcast), and a gated tile that is off on the target unit is a free host.
 *
 * This build claims exactly one of them - voiceAgent (registryId 5), which is
 * off on retail units - so kVisualHostCapacity116 is 1.  Claiming more means
 * patching each extra tile's descriptor, icon block and action body the same
 * way control_center_patch.cpp does for voice; the registration ABI and the
 * 44 persistent IDs below stay unchanged.
 *
 * The registry format already reserves all 44 IDs supported by the native
 * 64-byte settings record; later slot-bank profiles can raise this without
 * reassigning IDs.
 */
constexpr size_t kVisualHostCapacity116 = 1;
constexpr size_t kMaxIdBytes = 47;
constexpr size_t kMaxLabelBytes116 = 11;
constexpr size_t kMaxIconSourceBytes116 = 33;

enum class Result : uint8_t {
  Ok = 0,
  InvalidArgument,
  IdConflict,
  VisualHostFull,
  RegistryExhausted,
  PersistenceError,
  NotFound,
};

struct Invocation {
  uint8_t registry_id;
  std::string_view id;
};

using InvokeCallback = void (*)(const Invocation &invocation, void *context);
using CInvokeCallback = void (*)(uint8_t registry_id, const char *id,
                                 void *context);

struct Registration {
  std::string id;
  std::string label;
  /* System icon ID or absolute local asset path. */
  std::string icon_source;
  bool enabled_by_default = false;
  std::string action_uri;
  InvokeCallback callback = nullptr;
  CInvokeCallback c_callback = nullptr;
  void *callback_context = nullptr;
};

struct SnapshotEntry {
  uint8_t registry_id;
  std::string id;
  std::string label;
  std::string icon_source;
  bool enabled_by_default;
  bool active;
};

struct DispatchResult {
  bool handled = false;
  std::string redirect_uri;
};

/* Register or atomically update an ID. IDs keep their assigned number forever. */
Result register_control(const Registration &registration,
                        uint8_t *registry_id = nullptr);

/* Removes the live callback/UI entry but retains its on-disk slot as a tombstone. */
Result unregister_control(std::string_view id);

std::vector<SnapshotEntry> snapshot();

/* Parses and dispatches OnionCC?invoke=<registryId>. Called on the UI thread. */
DispatchResult dispatch_uri(std::string_view uri);

const char *result_string(Result result);

/* Internal ShellUI lifecycle/patch bridge. */
void initialize_builtin_control();
bool visual_registration(SnapshotEntry *entry);
void request_reload();
void poll_reload();

} // namespace onion::control_center

extern "C" {

typedef void (*onion_cc_callback_t)(uint8_t registry_id, const char *id,
                                    void *context);

struct onion_cc_registration {
  uint32_t struct_size;
  const char *id;
  const char *label;
  const char *icon_source;
  const char *action_uri;
  onion_cc_callback_t callback;
  void *callback_context;
  bool enabled_by_default;
};

/* Stable C entry points for PRX/homebrew modules. Return onion::...::Result. */
int onion_cc_register(const onion_cc_registration *registration,
                      uint8_t *registry_id);
int onion_cc_unregister(const char *id);

}
