/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <onion/ipc_server.hpp>
#include <onion/log.h>
#include <onion/system_tmp.h>

#include "onion_cjson.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace onion {
namespace {

struct ClientThreadArgs {
  IpcClientArgs *client;
  IpcCommandHandler handler;
  const char *tag;
};

void *client_thread(void *arg) {
  auto *pack = static_cast<ClientThreadArgs *>(arg);
  IpcClientArgs *client = pack->client;
  IpcCommandHandler handler = pack->handler;
  const char *tag = pack->tag ? pack->tag : "ipc";
  delete pack;

  LOG_DEBUG("[%s] Thread created for Socket %i", tag, client->socket);

  while (true) {
    IPCMessage ipcMessage{};
    const int readSize = ipc_network_recv_full(client->socket, &ipcMessage,
                                               static_cast<int32_t>(sizeof(ipcMessage)));
    if (readSize <= 0) {
      break;
    }
    if (!ipc_frame_is_complete(readSize)) {
      LOG_DEBUG("[%s][client %i] short frame %d (want %zu)", tag, client->cl_nmb,
           readSize, sizeof(ipcMessage));
      break;
    }
    ipc_message_force_nul(ipcMessage);

    if (ipcMessage.magic == static_cast<int>(0xDEADBABE)) {
      std::string message = ipcMessage.msg;
      if (handler) {
        handler(client, message, ipcMessage.cmd);
      }
    } else {
      LOG_ERROR("[%s][client %i] Invalid magic number", tag, client->cl_nmb);
      ipcMessage.error = -1;
      ipc_network_send_full(client->socket, &ipcMessage,
                            static_cast<int32_t>(sizeof(ipcMessage)));
    }
  }

  LOG_DEBUG("[%s][client %i] IPC Connection disconnected, Shutting down ...", tag,
       client->cl_nmb);
  ipc_network_close(client->socket);
  delete client;
  return nullptr;
}

} // namespace

int ipc_network_listen(const char *soc_path) {
  if (!soc_path) {
    return INVAIL;
  }
  mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  mkdir(ONION_SYSTEM_TMP_IPC_ROOT, 0777);
  unlink(soc_path);
  LOG_DEBUG("[ipc] Deleted Socket %s", soc_path);

  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) {
    LOG_ERROR("[ipc] Socket failed! %s", strerror(errno));
    return INVAIL;
  }

  sockaddr_un server{};
  server.sun_family = AF_UNIX;
  strncpy(server.sun_path, soc_path, sizeof(server.sun_path) - 1);

  if (bind(s, reinterpret_cast<struct sockaddr *>(&server), SUN_LEN(&server)) <
      0) {
    LOG_ERROR("[ipc] Bind failed! %s", strerror(errno));
    close(s);
    return INVAIL;
  }

  if (listen(s, 100) < 0) {
    LOG_ERROR("[ipc] listen failed! %s", strerror(errno));
    close(s);
    return INVAIL;
  }
  return s;
}

int ipc_network_accept(int socket_fd) {
  return accept(socket_fd, nullptr, nullptr);
}

int ipc_unix_connect(const char *path) {
  if (!path || !path[0])
    return -1;

  const int soc = socket(AF_UNIX, SOCK_STREAM, 0);
  if (soc < 0)
    return -1;

  sockaddr_un server{};
  server.sun_family = AF_UNIX;
  strncpy(server.sun_path, path, sizeof(server.sun_path) - 1);
  if (connect(soc, reinterpret_cast<struct sockaddr *>(&server),
              SUN_LEN(&server)) == -1) {
    close(soc);
    return -1;
  }
  return soc;
}

int ipc_network_recv(int socket_fd, void *buffer, int32_t size) {
  int n = recv(socket_fd, buffer, size, 0);
  LOG_DEBUG("got %i bytes", n);
  return n;
}

int ipc_network_recv_full(int socket_fd, void *buffer, int32_t size) {
  if (!buffer || size <= 0) {
    return -1;
  }
  auto *p = static_cast<char *>(buffer);
  int32_t got = 0;
  while (got < size) {
    const int n = recv(socket_fd, p + got, static_cast<size_t>(size - got), 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG_ERROR("recv_full error: %s", strerror(errno));
      return -1;
    }
    if (n == 0) {
      LOG_DEBUG("recv_full EOF after %d / %d", got, size);
      return got;
    }
    got += n;
  }
  LOG_TRACE("got %i bytes (full frame)", got);
  return got;
}

int ipc_network_send(int socket_fd, void *buffer, int32_t size) {
  return send(socket_fd, buffer, size, MSG_NOSIGNAL);
}

int ipc_network_send_full(int socket_fd, const void *buffer, int32_t size) {
  if (!buffer || size <= 0) {
    return -1;
  }
  const auto *p = static_cast<const char *>(buffer);
  int32_t sent = 0;
  while (sent < size) {
    const int n =
        send(socket_fd, p + sent, static_cast<size_t>(size - sent), MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (n == 0) {
      return sent;
    }
    sent += n;
  }
  return sent;
}

int ipc_network_close(int socket_fd) { return close(socket_fd); }

std::string ipc_json_escape(const std::string &in) {
  const std::string quoted =
      onion_cjson::print_owned(cJSON_CreateString(in.c_str()));
  if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"') {
    return {};
  }
  return quoted.substr(1, quoted.size() - 2);
}

std::string ipc_format_reply_body(bool error, const std::string &out_var) {
  cJSON *root = cJSON_CreateObject();
  if (!root || !cJSON_AddNumberToObject(root, "res", error ? -1 : 0) ||
      !cJSON_AddStringToObject(root, "var", out_var.c_str())) {
    cJSON_Delete(root);
    return {};
  }
  return onion_cjson::print_owned(root);
}

void ipc_reply(int sender_socket, DaemonCommands reply_cmd, bool error,
               const std::string &out_var) {
  std::string body = ipc_format_reply_body(error, out_var);
  if (body.empty() || body.size() >= DAEMON_BUFF_MAX) {
    error = true;
    body = ipc_format_reply_body(true, "IPC response too large");
  }

  IPCMessage outputMessage{};
  outputMessage.magic = 0xDEADBABE;
  outputMessage.cmd = reply_cmd;
  outputMessage.error = error ? -1 : 0;
  bzero(outputMessage.msg, sizeof(outputMessage.msg));
  memcpy(outputMessage.msg, body.data(), body.size());
  ipc_message_force_nul(outputMessage);

  LOG_ERROR("error: %d", outputMessage.error);
  ipc_network_send_full(sender_socket, &outputMessage,
                        static_cast<int32_t>(sizeof(outputMessage)));
}

void *ipc_server_loop(void *options_ptr) {
  auto *opts = static_cast<IpcServerOptions *>(options_ptr);
  if (!opts || !opts->socket_path || !opts->handler) {
    LOG_ERROR("[ipc] invalid server options");
    return nullptr;
  }

  const char *tag = opts->tag ? opts->tag : "ipc";
  const bool stoppable = (opts->running != nullptr);
  int cli_new = 0;

  while (!stoppable || opts->running->load()) {
    int serverSocket = ipc_network_listen(opts->socket_path);
    if (serverSocket < 0) {
      LOG_ERROR("[%s] networkListen error %s", tag, strerror(errno));
      LOG_DEBUG("rest: [%s] listen failed path=%s errno=%d", tag,
                opts->socket_path, errno);
      if (stoppable && !opts->running->load())
        break;
      sleep(1);
      continue;
    }
    if (opts->server_fd)
      opts->server_fd->store(serverSocket);
    LOG_DEBUG("rest: [%s] listening path=%s fd=%d", tag, opts->socket_path,
              serverSocket);

    int clientSocket;
    while ((clientSocket = ipc_network_accept(serverSocket)) >= 0) {
      LOG_DEBUG("[%s] Connection Accepted cl_nmb %i", tag, cli_new);

      auto *client = new IpcClientArgs();
      client->ip = "localhost";
      client->socket = clientSocket;
      client->cl_nmb = cli_new;

      auto *pack = new ClientThreadArgs{client, opts->handler, tag};
      pthread_t thr{};
      if (pthread_create(&thr, nullptr, client_thread, pack) != 0) {
        LOG_ERROR("[%s] pthread_create failed", tag);
        ipc_network_close(clientSocket);
        delete client;
        delete pack;
        continue;
      }
      if (opts->detach_clients) {
        pthread_detach(thr);
      }
      cli_new++;
    }

    // accept() failed: the listener is gone (e.g. after a standby resume the
    // socket may be dead). Re-listen to restore service unless a permanent
    // stop was requested. ipc_release_listen_fd owns the close so a concurrent
    // restart cannot double-close.
    LOG_DEBUG("rest: [%s] accept failed errno=%d (%s); re-listening", tag,
              errno, strerror(errno));
    if (opts->server_fd)
      ipc_release_listen_fd(opts->server_fd);
    else
      ipc_network_close(serverSocket);
    if (stoppable && !opts->running->load())
      break;
    LOG_WARN("[%s] networkAccept error %s; re-listening", tag, strerror(errno));
    sleep(1);
  }

  return nullptr;
}

void ipc_server_stop(IpcServerOptions *opts) {
  if (!opts)
    return;
  if (opts->running)
    opts->running->store(false);
  ipc_server_restart(opts);
}

void ipc_release_fd(std::atomic<int> *fd) {
  if (!fd)
    return;
  const int s = fd->exchange(-1);
  if (s >= 0) {
    LOG_DEBUG("rest: release listen fd=%d", s);
    shutdown(s, SHUT_RDWR);
    ipc_network_close(s);
  }
}

void ipc_release_listen_fd(std::atomic<int> *fd) { ipc_release_fd(fd); }

void ipc_server_restart(IpcServerOptions *opts) {
  if (!opts)
    return;
  const char *tag = opts->tag ? opts->tag : "ipc";
  LOG_DEBUG("rest: [%s] ipc_server_restart", tag);
  ipc_release_listen_fd(opts->server_fd);
}

} // namespace onion
