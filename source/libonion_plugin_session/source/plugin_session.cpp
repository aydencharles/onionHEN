#include "onion/plugin_session.hpp"

#include <algorithm>

namespace onion::plugin_session {
namespace {

uint16_t read_u16(std::span<const uint8_t> bytes, size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t read_u32(std::span<const uint8_t> bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

void write_u32(uint8_t *out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[i] = static_cast<uint8_t>(value >> (i * 8));
}

bool valid_id(std::string_view id) {
  if (id.empty() || id.size() >= kPluginIdMax) return false;
  return std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
  });
}

bool is_ui_command(uint16_t command) {
  return command >=
             static_cast<uint16_t>(plugin_ui::WireCommand::RegisterBegin) &&
         command <= static_cast<uint16_t>(plugin_ui::WireCommand::SetValue);
}

} // namespace

bool SessionDirectory::acquire(std::string_view owner) {
  if (!valid_id(owner)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  return owners_.emplace(owner).second;
}

void SessionDirectory::release(std::string_view owner) {
  std::lock_guard<std::mutex> lock(mutex_);
  owners_.erase(std::string(owner));
}

bool SessionDirectory::contains(std::string_view owner) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return owners_.contains(std::string(owner));
}

size_t SessionDirectory::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return owners_.size();
}

ConnectionSession::ConnectionSession(SessionDirectory &directory,
                                     plugin_ui::ProtocolBroker &ui_broker,
                                     EventSource *events, HostServices *host)
    : directory_(directory), ui_broker_(ui_broker), events_(events),
      host_(host) {}

ConnectionSession::~ConnectionSession() { disconnect(); }

plugin_ui::WireResponse
ConnectionSession::hello(std::span<const uint8_t> payload) {
  if (open_) return {plugin_ui::Status::InvalidState, {}};
  if (payload.size() < kHelloHeaderSize || read_u32(payload, 0) != kPluginAbiVersion ||
      read_u16(payload, 10) != 0)
    return {};
  const uint32_t capabilities = read_u32(payload, 4);
  const uint16_t id_size = read_u16(payload, 8);
  if ((capabilities & ~kKnownCapabilities) != 0 || id_size == 0 ||
      payload.size() != kHelloHeaderSize + id_size)
    return {};
  const std::string_view owner(
      reinterpret_cast<const char *>(payload.data() + kHelloHeaderSize), id_size);
  if (!valid_id(owner)) return {};
  if (!directory_.acquire(owner)) return {plugin_ui::Status::Busy, {}};
  owner_.assign(owner);
  capabilities_ = capabilities;
  open_ = true;
  return {plugin_ui::Status::Ok, {}};
}

plugin_ui::WireResponse
ConnectionSession::dispatch(uint16_t command, std::span<const uint8_t> payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (command == kHelloCommand) return hello(payload);
  if (!open_) return {plugin_ui::Status::InvalidState, {}};
  if (command == kPingCommand)
    return payload.empty() ? plugin_ui::WireResponse{plugin_ui::Status::Ok, {}}
                           : plugin_ui::WireResponse{};
  if (command == kEventCommand) {
    if (!payload.empty()) return {};
    if ((capabilities_ & Ui) == 0)
      return {plugin_ui::Status::PermissionDenied, {}};
    return events_ ? events_->poll(owner_)
                   : plugin_ui::WireResponse{plugin_ui::Status::NotSupported,
                                             {}};
  }
  if (command == kLogCommand) {
    if (payload.size() < 4) return {};
    const uint32_t level = read_u32(payload, 0);
    if (level > 3) return {};
    const std::string_view message(
        reinterpret_cast<const char *>(payload.data() + 4),
        payload.size() - 4);
    if (message.find('\0') != std::string_view::npos) return {};
    if (host_)
      return plugin_ui::WireResponse{host_->log(owner_, level, message), {}};
    return plugin_ui::WireResponse{plugin_ui::Status::NotSupported, {}};
  }
  if (command == kNotifyCommand) {
    if ((capabilities_ & Notify) == 0)
      return {plugin_ui::Status::PermissionDenied, {}};
    if (payload.empty()) return {};
    const std::string_view message(
        reinterpret_cast<const char *>(payload.data()), payload.size());
    if (message.find('\0') != std::string_view::npos) return {};
    if (host_)
      return plugin_ui::WireResponse{host_->notify(owner_, message), {}};
    return plugin_ui::WireResponse{plugin_ui::Status::NotSupported, {}};
  }
  if (command == kConfigGetCommand) {
    if (payload.empty() || payload.back() != 0) return {};
    const std::string_view key(
        reinterpret_cast<const char *>(payload.data()), payload.size() - 1);
    if (key.empty() || key.find('\0') != std::string_view::npos) return {};
    plugin_ui::WireResponse response{plugin_ui::Status::NotSupported, {}};
    if (host_) response.status = host_->config_get(owner_, key, response.data);
    return response;
  }
  if (command == kConfigSetCommand) {
    size_t key_end = payload.size();
    for (size_t index = 0; index < payload.size(); ++index) {
      if (payload[index] == 0) {
        key_end = index;
        break;
      }
    }
    if (key_end == 0 || key_end == payload.size() || key_end + 2 > payload.size() ||
        payload.back() != 0)
      return {};
    const std::string_view key(
        reinterpret_cast<const char *>(payload.data()), key_end);
    const std::string_view value(
        reinterpret_cast<const char *>(payload.data() + key_end + 1),
        payload.size() - key_end - 2);
    if (value.find('\0') != std::string_view::npos) return {};
    if (host_)
      return plugin_ui::WireResponse{host_->config_set(owner_, key, value), {}};
    return plugin_ui::WireResponse{plugin_ui::Status::NotSupported, {}};
  }
  if (!is_ui_command(command))
    return {plugin_ui::Status::NotSupported, {}};
  if ((capabilities_ & Ui) == 0)
    return {plugin_ui::Status::PermissionDenied, {}};
  return ui_broker_.dispatch(owner_, command, payload);
}

void ConnectionSession::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_) return;
  ui_broker_.disconnect(owner_);
  if (events_) events_->disconnect(owner_);
  directory_.release(owner_);
  owner_.clear();
  capabilities_ = 0;
  open_ = false;
}

bool ConnectionSession::is_open() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_;
}

std::string ConnectionSession::owner() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return owner_;
}

uint32_t ConnectionSession::capabilities() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return capabilities_;
}

int32_t wire_status(plugin_ui::Status status) {
  switch (status) {
  case plugin_ui::Status::Ok:
    return 0;
  case plugin_ui::Status::InvalidArgument:
  case plugin_ui::Status::InvalidDocument:
    return -1;
  case plugin_ui::Status::InvalidState:
  case plugin_ui::Status::Stale:
    return -2;
  case plugin_ui::Status::NotSupported:
    return -5;
  case plugin_ui::Status::InvalidEncoding:
    return -8;
  case plugin_ui::Status::PermissionDenied:
    return -10;
  case plugin_ui::Status::NotFound:
    return -11;
  case plugin_ui::Status::Busy:
    return -12;
  }
  return -8;
}

FrameResult ConnectionProtocol::handle(const Frame &request) {
  FrameResult result;
  result.response.command = kResponseCommand;
  result.response.request_id = request.request_id;

  plugin_ui::WireResponse dispatched;
  if (request.magic != kFrameMagic || request.version != kWireVersion ||
      request.request_id == 0 || request.payload_size > kMaxPayloadSize ||
      request.command == kResponseCommand) {
    dispatched.status = plugin_ui::Status::InvalidEncoding;
    result.close_connection = true;
  } else {
    dispatched = session_.dispatch(
        request.command,
        std::span<const uint8_t>(request.payload, request.payload_size));
  }

  if (dispatched.data.size() > kMaxPayloadSize - kResponseHeaderSize) {
    dispatched = {plugin_ui::Status::InvalidEncoding, {}};
    result.close_connection = true;
  }
  write_u32(result.response.payload,
            static_cast<uint32_t>(wire_status(dispatched.status)));
  write_u32(result.response.payload + 4,
            static_cast<uint32_t>(dispatched.data.size()));
  std::copy(dispatched.data.begin(), dispatched.data.end(),
            result.response.payload + kResponseHeaderSize);
  result.response.payload_size = static_cast<uint32_t>(
      kResponseHeaderSize + dispatched.data.size());
  return result;
}

} // namespace onion::plugin_session
