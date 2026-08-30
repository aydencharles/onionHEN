#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cheats/cheat_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared C helpers for C++ parsers + ShnExt. Keep small; no orchestration. */

int onion_cheat_hex_decode(const char *hex, uint8_t *out, size_t max_len,
                           size_t *out_len);
char *onion_cheat_load_file_buffer(const char *path, long *size_out);
void onion_cheat_replace_all(char *text, size_t cap, const char *from,
                             const char *to);
void onion_cheat_xml_unescape(char *text);
void onion_cheat_secure_zero(void *ptr, size_t len);

/** ShnExt (deflate + AES + cJSON + optional keystone). Adapter: ShnExtCheatParser. */
int onion_cheat_parse_shnext_buffer(const char *data, size_t size,
                                    onion_cheat_file_t *out);

#ifdef __cplusplus
}
#endif
