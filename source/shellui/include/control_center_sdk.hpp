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
 * A tile can therefore only be added by taking one over.
 *
 * This build claims accessibility (registryId 7).  It is the best host of the
 * sixteen because module 193's factory emits r29[7] / r23[7] with no
 * `isRevoked` member at all: the slot is unconditional, so it needs neither a
 * feature-gate rewrite nor an isRevoked rewrite, and the persisted
 * control-visibility blob always resolves it.
 *
 * The five feature-gated tiles (registryId 2..6: notifications,
 * gamingLounge, music, voiceAgent, broadcast) are a worse choice.  voiceAgent
 * in particular is reported disabled by retail firmware and the whole VnaUI
 * subsystem is compiled out, so it cannot be relied on to appear.
 *
 * kVisualHostCapacity116 stays 1: one host, claimed by this patch.  Raising it
 * means patching another tile's descriptor, icon block and getMenu closure the
 * same way control_center_patch.cpp does for accessibility; the registration
 * ABI and the 44 persistent IDs below stay unchanged.
 *
 * The registry format already reserves all 44 IDs supported by the native
 * 64-byte settings record; later slot-bank profiles can raise this without
 * reassigning IDs.
 */
constexpr size_t kVisualHostCapacity116 = 1;
constexpr size_t kMaxIdBytes = 47;
constexpr size_t kMaxLabelBytes116 = 11;
constexpr size_t kMaxIconSourceBytes116 = 33;
/*
 * Byte capacity of the HBC string slot (#13209) the 11.6 patch repoints at
 * Registration::web_url.  #13209 declares 39 bytes but the next string in the
 * compacted storage starts at +38, so 38 is the widest safe write.  Anything
 * longer cannot be written into the bundle, so it is rejected at registration
 * time instead of silently skipping the patch.
 */
constexpr size_t kMaxWebUrlBytes116 = 38;

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
  /*
   * URI the tile's popup loads in a native WebViewPS panel.  On the 11.6 host
   * the popup is driven by the tile's `getMenu` hook, not by `onPress`, so a
   * web_url gives an in-app panel instead of handing the URI to the system
   * handler the way action_uri does.
   *
   * Must fit the 38-byte string slot the patch rewrites (kUriCapacity), so
   * keep it short - "http://127.0.0.1:9090/" is 22 bytes.  When empty the
   * patch falls back to OnionCC?invoke=<registry_id>, which renders nothing
   * but keeps dispatch_uri() reachable.
   */
  std::string web_url;
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
  /*
   * Copied out of Registration::web_url so the 11.6 patch can point the tile's
   * popup WebView at it without reaching back into the SDK's private state.
   */
  std::string web_url;
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
bool has_pending_reload();
bool poll_reload();

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
