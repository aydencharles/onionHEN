#include <onion/log.h>
#include "cheats/runtime.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const k_cheat_extensions[] = {"json", "shn", "mc4",
                                                 "ShnExt"};
/* HENCC catalog folders are json/, shn/, mc4/. ShnExt is not a catalog folder. */
enum { k_hencc_install_folder_count = 3 };
_Static_assert(k_hencc_install_folder_count <
                   (int)(sizeof(k_cheat_extensions) /
                         sizeof(k_cheat_extensions[0])),
               "install folders must be a prefix of k_cheat_extensions");

const char *onion_cheat_extension_for_rank(int rank) {
  if (rank < 0 || (size_t)rank >= sizeof(k_cheat_extensions) /
                                      sizeof(k_cheat_extensions[0])) {
    return NULL;
  }
  return k_cheat_extensions[rank];
}

int onion_cheat_extension_rank(const char *name, size_t *extension_start) {
  size_t name_len;

  if (name == NULL) {
    return -1;
  }
  name_len = strlen(name);
  for (size_t i = 0;
       i < sizeof(k_cheat_extensions) / sizeof(k_cheat_extensions[0]); ++i) {
    const char *extension = k_cheat_extensions[i];
    const size_t extension_len = strlen(extension);
    if (name_len <= extension_len + 1 ||
        name[name_len - extension_len - 1] != '.') {
      continue;
    }
    if (strcasecmp(name + name_len - extension_len, extension) == 0) {
      if (extension_start != NULL) {
        *extension_start = name_len - extension_len - 1;
      }
      return (int)i;
    }
  }
  return -1;
}

int onion_cheat_match_ext(const char *name, char *ext_out, size_t ext_out_size) {
  const int rank = onion_cheat_extension_rank(name, NULL);
  const char *extension;
  if (rank < 0) {
    return 0;
  }
  extension = onion_cheat_extension_for_rank(rank);
  if (ext_out != NULL && ext_out_size > 0) {
    snprintf(ext_out, ext_out_size, "%s", extension);
  }
  return 1;
}

static int ascii_iequals(const char *lhs, const char *rhs) {
  if (lhs == NULL || rhs == NULL) {
    return 0;
  }
  return strcasecmp(lhs, rhs) == 0;
}

static int is_title_separator(char ch) {
  return ch == '_' || ch == '-' || ch == ' ';
}

static size_t title_id_len_at(const char *s) {
  size_t i;
  size_t digits = 0;

  if (s == NULL) {
    return 0;
  }
  for (i = 0; i < 4; ++i) {
    if (!isalpha((unsigned char)s[i])) {
      return 0;
    }
  }
  for (; s[i] != '\0' && !is_title_separator(s[i]); ++i) {
    if (isdigit((unsigned char)s[i])) {
      ++digits;
    } else if (!isalpha((unsigned char)s[i])) {
      return 0;
    }
  }
  if (i < 8 || digits < 4) {
    return 0;
  }
  return i;
}

static const char *find_title_start(const char *base) {
  const char *p;

  if (title_id_len_at(base) > 0) {
    return base;
  }
  for (p = base; *p != '\0'; ++p) {
    if ((p == base || is_title_separator(p[-1])) && title_id_len_at(p) > 0) {
      return p;
    }
  }
  return NULL;
}

static int looks_like_process(const char *value) {
  if (value == NULL || value[0] == '\0') {
    return 0;
  }
  if (onion_cheat_is_eboot_process(value)) {
    return 1;
  }
  return strchr(value, '.') != NULL;
}

static void lowercase_ascii(char *value) {
  size_t i;
  if (value == NULL) {
    return;
  }
  for (i = 0; value[i] != '\0'; ++i) {
    value[i] = (char)tolower((unsigned char)value[i]);
  }
}

int onion_cheat_is_source_id(const char *value) {
  size_t i;

  if (value == NULL || value[0] == '\0' || strlen(value) != 8) {
    return 0;
  }
  for (i = 0; i < 8; ++i) {
    if (!isxdigit((unsigned char)value[i])) {
      return 0;
    }
  }
  return 1;
}

int onion_cheat_is_eboot_process(const char *process) {
  return ascii_iequals(process, "eboot") ||
         ascii_iequals(process, "eboot.bin");
}

static void split_process_and_source_id(onion_cheat_filename_t *out) {
  const char *last_us;
  const char *process_or_author = out->suffix;
  char prefix[ONION_CHEAT_SUFFIX_LEN];

  if (out->suffix[0] == '\0') {
    return;
  }
  if (onion_cheat_is_source_id(out->suffix)) {
    snprintf(out->source_id, sizeof(out->source_id), "%s", out->suffix);
    lowercase_ascii(out->source_id);
    return;
  }

  last_us = strrchr(out->suffix, '_');
  if (last_us != NULL && onion_cheat_is_source_id(last_us + 1)) {
    const size_t prefix_len = (size_t)(last_us - out->suffix);
    if (prefix_len > 0 && prefix_len < sizeof(prefix)) {
      memcpy(prefix, out->suffix, prefix_len);
      prefix[prefix_len] = '\0';
      process_or_author = prefix;
    } else {
      process_or_author = "";
    }
    snprintf(out->source_id, sizeof(out->source_id), "%s", last_us + 1);
    lowercase_ascii(out->source_id);
  }

  if (looks_like_process(process_or_author)) {
    snprintf(out->process, sizeof(out->process), "%s", process_or_author);
  }
}

int onion_cheat_parse_filename(const char *filename,
                               onion_cheat_filename_t *out) {
  char base[256];
  size_t extension_start = 0;
  const char *title_at;
  const char *sep;
  const char *vstart;
  const char *vend;
  size_t title_len;
  size_t version_len;
  size_t suffix_len;
  size_t i;

  if (filename == NULL || out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  out->extension_rank = onion_cheat_extension_rank(filename, &extension_start);
  if (out->extension_rank < 0 || extension_start == 0 ||
      extension_start >= sizeof(base)) {
    return -1;
  }

  memcpy(base, filename, extension_start);
  base[extension_start] = '\0';
  if (extension_start >= 4 &&
      strcasecmp(base + extension_start - 4, ".xml") == 0) {
    base[extension_start - 4] = '\0';
  }

  title_at = find_title_start(base);
  if (title_at == NULL) {
    title_at = base;
  }

  sep = title_at;
  while (*sep && !is_title_separator(*sep)) {
    ++sep;
  }
  title_len = (size_t)(sep - title_at);
  if (*sep == '\0' || title_len < 4 ||
      title_len >= sizeof(out->title_id)) {
    return -1;
  }
  memcpy(out->title_id, title_at, title_len);
  out->title_id[title_len] = '\0';

  vstart = sep + 1;
  vend = vstart;
  while (*vend &&
         ((*vend >= '0' && *vend <= '9') || *vend == '.' || *vend == 'x' ||
          *vend == 'X')) {
    ++vend;
  }
  version_len = (size_t)(vend - vstart);
  if (version_len == 0 || version_len >= sizeof(out->version)) {
    return -1;
  }
  memcpy(out->version, vstart, version_len);
  out->version[version_len] = '\0';

  if (is_title_separator(*vend)) {
    ++vend;
  }
  suffix_len = strlen(vend);
  if (suffix_len >= sizeof(out->suffix)) {
    return -1;
  }
  memcpy(out->suffix, vend, suffix_len + 1);
  split_process_and_source_id(out);

  for (i = 0; out->title_id[i]; ++i) {
    out->title_id[i] = (char)toupper((unsigned char)out->title_id[i]);
  }
  return 0;
}

int onion_cheat_is_legacy_eboot_alias(const char *suffix) {
  if (suffix == NULL || suffix[0] == '\0') {
    return 0;
  }
  if (looks_like_process(suffix) && !onion_cheat_is_eboot_process(suffix)) {
    return 0;
  }
  return 1;
}

static int processes_match(const char *lhs, const char *rhs) {
  if (onion_cheat_is_eboot_process(lhs) && onion_cheat_is_eboot_process(rhs)) {
    return 1;
  }
  if (lhs == NULL || rhs == NULL || lhs[0] == '\0' || rhs[0] == '\0') {
    return 0;
  }
  return strcasecmp(lhs, rhs) == 0;
}

int onion_cheat_filename_compatible(const onion_cheat_filename_t *parts,
                                    const char *process) {
  if (parts == NULL) {
    return 0;
  }

  if (parts->process[0] != '\0') {
    if (onion_cheat_is_eboot_process(parts->process)) {
      return process == NULL || process[0] == '\0' ||
             onion_cheat_is_eboot_process(process);
    }
    return processes_match(parts->process, process);
  }

  if (parts->source_id[0] == '\0' && parts->suffix[0] != '\0') {
    return process == NULL || process[0] == '\0' ||
           onion_cheat_is_eboot_process(process);
  }
  return 1;
}

static int scope_rank(const onion_cheat_filename_t *parts) {
  if (parts->process[0] != '\0') {
    return 0;
  }
  if (parts->source_id[0] != '\0' || parts->suffix[0] == '\0') {
    return 1;
  }
  return 2;
}

int onion_cheat_filename_compare(const onion_cheat_filename_t *lhs,
                                 const char *lhs_name,
                                 const onion_cheat_filename_t *rhs,
                                 const char *rhs_name) {
  int left;
  int right;

  if (lhs == NULL || rhs == NULL) {
    return lhs == rhs ? 0 : (lhs == NULL ? 1 : -1);
  }

  left = scope_rank(lhs);
  right = scope_rank(rhs);
  if (left != right) {
    return left - right;
  }

  if (lhs->extension_rank != rhs->extension_rank) {
    return lhs->extension_rank - rhs->extension_rank;
  }
  if (lhs_name != NULL && rhs_name != NULL) {
    return strcasecmp(lhs_name, rhs_name);
  }
  return 0;
}

static int copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  FILE *out = NULL;
  char buf[8192];
  size_t n;

  if (in == NULL) {
    return -1;
  }
  out = fopen(dst, "wb");
  if (out == NULL) {
    fclose(in);
    return -1;
  }
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return -1;
    }
  }
  fclose(in);
  fclose(out);
  return 0;
}

static int flatten_cancel_requested(onion_cheat_cancel_fn should_cancel,
                                    void *cancel_user) {
  return should_cancel != NULL && should_cancel(cancel_user) != 0;
}

static int is_installable_name(const char *name, int expected_rank) {
  if (name == NULL || name[0] == '\0' || name[0] == '.') {
    return 0;
  }
  return onion_cheat_extension_rank(name, NULL) == expected_rank;
}

static int visit_install_folder(const char *folder, int expected_rank,
                                onion_cheat_cancel_fn should_cancel,
                                void *cancel_user, int *cancelled,
                                int (*on_file)(const char *src, const char *name,
                                               void *user),
                                void *user) {
  DIR *directory;
  struct dirent *ent;

  if (flatten_cancel_requested(should_cancel, cancel_user)) {
    if (cancelled != NULL) {
      *cancelled = 1;
    }
    return ONION_CHEAT_FLATTEN_CANCELLED;
  }
  directory = opendir(folder);
  if (directory == NULL) {
    return ONION_CHEAT_FLATTEN_OK;
  }
  while ((ent = readdir(directory)) != NULL) {
    char path[512];
    struct stat st;
    int written;

    if (flatten_cancel_requested(should_cancel, cancel_user)) {
      if (cancelled != NULL) {
        *cancelled = 1;
      }
      closedir(directory);
      return ONION_CHEAT_FLATTEN_CANCELLED;
    }
    if (!is_installable_name(ent->d_name, expected_rank)) {
      continue;
    }
    written = snprintf(path, sizeof(path), "%s/%s", folder, ent->d_name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
      continue;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }
    if (on_file != NULL && on_file(path, ent->d_name, user) != 0) {
      closedir(directory);
      return ONION_CHEAT_FLATTEN_ERROR;
    }
  }
  closedir(directory);
  return ONION_CHEAT_FLATTEN_OK;
}

static int count_install_file(const char *src, const char *name, void *user) {
  size_t *count = (size_t *)user;
  (void)src;
  (void)name;
  ++(*count);
  return 0;
}

struct copy_install_state {
  int *copied;
  int *skipped;
  size_t *completed;
  size_t total;
  onion_cheat_progress_fn progress;
  void *progress_user;
};

static void note_install_progress(struct copy_install_state *state) {
  ++(*state->completed);
  if (state->progress != NULL) {
    state->progress(*state->completed, state->total, state->progress_user);
  }
}

static int copy_install_file(const char *src, const char *name, void *user) {
  struct copy_install_state *state = (struct copy_install_state *)user;
  char dest[512];
  int written;

  written = snprintf(dest, sizeof(dest), ONION_CHEATS_DIR "/%s", name);
  if (written < 0 || (size_t)written >= sizeof(dest)) {
    (*state->skipped)++;
    note_install_progress(state);
    return 0;
  }
  if (strcmp(src, dest) == 0) {
    note_install_progress(state);
    return 0;
  }
  if (copy_file(src, dest) == 0) {
    (*state->copied)++;
    LOG_TRACE("[flatten] %s -> %s", src, dest);
  } else {
    (*state->skipped)++;
  }
  note_install_progress(state);
  return 0;
}

static int for_each_install_folder(const char *root,
                                   onion_cheat_cancel_fn should_cancel,
                                   void *cancel_user, int *cancelled,
                                   int (*on_file)(const char *src,
                                                  const char *name, void *user),
                                   void *user) {
  size_t i;

  for (i = 0; i < k_hencc_install_folder_count; ++i) {
    char folder[512];
    int result;
    int written;

    written = snprintf(folder, sizeof(folder), "%s/%s", root,
                       k_cheat_extensions[i]);
    if (written < 0 || (size_t)written >= sizeof(folder)) {
      continue;
    }
    result = visit_install_folder(folder, (int)i, should_cancel, cancel_user,
                                  cancelled, on_file, user);
    if (result != ONION_CHEAT_FLATTEN_OK) {
      return result;
    }
    if (cancelled != NULL && *cancelled) {
      return ONION_CHEAT_FLATTEN_CANCELLED;
    }
  }
  return ONION_CHEAT_FLATTEN_OK;
}

void onion_cheat_normalize_filename_token(const char *value, char *out,
                                          size_t out_size) {
  size_t j = 0;
  if (out == NULL || out_size == 0)
    return;
  out[0] = '\0';
  if (!value)
    return;
  for (size_t i = 0; value[i] && j + 1 < out_size; ++i) {
    const unsigned char ch = (unsigned char)value[i];
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '_' || ch == '-') {
      out[j++] = (char)ch;
    } else {
      out[j++] = '_';
    }
  }
  out[j] = '\0';
}

void onion_cheat_normalize_version(const char *version, char *out,
                                   size_t out_size) {
  onion_cheat_normalize_filename_token(version, out, out_size);
}

int onion_cheat_flatten_install_tree_cancellable(
    const char *root, onion_cheat_progress_fn progress, void *progress_user,
    onion_cheat_cancel_fn should_cancel, void *cancel_user) {
  int copied = 0;
  int skipped = 0;
  int cancelled = 0;
  size_t completed = 0;
  size_t total = 0;
  struct copy_install_state state;
  int result;

  mkdir(ONION_DATA_ROOT, 0777);
  mkdir(ONION_CHEATS_DIR, 0777);

  if (root == NULL || root[0] == '\0') {
    root = ONION_CHEATS_DIR;
  }
  result = for_each_install_folder(root, should_cancel, cancel_user, &cancelled,
                                   count_install_file, &total);
  if (result == ONION_CHEAT_FLATTEN_CANCELLED || cancelled) {
    return ONION_CHEAT_FLATTEN_CANCELLED;
  }
  if (progress != NULL) {
    progress(0, total, progress_user);
  }
  state.copied = &copied;
  state.skipped = &skipped;
  state.completed = &completed;
  state.total = total;
  state.progress = progress;
  state.progress_user = progress_user;
  result = for_each_install_folder(root, should_cancel, cancel_user, &cancelled,
                                   copy_install_file, &state);
  if (result == ONION_CHEAT_FLATTEN_CANCELLED || cancelled) {
    LOG_DEBUG("[flatten] cancelled after %zu/%zu cheat file(s)", completed,
              total);
    return ONION_CHEAT_FLATTEN_CANCELLED;
  }
  if (skipped > 0) {
    LOG_WARN("[flatten] installed %d cheat file(s), skipped %d", copied,
             skipped);
  } else {
    LOG_DEBUG("[flatten] installed %d cheat file(s), skipped 0", copied);
  }
  return copied > 0 ? ONION_CHEAT_FLATTEN_OK : ONION_CHEAT_FLATTEN_ERROR;
}
