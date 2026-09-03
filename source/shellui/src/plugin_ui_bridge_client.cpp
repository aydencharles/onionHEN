#include "plugin_ui_bridge_client.hpp"

#include "dynamic_ui_runtime.hpp"

#include <onion/ipc_server.hpp>
#include <onion/log.h>
#include <onion/plugin_ui_bridge.hpp>
#include <onion/system_tmp.h>

#include <atomic>
#include <mutex>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace onion::shellui::plugin_ui_bridge {
namespace {

std::atomic_bool g_started{false};
std::atomic<int> g_socket{-1};
std::mutex g_socket_mutex;

bool send_action(plugin_ui::Handle handle, const plugin_ui::Node &node,
                 std::string_view value, void *) {
  std::vector<uint8_t> payload;
  if (!onion::plugin_bridge::encode_action({handle, node.id,
                                             std::string(value)},
                                            payload))
    return false;

  uint8_t header[onion::plugin_bridge::kFrameHeaderSize];
  if (!onion::plugin_bridge::encode_frame_header(
          {onion::plugin_bridge::MessageType::Action,
           static_cast<uint32_t>(payload.size())},
          header))
    return false;

  std::lock_guard<std::mutex> lock(g_socket_mutex);
  const int socket = g_socket.load(std::memory_order_acquire);
  if (socket < 0) return false;
  const bool sent =
      ipc_network_send_full(socket, header, sizeof(header)) ==
          static_cast<int>(sizeof(header)) &&
      ipc_network_send_full(socket, payload.data(),
                            static_cast<int32_t>(payload.size())) ==
          static_cast<int>(payload.size());
  if (!sent) (void)shutdown(socket, SHUT_RDWR);
  return sent;
}

void close_socket(int expected) {
  std::lock_guard<std::mutex> lock(g_socket_mutex);
  if (g_socket.compare_exchange_strong(expected, -1)) {
    (void)shutdown(expected, SHUT_RDWR);
    ipc_network_close(expected);
  }
}

bool receive_snapshots(int socket) {
  while (true) {
    uint8_t header_bytes[onion::plugin_bridge::kFrameHeaderSize];
    if (ipc_network_recv_full(socket, header_bytes, sizeof(header_bytes)) !=
        static_cast<int>(sizeof(header_bytes)))
      return false;

    onion::plugin_bridge::FrameHeader header;
    if (!onion::plugin_bridge::decode_frame_header(header_bytes, header) ||
        header.type != onion::plugin_bridge::MessageType::Snapshot)
      return false;

    std::vector<uint8_t> payload(header.payload_size);
    if (!payload.empty() &&
        ipc_network_recv_full(socket, payload.data(),
                              static_cast<int32_t>(payload.size())) !=
            static_cast<int>(payload.size()))
      return false;

    plugin_ui::RegistrySnapshot snapshot;
    std::string error;
    if (!onion::plugin_bridge::decode_snapshot(payload, snapshot, &error)) {
      LOG_WARN("[plugin-bridge] rejected daemon snapshot: %s", error.c_str());
      return false;
    }
    dynamic_ui::replace_snapshot(std::move(snapshot));
  }
}

void *reader_thread(void *) {
  while (true) {
    const int socket =
        ipc_unix_connect(ONION_SYSTEM_TMP_SHELLUI_PLUGIN_SOCKET);
    if (socket < 0) {
      sleep(1);
      continue;
    }
    g_socket.store(socket, std::memory_order_release);
    LOG_INFO("[plugin-bridge] connected to daemon");
    (void)receive_snapshots(socket);
    close_socket(socket);
    dynamic_ui::replace_snapshot({});
    LOG_WARN("[plugin-bridge] daemon disconnected; reconnecting");
    sleep(1);
  }
  return nullptr;
}

} // namespace

bool start() {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) return true;
  dynamic_ui::set_action_sink(send_action, nullptr);
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, reader_thread, nullptr) != 0) {
    dynamic_ui::set_action_sink(nullptr, nullptr);
    g_started.store(false, std::memory_order_release);
    return false;
  }
  pthread_detach(thread);
  return true;
}

} // namespace onion::shellui::plugin_ui_bridge
