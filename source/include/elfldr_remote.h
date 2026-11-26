/* OnionHEN: launch ELFs via elfldr sockets.
 *
 * 9020 is OnionHEN's private runtime loader. 9021 is the legacy/external
 * ps5-payload-dev elfldr port reserved for bootstrap and loader recovery.
 * User payload policy is strict 9020-only and requires an exact PID response.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ELFLDR_REMOTE_PORT
#define ELFLDR_REMOTE_PORT 9021
#endif

#ifndef ONION_ELFLDR_PORT
#define ONION_ELFLDR_PORT 9020
#endif

#define ONION_ELFLDR_PING "onion:ping"
#define ONION_ELFLDR_PONG "OK onion_elfldr 9020\n"

/**
 * True if something accepts connections on 127.0.0.1:@port.
 */
bool elfldr_remote_available_on(uint16_t port);

/**
 * True only if OnionHEN's private embedded loader answers on 127.0.0.1:9020.
 */
bool elfldr_remote_onion_available(void);

/**
 * True if something accepts connections on 127.0.0.1:9021 (typically elfldr.elf).
 */
bool elfldr_remote_available(void);

/**
 * Send raw ELF bytes to @port (process name becomes "payload.elf" unless the
 * payload renames itself).
 */
bool elfldr_remote_send_bytes_to(uint16_t port, const uint8_t *elf,
                                 size_t size);

/**
 * Stage @elf at @abs_path, launch it exclusively through OnionHEN :9020, and
 * wait for its exact PID. Returns the loader-reported PID (>1), or -1 for any
 * staging, transport, protocol, timeout, spawn, or invalid-PID failure.
 */
pid_t elfldr_remote_onion_write_and_launch_get_pid(const char *abs_path,
                                                   const uint8_t *elf,
                                                   size_t size);

/**
 * Launch an ELF already present at @abs_path through the private loader.
 * @args is an optional whitespace-separated argv string. Returns the exact
 * loader-reported PID (>1), or -1 on transport/protocol/spawn failure.
 */
pid_t elfldr_remote_onion_launch_file_get_pid(const char *abs_path,
                                              const char *args);

/**
 * Stage @elf at @abs_path and launch it through the private loader with
 * optional command-line @args. The staged file is left in place; callers that
 * use a transient runtime path may unlink it after this function returns.
 */
pid_t elfldr_remote_onion_write_and_launch_get_pid_with_args(
    const char *abs_path, const uint8_t *elf, size_t size, const char *args);

#ifdef __cplusplus
}
#endif
