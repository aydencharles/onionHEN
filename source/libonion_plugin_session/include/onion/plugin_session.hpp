#pragma once

#include <onion/plugin_ui.hpp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <type_traits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace onion::plugin_session {

inline constexpr uint32_t kFrameMagic = 0x4F504943u;
inline constexpr uint16_t kWireVersion = 1;
inline constexpr uint16_t kPingCommand = 1;
inline constexpr uint16_t kResponseCommand = 2;
inline constexpr uint16_t kLogCommand = 3;
inline constexpr uint16_t kNotifyCommand = 4;
inline constexpr uint16_t kConfigGetCommand = 5;
inline constexpr uint16_t kConfigSetCommand = 6;
inline constexpr uint16_t kGetStatusCommand = 7;
inline constexpr uint16_t kStopCommand = 8;
inline constexpr uint16_t kEventCommand = 9;
inline constexpr uint16_t kHelloCommand = 16;
inline constexpr size_t kMaxPayloadSize = 4096;
inline constexpr size_t kResponseHeaderSize = 8;
inline constexpr size_t kHelloHeaderSize = 12;
inline constexpr uint32_t kPluginAbiVersion = 1;
inline constexpr size_t kPluginIdMax = 32;

enum Capability : uint32_t {
  Notify = 1u << 0,
  Ipc = 1u << 1,
  Process = 1u << 2,
  Inject = 1u << 3,
  Kernel = 1u << 4,
  Ui = 1u << 5,
};

inline constexpr uint32_t kKnownCapabilities =
    Notify | Ipc | Process | Inject | Kernel | Ui;

struct Frame {
  uint32_t magic = kFrameMagic;
  uint16_t version = kWireVersion;
  uint16_t command = 0;
  uint32_t request_id = 0;
  uint32_t payload_size = 0;
  uint8_t payload[kMaxPayloadSize]{};
};

static_assert(sizeof(Frame) == 16 + kMaxPayloadSize);
static_assert(std::is_standard_layout_v<Frame>);
static_assert(std::is_trivially_copyable_v<Frame>);
static_assert(offsetof(Frame, payload) == 16);

class SessionDirectory {
public:
  bool acquire(std::string_view owner);
  void release(std::string_view owner);
  bool contains(std::string_view owner) const;
  size_t size() const;

private:
  mutable std::mutex mutex_;
  std::unordered_set<std::string> owners_;
};

class EventSource {
public:
  virtual ~EventSource() = default;
  virtual plugin_ui::WireResponse poll(std::string_view owner) = 0;
  virtual void disconnect(std::string_view owner) = 0;
};

/*
 * Host-side services a plugin can reach over the session protocol. Injected by
 * the daemon so libonion_plugin_session stays free of platform dependencies
 * (logging, notifications, and configuration persistence all live in the
 * host). A null pointer means the corresponding commands return NotSupported.
 */
class HostServices {
public:
  virtual ~HostServices() = default;

  /* LOG payload is a 32-bit little-endian level followed by UTF-8 text. */
  virtual plugin_ui::Status log(std::string_view owner, uint32_t level,
                                std::string_view message) = 0;

  /* Requires the Notify capability; enforced by ConnectionSession. */
  virtual plugin_ui::Status notify(std::string_view owner,
                                   std::string_view message) = 0;

  /* CONFIG_GET: value bytes are returned raw (no trailing NUL). */
  virtual plugin_ui::Status config_get(std::string_view owner,
                                       std::string_view key,
                                       std::vector<uint8_t> &value) = 0;

  /* CONFIG_SET: key/value are NUL-terminated strings on the wire. */
  virtual plugin_ui::Status config_set(std::string_view owner,
                                       std::string_view key,
                                       std::string_view value) = 0;
};

/* One instance is owned by one accepted transport connection. */
class ConnectionSession {
public:
  ConnectionSession(SessionDirectory &directory,
                    plugin_ui::ProtocolBroker &ui_broker,
                    EventSource *events = nullptr,
                    HostServices *host = nullptr);
  ~ConnectionSession();

  ConnectionSession(const ConnectionSession &) = delete;
  ConnectionSession &operator=(const ConnectionSession &) = delete;

  plugin_ui::WireResponse dispatch(uint16_t command,
                                   std::span<const uint8_t> payload);
  void disconnect();
  bool is_open() const;
  std::string owner() const;
  uint32_t capabilities() const;

private:
  plugin_ui::WireResponse hello(std::span<const uint8_t> payload);

  SessionDirectory &directory_;
  plugin_ui::ProtocolBroker &ui_broker_;
  EventSource *events_ = nullptr;
  HostServices *host_ = nullptr;
  mutable std::mutex mutex_;
  std::string owner_;
  uint32_t capabilities_ = 0;
  bool open_ = false;
};

struct FrameResult {
  Frame response;
  bool close_connection = false;
};

/* Validates complete SDK frames and encodes one response per request. */
class ConnectionProtocol {
public:
  ConnectionProtocol(SessionDirectory &directory,
                     plugin_ui::ProtocolBroker &ui_broker,
                     EventSource *events = nullptr,
                     HostServices *host = nullptr)
      : session_(directory, ui_broker, events, host) {}

  FrameResult handle(const Frame &request);
  ConnectionSession &session() { return session_; }
  const ConnectionSession &session() const { return session_; }

private:
  ConnectionSession session_;
};

int32_t wire_status(plugin_ui::Status status);

} // namespace onion::plugin_session
