#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "util_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Patch byte cap (fixed arrays in onion_patch_t). Cheat/patch counts are dynamic. */
#ifndef ONION_MAX_PATCH_BYTES
#define ONION_MAX_PATCH_BYTES 1024
#endif

/* File-level credit authors (GoldHEN credits / SHN Moder / ShnExt author). */
#ifndef ONION_MAX_AUTHORS
#define ONION_MAX_AUTHORS 16
#endif
#ifndef ONION_AUTHOR_NAME_LEN
#define ONION_AUTHOR_NAME_LEN 64
#endif

/* Section bounds (matches util_module_info_t / NineS util_module_info_t) */
#ifndef MODULE_INFO_MAX_SECTIONS
#define MODULE_INFO_MAX_SECTIONS UTIL_MODULE_INFO_MAX_SECTIONS
#endif

typedef struct {
  bool code_cave_reloc;
  bool absolute;
  bool is_asm; /* ShnExt may leave unassembled ASM text */
  int section;
  uint64_t offset;
  size_t on_len;
  size_t off_len;
  uint8_t on[ONION_MAX_PATCH_BYTES];
  uint8_t off[ONION_MAX_PATCH_BYTES];
} onion_patch_t;

typedef struct {
  char name[256];
  char description[256];
  char module_name[128];
  bool enabled;
  size_t patch_count;
  size_t patch_capacity;
  onion_patch_t *patches;
} onion_cheat_entry_t;

typedef struct {
  char name[256];
  char process[128];
  size_t author_count;
  char authors[ONION_MAX_AUTHORS][ONION_AUTHOR_NAME_LEN];
  size_t cheat_count;
  size_t cheat_capacity;
  int master_code_id;
  pid_t last_applied_pid;
  onion_cheat_entry_t *cheats;
} onion_cheat_file_t;

void onion_cheat_file_clear(onion_cheat_file_t *f);
/** Append unique non-empty author; returns 0 on success/duplicate, -1 if full/invalid. */
int onion_cheat_file_add_author(onion_cheat_file_t *f, const char *author);
int onion_cheat_file_ensure_cheat(onion_cheat_file_t *f);
int onion_cheat_entry_ensure_patch(onion_cheat_entry_t *e);

/* Load path: onion::cheats::CheatParserFactory / CheatRepository (C++ only). */

#ifdef __cplusplus
}
#endif
