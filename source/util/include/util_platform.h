#pragma once

/*
 * Shared util-platform helpers used by cheats and other daemons.
 * Prefer these over duplicating sysctl/dynlib/file logic per feature.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- firmware (Prospero SW version; not kern.sdk_version) --------------- */

/**
 * Firmware major from sceKernelGetProsperoSystemSwVersion (value >> 16).
 * Used for runtime feature gates (e.g. mdbg vs kdirect). Distinct from
 * libhijacker getSystemSwVersion() which reads kern.sdk_version for offsets.
 */
uint32_t util_system_fw_major(void);

/* ---- file I/O ----------------------------------------------------------- */

/** Read entire file into malloc'd buffer (+NUL). Caller frees *buf_out. */
int util_file_read_alloc(const char *path, char **buf_out, size_t *size_out,
                         size_t max_size);

/* ---- game / process context for cheats --------------------------------- */

typedef struct {
  int pid;
  int appid;
  char title_id[16];
  char version[32];
  char platform[16]; /* "ps4" / "ps5" / "unknown" */
  char process_name[64];
} game_context_t;

void util_game_platform_from_title_id(const char *title_id, char *out,
                                      size_t out_size);

/**
 * Resolve installed app version for title_id (param.json / param.sfo).
 * Same discovery paths as BREW_UTIL_GET_GAME_VER.
 */
int util_resolve_game_version(const char *title_id, char *out, size_t out_size);

/**
 * Fill context for the current BigApp (pid/appid/title/process).
 * version is resolved when possible; otherwise "unknown".
 */
int util_get_running_bigapp(game_context_t *out);

/*
 * Dynlib module info — layout matches NineS module_info_t / SYS_dl_get_info_2.
 * Kept here so C++ util targets need not pull NineS freebsd-helper headers.
 */
#define UTIL_MODULE_INFO_NAME_LENGTH 128
#define UTIL_MODULE_INFO_SANDBOXED_PATH_LENGTH 1024
#define UTIL_MODULE_INFO_MAX_SECTIONS 4
#define UTIL_FINGERPRINT_LENGTH 20

typedef struct {
  uint64_t vaddr;
  uint64_t size;
  uint32_t prot;
} util_module_section_t;

typedef struct {
  char filename[UTIL_MODULE_INFO_NAME_LENGTH];
  uint64_t handle;
  uint8_t unknown0[32];
  uint64_t init;
  uint64_t fini;
  uint64_t eh_frame_hdr;
  uint64_t eh_frame_hdr_sz;
  uint64_t eh_frame;
  uint64_t eh_frame_sz;
  util_module_section_t sections[UTIL_MODULE_INFO_MAX_SECTIONS];
  uint8_t unknown7[1176];
  uint8_t fingerprint[UTIL_FINGERPRINT_LENGTH];
  uint32_t unknown8;
  char libname[UTIL_MODULE_INFO_NAME_LENGTH];
  uint32_t unknown9;
  char sandboxed_path[UTIL_MODULE_INFO_SANDBOXED_PATH_LENGTH];
  uint64_t sdk_version;
} util_module_info_t;

/**
 * Find module in pid via NineS get_module_info, then eboot imagebase fallback
 * for eboot.bin / process name when dynlib list is incomplete.
 */
int util_find_module(pid_t pid, const char *module_name,
                     util_module_info_t *out);

/** Scan processes of appid for a module; sets *pid_out on success. */
int util_find_module_in_app(int appid, const char *module_name, pid_t *pid_out,
                            util_module_info_t *out);

#ifdef __cplusplus
}
#endif
