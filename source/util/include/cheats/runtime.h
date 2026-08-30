#pragma once

#include <stddef.h>

/* Flat cheat directory only. */
#ifndef ONION_DATA_ROOT
#define ONION_DATA_ROOT "/data/OnionHEN"
#endif
#ifndef ONION_CHEATS_DIR
#define ONION_CHEATS_DIR ONION_DATA_ROOT "/cheats"
#endif

#ifndef ONION_CHEAT_TITLE_ID_LEN
#define ONION_CHEAT_TITLE_ID_LEN 32
#endif
#ifndef ONION_CHEAT_VERSION_LEN
#define ONION_CHEAT_VERSION_LEN 64
#endif
#ifndef ONION_CHEAT_PROCESS_LEN
#define ONION_CHEAT_PROCESS_LEN 128
#endif
#ifndef ONION_CHEAT_SOURCE_ID_LEN
#define ONION_CHEAT_SOURCE_ID_LEN 16
#endif
#ifndef ONION_CHEAT_SUFFIX_LEN
#define ONION_CHEAT_SUFFIX_LEN 128
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pieces of a recognized cheat filename.
 *
 * Online dumps use TITLEID_VERSION[_PROCESS][_SOURCE_ID].ext. process and
 * source_id are empty when that segment is omitted. suffix is the raw
 * remainder after the version (author, process, source ID, or a combination).
 */
typedef struct onion_cheat_filename {
  char title_id[ONION_CHEAT_TITLE_ID_LEN];
  char version[ONION_CHEAT_VERSION_LEN];
  char process[ONION_CHEAT_PROCESS_LEN];
  char source_id[ONION_CHEAT_SOURCE_ID_LEN];
  char suffix[ONION_CHEAT_SUFFIX_LEN];
  int extension_rank;
} onion_cheat_filename_t;

/** Canonical cheat extension for a load-priority rank; NULL if out of range. */
const char *onion_cheat_extension_for_rank(int rank);

/**
 * Return the load-priority rank for a recognized filename extension.
 * Writes the extension's leading-dot offset when @p extension_start is set.
 * Returns -1 when the filename has no supported extension.
 */
int onion_cheat_extension_rank(const char *name, size_t *extension_start);

/**
 * If @p name ends with a known cheat extension (.json/.shn/.mc4/.ShnExt),
 * write the format tag ("json", "shn", "mc4", "ShnExt") to @p ext_out and
 * return 1. Otherwise return 0.
 */
int onion_cheat_match_ext(const char *name, char *ext_out, size_t ext_out_size);

/**
 * Split a cheat filename into title, version, optional process, optional
 * 8-hex source ID, raw suffix, and extension rank. Title id is uppercased;
 * source ID is lowercased. Accepts an optional game-name prefix before the
 * title id.
 * Returns 0 on success.
 */
int onion_cheat_parse_filename(const char *filename,
                               onion_cheat_filename_t *out);

/** True when @p value is an 8-digit hexadecimal source ID. */
int onion_cheat_is_source_id(const char *value);

/** True when @p process is the default eboot / eboot.bin process. */
int onion_cheat_is_eboot_process(const char *process);

/**
 * True when @p suffix is a website/legacy eboot alias (source ID, author, eboot)
 * rather than a real non-eboot process scope such as worker.bin or
 * default.elf.
 */
int onion_cheat_is_legacy_eboot_alias(const char *suffix);

/**
 * True when @p parts is a legal match for the running process.
 * Compatibility uses title/version/process only; source ID is identity.
 */
int onion_cheat_filename_compatible(const onion_cheat_filename_t *parts,
                                    const char *process);

/**
 * Rank two compatible filenames. Negative if @p lhs is a better match.
 * Order: process-scoped, generic TITLE_VER, source-ID/author eboot alias;
 * then extension rank; then name.
 */
int onion_cheat_filename_compare(const onion_cheat_filename_t *lhs,
                                 const char *lhs_name,
                                 const onion_cheat_filename_t *rhs,
                                 const char *rhs_name);

/**
 * Sanitize a filename token: keep ASCII alnum . _ -;
 * replace other characters with '_'. Empty/NULL → empty string.
 */
void onion_cheat_normalize_filename_token(const char *value, char *out,
                                          size_t out_size);

/** Backward-compatible version-token wrapper. */
void onion_cheat_normalize_version(const char *version, char *out,
                                   size_t out_size);

typedef void (*onion_cheat_progress_fn)(size_t completed, size_t total,
                                        void *user);
typedef int (*onion_cheat_cancel_fn)(void *user);

enum onion_cheat_flatten_result {
  ONION_CHEAT_FLATTEN_OK = 0,
  ONION_CHEAT_FLATTEN_ERROR = -1,
  ONION_CHEAT_FLATTEN_CANCELLED = 1,
};

/**
 * Copy HENCC json/, shn/, and mc4/ files into ONION_CHEATS_DIR with original
 * names. Not recursive. Cooperative cancellation between complete files.
 */
int onion_cheat_flatten_install_tree_cancellable(
    const char *root, onion_cheat_progress_fn progress, void *progress_user,
    onion_cheat_cancel_fn should_cancel, void *cancel_user);

#ifdef __cplusplus
}
#endif
