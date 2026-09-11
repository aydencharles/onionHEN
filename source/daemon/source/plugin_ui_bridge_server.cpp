#include "plugin_ui_bridge_server.hpp"

#include <onion/ipc_server.hpp>
#include <onion/log.h>
#include <onion/plugin_ui_bridge.hpp>
#include <onion/system_tmp.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace onion::daemon::plugin_ui_bridge {
namespace {

constexpr size_t kMaxQueuedEventsPerOwner = 64;

class EventQueue final : public plugin_session::EventSource {
public:
  void push(const plugin_ui::ActionEvent &event) {
    std::vector<uint8_t> encoded;
    if (!plugin_bridge::encode_ui_event(event, encoded)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto &queue = by_owner_[event.owner];
    if (queue.size() >= kMaxQueuedEventsPerOwner) queue.pop_front();
    queue.push_back(std::move(encoded));
  }

  plugin_ui::WireResponse poll(std::string_view owner) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = by_owner_.find(std::string(owner));
    if (found == by_owner_.end() || found->second.empty())
      return {plugin_ui::Status::NotFound, {}};
    std::vector<uint8_t> event = std::move(found->second.front());
    found->second.pop_front();
    if (found->second.empty()) by_owner_.erase(found);
    return {plugin_ui::Status::Ok, std::move(event)};
  }

  void disconnect(std::string_view owner) override {
    std::lock_guard<std::mutex> lock(mutex_);
    by_owner_.erase(std::string(owner));
  }

private:
  std::mutex mutex_;
  std::map<std::string, std::deque<std::vector<uint8_t>>> by_owner_;
};

std::atomic_bool g_running{false};
std::atomic_bool g_started{false};
std::atomic<int> g_listener{-1};
std::atomic<int> g_client{-1};
std::mutex g_publish_mutex;
std::mutex g_send_mutex;
std::mutex g_snapshot_mutex;
std::vector<uint8_t> g_snapshot;
uint64_t g_snapshot_generation = 0;
plugin_ui::ProtocolBroker *g_broker = nullptr;
EventQueue g_events;

bool send_message_locked(int socket, plugin_bridge::MessageType type,
                         std::span<const uint8_t> payload) {
  if (socket < 0 || payload.size() > plugin_bridge::kMaxPayloadSize) return false;
  uint8_t header_bytes[plugin_bridge::kFrameHeaderSize];
  if (!plugin_bridge::encode_frame_header(
          {type, static_cast<uint32_t>(payload.size())}, header_bytes))
    return false;
  return ipc_network_send_full(socket, header_bytes, sizeof(header_bytes)) ==
             static_cast<int>(sizeof(header_bytes)) &&
         (payload.empty() ||
          ipc_network_send_full(socket, payload.data(),
                                static_cast<int32_t>(payload.size())) ==
              static_cast<int>(payload.size()));
}

bool send_message(int socket, plugin_bridge::MessageType type,
                  std::span<const uint8_t> payload) {
  std::lock_guard<std::mutex> lock(g_send_mutex);
  return send_message_locked(socket, type, payload);
}

void publish_snapshot(const plugin_ui::RegistrySnapshot &snapshot, void *) {
  std::vector<uint8_t> encoded;
  std::string error;
  if (!plugin_bridge::encode_snapshot(snapshot, encoded, &error)) {
    LOG_ERROR("[plugin-bridge] snapshot encode failed: %s", error.c_str());
    return;
  }
  std::lock_guard<std::mutex> publish_lock(g_publish_mutex);
  {
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    if (snapshot.generation < g_snapshot_generation) return;
    g_snapshot = encoded;
    g_snapshot_generation = snapshot.generation;
  }
  const int client = g_client.load(std::memory_order_acquire);
  if (client >= 0 &&
      !send_message(client, plugin_bridge::MessageType::Snapshot, encoded))
    (void)shutdown(client, SHUT_RDWR);
}

bool attach_client(int client) {
  std::lock_guard<std::mutex> publish_lock(g_publish_mutex);
  std::vector<uint8_t> snapshot;
  {
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    snapshot = g_snapshot;
  }
  std::lock_guard<std::mutex> send_lock(g_send_mutex);
  if (!send_message_locked(client, plugin_bridge::MessageType::Snapshot,
                           snapshot))
    return false;
  g_client.store(client, std::memory_order_release);
  return true;
}

void handle_action(std::span<const uint8_t> payload) {
  plugin_bridge::ActionRequest action;
  if (!plugin_bridge::decode_action(payload, action) || !g_broker) {
    LOG_WARN("[plugin-bridge] rejected malformed ShellUI action");
    return;
  }
  plugin_ui::ActionEvent event;
  const plugin_ui::Status status = g_broker->dispatch_action(
      action.handle, action.node_id, action.value, event);
  if (status != plugin_ui::Status::Ok) {
    LOG_WARN("[plugin-bridge] action rejected handle=%llu node=%s status=%d",
             static_cast<unsigned long long>(action.handle),
             action.node_id.c_str(), static_cast<int>(status));
    return;
  }
  g_events.push(event);
}

void serve_client(int client) {
  while (g_running.load(std::memory_order_acquire)) {
    uint8_t header_bytes[plugin_bridge::kFrameHeaderSize];
    if (ipc_network_recv_full(client, header_bytes, sizeof(header_bytes)) !=
        static_cast<int>(sizeof(header_bytes)))
      break;
    plugin_bridge::FrameHeader header;
    if (!plugin_bridge::decode_frame_header(header_bytes, header) ||
        header.type != plugin_bridge::MessageType::Action)
      break;
    std::vector<uint8_t> payload(header.payload_size);
    if (!payload.empty() &&
        ipc_network_recv_full(client, payload.data(),
                              static_cast<int32_t>(payload.size())) !=
            static_cast<int>(payload.size()))
      break;
    handle_action(payload);
  }
}

void close_client(int expected) {
  std::lock_guard<std::mutex> lock(g_send_mutex);
  if (g_client.compare_exchange_strong(expected, -1)) {
    (void)shutdown(expected, SHUT_RDWR);
    ipc_network_close(expected);
  }
}

void release_client() {
  std::lock_guard<std::mutex> lock(g_send_mutex);
  ipc_release_listen_fd(&g_client);
}

void *server_thread(void *) {
  while (g_running.load(std::memory_order_acquire)) {
    const int listener =
        ipc_network_listen(ONION_SYSTEM_TMP_SHELLUI_PLUGIN_SOCKET);
    if (listener < 0) {
      if (!g_running.load(std::memory_order_acquire)) break;
      LOG_ERROR("[plugin-bridge] listen failed: %s", std::strerror(errno));
      sleep(1);
      continue;
    }
    g_listener.store(listener, std::memory_order_release);
    LOG_INFO("[plugin-bridge] listening on %s",
             ONION_SYSTEM_TMP_SHELLUI_PLUGIN_SOCKET);
    while (g_running.load(std::memory_order_acquire)) {
      const int client = ipc_network_accept(listener);
      if (client < 0) break;
      if (!attach_client(client)) {
        ipc_network_close(client);
        continue;
      }
      LOG_INFO("[plugin-bridge] ShellUI connected");
      serve_client(client);
      close_client(client);
      LOG_INFO("[plugin-bridge] ShellUI disconnected");
    }
    int expected = listener;
    if (g_listener.compare_exchange_strong(expected, -1))
      ipc_network_close(listener);
    if (g_running.load(std::memory_order_acquire)) sleep(1);
  }
  g_started.store(false, std::memory_order_release);
  return nullptr;
}

} // namespace

bool start(plugin_ui::ProtocolBroker &broker, plugin_ui::Registry &registry) {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) return true;
  g_broker = &broker;
  broker.set_snapshot_sink(publish_snapshot, nullptr);
  publish_snapshot(registry.snapshot(), nullptr);
  g_running.store(true, std::memory_order_release);
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, server_thread, nullptr) != 0) {
    g_running.store(false, std::memory_order_release);
    g_started.store(false, std::memory_order_release);
    broker.set_snapshot_sink(nullptr, nullptr);
    return false;
  }
  pthread_detach(thread);
  return true;
}

void stop() {
  g_running.store(false, std::memory_order_release);
  if (g_broker) g_broker->set_snapshot_sink(nullptr, nullptr);
  ipc_release_listen_fd(&g_listener);
  release_client();
  unlink(ONION_SYSTEM_TMP_SHELLUI_PLUGIN_SOCKET);
}

void restart() {
  if (!g_running.load(std::memory_order_acquire)) return;
  ipc_release_listen_fd(&g_listener);
  release_client();
}

bool is_listening() {
  return g_listener.load(std::memory_order_acquire) >= 0;
}

plugin_session::EventSource &event_source() { return g_events; }

} // namespace onion::daemon::plugin_ui_bridge
