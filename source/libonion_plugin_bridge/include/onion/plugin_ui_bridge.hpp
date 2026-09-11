#pragma once

#include <onion/plugin_ui.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace onion::plugin_bridge {

inline constexpr uint32_t kMagic = 0x4255494fu; /* 'OIUB' */
inline constexpr uint16_t kVersion = 1;
inline constexpr size_t kFrameHeaderSize = 16;
inline constexpr size_t kMaxPayloadSize = 16u * 1024u * 1024u;
inline constexpr size_t kMaxContributions = 256;
inline constexpr uint32_t kUiActionEventId = 0x108;
inline constexpr uint32_t kUiEventAbiVersion = 1;

enum class MessageType : uint16_t {
  Snapshot = 1,
  Action = 2,
};

struct FrameHeader {
  MessageType type = MessageType::Snapshot;
  uint32_t payload_size = 0;
};

struct ActionRequest {
  plugin_ui::Handle handle = 0;
  std::string node_id;
  std::string value;
};

bool encode_frame_header(const FrameHeader &header,
                         std::span<uint8_t, kFrameHeaderSize> out);
bool decode_frame_header(std::span<const uint8_t, kFrameHeaderSize> encoded,
                         FrameHeader &out);
bool encode_snapshot(const plugin_ui::RegistrySnapshot &snapshot,
                     std::vector<uint8_t> &out, std::string *error = nullptr);
bool decode_snapshot(std::span<const uint8_t> encoded,
                     plugin_ui::RegistrySnapshot &out,
                     std::string *error = nullptr);
bool encode_action(const ActionRequest &action, std::vector<uint8_t> &out);
bool decode_action(std::span<const uint8_t> encoded, ActionRequest &out);

/* Response data for ONION_PLUGIN_IPC_EVENT polling. */
bool encode_ui_event(const plugin_ui::ActionEvent &event,
                     std::vector<uint8_t> &out);

} // namespace onion::plugin_bridge
