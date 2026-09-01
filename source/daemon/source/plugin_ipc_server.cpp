#include "plugin_ipc_server.hpp"

#include <onion/ipc_server.hpp>
#include <onion/log.h>
#include <onion/system_tmp.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <new>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_set>

namespace onion::daemon::plugin_ipc {
namespace {

constexpr size_t kMaxClients = 32;

struct ClientArgs {
  int socket = -1;
  unsigned number = 0;
};

std::atomic_bool g_running{false};
std::atomic_bool g_started{false};
std::atomic<int> g_listener{-1};
std::atomic<unsigned> g_next_client{0};
std::mutex g_clients_mutex;
std::unordered_set<int> g_clients;
plugin_ui::Registry g_registry;
plugin_ui::ProtocolBroker g_ui_broker(g_registry);
plugin_session::SessionDirectory g_sessions;

void forget_client(int socket) {
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  g_clients.erase(socket);
}

void shutdown_clients() {
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  for (int socket : g_clients) (void)shutdown(socket, SHUT_RDWR);
}

void close_listener(int expected) {
  if (g_listener.compare_exchange_strong(expected, -1))
    ipc_network_close(expected);
}

void *client_thread(void *opaque) {
  auto *args = static_cast<ClientArgs *>(opaque);
  const int socket = args->socket;
  const unsigned number = args->number;
  delete args;

  LOG_DEBUG("[plugin-ipc][client %u] connected fd=%d", number, socket);
  serve_connection(socket, g_sessions, g_ui_broker);
  forget_client(socket);
  ipc_network_close(socket);
  LOG_DEBUG("[plugin-ipc][client %u] disconnected", number);
  return nullptr;
}

void *server_thread(void *) {
  while (g_running.load(std::memory_order_acquire)) {
    const int listener = ipc_network_listen(ONION_SYSTEM_TMP_PLUGIN_SOCKET);
    if (listener < 0) {
      LOG_ERROR("[plugin-ipc] listen failed: %s", strerror(errno));
      if (!g_running.load(std::memory_order_acquire)) break;
      sleep(1);
      continue;
    }
    g_listener.store(listener, std::memory_order_release);
    LOG_INFO("[plugin-ipc] listening on %s", ONION_SYSTEM_TMP_PLUGIN_SOCKET);

    while (g_running.load(std::memory_order_acquire)) {
      const int client = ipc_network_accept(listener);
      if (client < 0) break;
      if (!g_running.load(std::memory_order_acquire)) {
        ipc_network_close(client);
        break;
      }

      bool accepted = false;
      {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        if (g_clients.size() < kMaxClients) {
          g_clients.insert(client);
          accepted = true;
        }
      }
      if (!accepted) {
        LOG_WARN("[plugin-ipc] connection limit reached");
        ipc_network_close(client);
        continue;
      }
      auto *args = new (std::nothrow)
          ClientArgs{client, g_next_client.fetch_add(1)};
      pthread_t thread{};
      if (!args || pthread_create(&thread, nullptr, client_thread, args) != 0) {
        LOG_ERROR("[plugin-ipc] failed to create client thread");
        delete args;
        forget_client(client);
        ipc_network_close(client);
        continue;
      }
      pthread_detach(thread);
    }

    close_listener(listener);
    if (!g_running.load(std::memory_order_acquire)) break;
    LOG_WARN("[plugin-ipc] listener interrupted; re-listening");
    sleep(1);
  }

  g_started.store(false, std::memory_order_release);
  return nullptr;
}

} // namespace

void serve_connection(int socket, plugin_session::SessionDirectory &directory,
                      plugin_ui::ProtocolBroker &ui_broker) {
  plugin_session::ConnectionProtocol protocol(directory, ui_broker);
  while (true) {
    plugin_session::Frame request;
    const int received = ipc_network_recv_full(
        socket, &request, static_cast<int32_t>(sizeof(request)));
    if (received != static_cast<int>(sizeof(request))) break;

    const plugin_session::FrameResult result = protocol.handle(request);
    if (ipc_network_send_full(socket, &result.response,
                              static_cast<int32_t>(sizeof(result.response))) !=
        static_cast<int>(sizeof(result.response))) {
      break;
    }
    if (result.close_connection) break;
  }
}

bool start() {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) return true;
  g_running.store(true, std::memory_order_release);
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, server_thread, nullptr) != 0) {
    g_running.store(false, std::memory_order_release);
    g_started.store(false, std::memory_order_release);
    return false;
  }
  pthread_detach(thread);
  return true;
}

void stop() {
  g_running.store(false, std::memory_order_release);
  ipc_release_listen_fd(&g_listener);
  shutdown_clients();
  unlink(ONION_SYSTEM_TMP_PLUGIN_SOCKET);
}

void restart() {
  if (!g_running.load(std::memory_order_acquire)) return;
  ipc_release_listen_fd(&g_listener);
  shutdown_clients();
}

bool is_listening() {
  return g_listener.load(std::memory_order_acquire) >= 0;
}

} // namespace onion::daemon::plugin_ipc
