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
#include <stdio.h>
#include <ps5/payload.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ini.h>
#include <poll.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include "faulthandler.h"
#include <pthread.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/types.h>
#include <ifaddrs.h>

/*================== Networking =================*/
/* SceNetCtlInfo / sceNetCtlGetInfo: shared ABI header. */
#include <ps5/net_ctl.h>
/*================== Networking =================*/

typedef struct notify_request
{
	char useless1[45];
	char message[3075];
} notify_request_t;

/*================ SETTINGS ==============*/
/* Persisted keys: onion::SettingsStore. Concurrent CMD flags stay atomic.
 * Always C++ linkage even when this header is included under extern "C". */
#ifdef __cplusplus
extern "C++" {
#include <onion/settings.hpp>
extern onion::SettingsStore g_settings;
/** Reload twin config into g_settings; missing file applies defaults (true). */
bool LoadSettings();
} /* extern "C++" */
#endif
/*================ SETTINGS ==============*/
/* Payload load helpers (shared onion_payload). */
#include <onion/payload.h>

/* load_payload: see extern "C" block below */

extern atomic_bool g_running;

typedef struct
{
	int32_t type;			 // 0x00
	int32_t req_id;			 // 0x04
	int32_t priority;		 // 0x08
	int32_t msg_id;			 // 0x0C
	int32_t target_id;		 // 0x10
	int32_t user_id;		 // 0x14
	int32_t unk1;			 // 0x18
	int32_t unk2;			 // 0x1C
	int32_t app_id;			 // 0x20
	int32_t error_num;		 // 0x24
	int32_t unk3;			 // 0x28
	char use_icon_image_uri; // 0x2C
	char message[1024];		 // 0x2D
	char uri[1024];			 // 0x42D
	char unkstr[1024];		 // 0x82D
} OrbisNotificationRequest;	 // Size = 0xC30


#include <onion/lnc.h>

bool copyFile(const char *source, const char *destination);

int32_t sceKernelSendNotificationRequest(int32_t device, OrbisNotificationRequest *req, size_t size, int32_t blocking);

/* Always C linkage (impl in common_utils.c) regardless of include context. */
#ifdef __cplusplus
extern "C" {
#endif
/**
 * Read the console IP.
 *
 * @param size must be >= ONION_NET_IP_ADDRESS_SIZE
 * @return 0 on success, -1 otherwise (the buffer still receives a value)
 */
int get_ip_address(char *ip_address, size_t size);
#include <onion/net.h>
#include <onion/platform.h>
#include <onion/proc_query.h>
int sceNetCtlInit(void);
int sceUserServiceInitialize(void *ptr);
bool load_payload(const char *path);
bool load_payload_with_key(const char *path, const char *key);
#ifdef __cplusplus
}
#endif
