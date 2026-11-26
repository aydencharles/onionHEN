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

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string>
#include <msg.hpp>
#include <onion/ipc_server.hpp>
// clientArgs = onion::IpcClientArgs (see ipc_server.hpp)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#ifdef __cplusplus
#define restrict // Define restrict as empty for C++
#endif

extern "C"
{
#define ENTRYPOINT_OFFSET 0x70

#define PROCESS_LAUNCHED 1

#define LOOB_BUILDER_SIZE 21
#define LOOP_BUILDER_TARGET_OFFSET 3
#define USLEEP_NID "QcteRwbsnV0"
#include <ps5/kernel.h>

}

typedef struct {
  uint64_t pad0;
  char version_str[0x1C];
  uint32_t version;
  uint64_t pad1;
} OrbisKernelSwVersion;
extern "C" int sceKernelGetProsperoSystemSwVersion(OrbisKernelSwVersion *sw);

void startMessageReceiver();
bool notifyHandlers(const uint32_t prefix, const pid_t pid, const bool isHomebrew) noexcept;
bool hasPrefixHandler(const uint32_t prefix) noexcept;
void* messageThread(void*);
// touch_file: libonion_platform (onion/fs.h)
void *IPC_loop(void *args);