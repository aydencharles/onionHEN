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

#include <onion/platform.h>
/* C++ headers before common_utils.h: the latter pulls stdatomic.h, which
 * breaks libc++ <atomic> (pulled transitively via onion/settings.hpp) if
 * included first. */
#include <onion/settings.hpp>
#include "common_utils.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

namespace {

// Console IP as last observed, used only to notify the user on change.
char g_ip_address[ONION_NET_IP_ADDRESS_SIZE];
pthread_mutex_t g_ip_lock = PTHREAD_MUTEX_INITIALIZER;

} // namespace

void check_addr_change(void) {
  pthread_mutex_lock(&g_ip_lock);

  char current[ONION_NET_IP_ADDRESS_SIZE];
  if (get_ip_address(current, sizeof(current)) < 0) {
    pthread_mutex_unlock(&g_ip_lock);
    return;
  }

  if (strcmp(g_ip_address, current) != 0) {
    onion_notify(true, "notify.net.ip_changed", current);
    strncpy(g_ip_address, current, sizeof(g_ip_address));
  }

  pthread_mutex_unlock(&g_ip_lock);
}

void *ip_thread(void *arg) {
  (void)arg;
  do {
    sleep(1);
  } while (get_ip_address(g_ip_address, sizeof(g_ip_address)) < 0);

  while (true) {
    check_addr_change();
    sleep(2);
  }
}

/** Starts and detaches the optional network IP-change watcher. */
void start_ip_thread(void) {
  pthread_t ip_thread_thr;
  const int create_result =
      pthread_create(&ip_thread_thr, NULL, ip_thread, NULL);
  if (create_result != 0) {
    LOG_WARN("failed to start network IP watcher thread rc=%d", create_result);
    return;
  }

  const int detach_result = pthread_detach(ip_thread_thr);
  if (detach_result != 0) {
    LOG_WARN("failed to detach network IP watcher thread rc=%d", detach_result);
  }
}
