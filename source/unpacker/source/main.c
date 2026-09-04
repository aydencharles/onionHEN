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

#include <onion/log.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "LzmaLib.h"
#include <elf.h>
#include <onion/conflict.h>
#include <unistd.h>

#define LZMA_CLI_HEADER_SIZE 13

typedef struct {
  int32_t type;             // 0x00
  int32_t req_id;           // 0x04
  int32_t priority;         // 0x08
  int32_t msg_id;           // 0x0C
  int32_t target_id;        // 0x10
  int32_t user_id;          // 0x14
  int32_t unk1;             // 0x18
  int32_t unk2;             // 0x1C
  int32_t app_id;           // 0x20
  int32_t error_num;        // 0x24
  int32_t unk3;             // 0x28
  char use_icon_image_uri;  // 0x2C
  char message[1024];       // 0x2D
  char uri[1024];           // 0x42D
  char unkstr[1024];        // 0x82D
} OrbisNotificationRequest; // Size = 0xC30

int sceKernelSendNotificationRequest(int32_t device,
                                     OrbisNotificationRequest *req, size_t size,
                                     int32_t blocking);

void notify(const char *text, ...) {
  OrbisNotificationRequest req;
  va_list args;

  memset(&req, 0, sizeof(req));

  // Process args
  va_start(args, text);
  vsnprintf(req.message, sizeof(req.message), text, args);
  va_end(args);

  req.type = 0;
  req.unk3 = 0;
  req.use_icon_image_uri = 1;
  req.target_id = -1;
  snprintf(req.uri, sizeof(req.uri), "cxml://psnotification/tex_icon_system");

  LOG_DEBUG("Notify: %s", req.message);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

#ifndef ONIONHEN_BOOTSTRAPPER_LZMA
#define ONIONHEN_BOOTSTRAPPER_LZMA "../../bin/bootstrapper.elf.lzma"
#endif
#ifndef ONIONHEN_BOOTSTRAPPER_SIZE
#define ONIONHEN_BOOTSTRAPPER_SIZE "../../bin/bootstrapper.elf.lzma.size"
#endif

__asm__(".intel_syntax noprefix\n"
        ".section .data\n"
        ".global onionhen_compressed\n"
        ".type   onionhen_compressed, @object\n"
        ".align  16\n"
        "onionhen_compressed:\n"
        ".incbin \"" ONIONHEN_BOOTSTRAPPER_LZMA "\"\n"
        "onionhen_compressed_end:\n"
        ".global onionhen_compressed_size\n"
        ".type  onionhen_compressed_size, @object\n"
        ".align  4\n"
        "onionhen_compressed_size:\n"
        ".int    onionhen_compressed_end - onionhen_compressed\n"
        ".global onionhen_decompressed_size\n"
        ".type   onionhen_decompressed_size, @object\n"
        ".align  16\n"
        "onionhen_decompressed_size:\n"
        ".incbin \"" ONIONHEN_BOOTSTRAPPER_SIZE "\"\n");

extern uint32_t onionhen_compressed_size;
extern uint8_t onionhen_compressed[];
extern uint8_t onionhen_compressed_end[];
extern uint8_t onionhen_decompressed_size[];

bool send_to_elfldr(const void* buffer, size_t buffer_size) {
    int sockfd = -1;
    struct sockaddr_in server_addr;
    int bytes_sent = 0;
    int total_sent = 0;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("Failed to create socket: %d", sockfd);
        return false;
    }

    // Set socket options (optional - for faster reuse)
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set up server address (always localhost)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9021);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to connect to localhost:9021");
        close(sockfd);
        return false;
    }

    LOG_DEBUG("Connected to localhost:9021");

    // Send all data in the buffer
    const char* data_ptr = (const char*)buffer;
    while (total_sent < buffer_size) {
        bytes_sent = send(sockfd, data_ptr + total_sent, buffer_size - total_sent, 0);
        if (bytes_sent <= 0) {
            LOG_ERROR("Failed to send data: %d", bytes_sent);
            close(sockfd);
            return false;
        }
        total_sent += bytes_sent;
    }

    LOG_DEBUG("Successfully sent %d bytes to localhost:9021", total_sent);

    // Close socket
    close(sockfd);

    return true;
}

int main() {
  const char *conflict = onion_conflict_detect();
  if (conflict) {
    LOG_ERROR("refusing start: %s already running", conflict);
    notify("OnionHEN refused to start: %s is already running", conflict);
    return 0;
  }

  if (onionhen_compressed_size <= 0) {
    LOG_ERROR("Invalid OnionHEN payload! unable to unpack it!");
    return 0;
  }

  size_t decompress_size = atoi((char *)onionhen_decompressed_size);
  // LOG_DEBUG("Decompressed size: %zu bytes\nCompressed: %d", size,
  // onionhen_compressed_size); LOG_DEBUG("Payload has %d bytes, decompressing...",
  // onionhen_compressed_size);
  uint8_t *decompressed = (uint8_t *)malloc(decompress_size);
  if (!decompressed) {
    notify("Failed to allocate memory for decompressed OnionHEN payload!");
    return -1;
  }
  size_t size = decompress_size;
  size_t srcLen = onionhen_compressed_size;

  //
  // The PROPS used by the LZMA is located at the first 5 bytes of the file, the
  // size of the whole LZMA cli header is 13 bytes we skip it to get the real
  // compressed data
  //
  int res = LzmaUncompress(decompressed, &size,
                           onionhen_compressed + LZMA_CLI_HEADER_SIZE, &srcLen,
                           onionhen_compressed, LZMA_PROPS_SIZE);
  if (res != 0) {
    notify("Failed to decompress OnionHEN payload! error: %d", res);
    free(decompressed);
    return -1;
  }

  LOG_DEBUG("Bootstrapping OnionHEN.elf...");

  /* Do not write OnionHEN.bin under /data/OnionHEN — keep the payload in RAM
   * only and hand it straight to elfldr :9021. */

  if(!send_to_elfldr(decompressed, decompress_size)) {
    notify("The elfldr on port 9021 is REQUIRED for OnionHEN make sure its running and try again!");
    free(decompressed);
    return -1;
  }

 
  free(decompressed);

  return 0;
}
