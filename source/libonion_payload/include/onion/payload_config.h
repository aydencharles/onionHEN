#pragma once

#include <onion/payload_identity.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_PAYLOAD_PRIORITY_MAX 999
#define ONION_PAYLOAD_DELAY_MAX 300

typedef struct OnionPayloadConfig {
  int priority;
  int delay_seconds;
} OnionPayloadConfig;

/* Read <accessible ELF path>.json. Missing files use defaults and succeed;
 * invalid/unreadable files return false with defaults in out. */
bool onion_payload_config_load(const char *path, OnionPayloadConfig *out);
bool onion_payload_config_save(const char *path, const OnionPayloadConfig *config);

/* Descending priority, then ascending basename and canonical absolute path. */
int onion_payload_compare(int priority_a, const char *path_a,
                          int priority_b, const char *path_b);

#ifdef __cplusplus
}
#endif
