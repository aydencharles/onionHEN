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

#include "sha256.h"

static const char *const k_cheat_extensions[] = {"json", "shn", "mc4",
                                                 "ShnExt"};

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

int onion_cheat_source_id_from_path(const char *relative_path, char *out,
                                    size_t out_size) {
  static const char hex[] = "0123456789abcdef";
  SHA256_CTX ctx;
  uint8_t digest[SHA256_BLOCK_SIZE];
  size_t i;

  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  if (relative_path == NULL || relative_path[0] == '\0' || out == NULL ||
      out_size < 9) {
    return -1;
  }
  if (relative_path[0] == '/' || relative_path[0] == '\\') {
    return -1;
  }

  sha256_init(&ctx);
  for (i = 0; relative_path[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)relative_path[i];
    if (ch == '\\') {
      ch = '/';
    } else if (ch >= 'A' && ch <= 'Z') {
      ch = (unsigned char)(ch - 'A' + 'a');
    }
    sha256_update(&ctx, &ch, 1);
  }
  sha256_final(&ctx, digest);
  for (i = 0; i < 4; ++i) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 0x0f];
  }
  out[8] = '\0';
  return 0;
}

static int has_path_separator(const char *path) {
  return path != NULL && (strchr(path, '/') != NULL || strchr(path, '\\') != NULL);
}

int onion_cheat_build_flat_name_for_source(const char *filename,
                                           const char *relative_source_path,
                                           char *out, size_t out_size) {
  onion_cheat_filename_t parts;
  const char *extension;
  const char *process;
  char generated_source_id[ONION_CHEAT_SOURCE_ID_LEN];
  const char *source_id;
  int written;

  memset(&parts, 0, sizeof(parts));
  memset(generated_source_id, 0, sizeof(generated_source_id));
  if (out == NULL || out_size == 0) {
    return -1;
  }
  if (onion_cheat_parse_filename(filename, &parts) < 0) {
    return -1;
  }
  extension = onion_cheat_extension_for_rank(parts.extension_rank);
  if (extension == NULL) {
    return -1;
  }

  /* A source already carrying an explicit ID is authoritative. For a file
   * nested below the scan root, derive one from its original relative path so
   * flattening cannot make two physical sources overwrite each other. */
  source_id = parts.source_id;
  if (source_id[0] == '\0' && has_path_separator(relative_source_path) &&
      onion_cheat_source_id_from_path(relative_source_path,
                                      generated_source_id,
                                      sizeof(generated_source_id)) == 0) {
    source_id = generated_source_id;
  }

  process = parts.process;
  if (process[0] != '\0' && onion_cheat_is_eboot_process(process)) {
    process = "";
  }
  if (process[0] != '\0' && source_id[0] != '\0') {
    written = snprintf(out, out_size, "%s_%s_%s_%s.%s", parts.title_id,
                       parts.version, process, source_id, extension);
  } else if (process[0] != '\0') {
    written = snprintf(out, out_size, "%s_%s_%s.%s", parts.title_id,
                       parts.version, process, extension);
  } else if (source_id[0] != '\0') {
    written = snprintf(out, out_size, "%s_%s_%s.%s", parts.title_id,
                       parts.version, source_id, extension);
  } else {
    written = snprintf(out, out_size, "%s_%s.%s", parts.title_id,
                       parts.version, extension);
  }
  return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int onion_cheat_build_flat_name(const char *filename, char *out, size_t out_size) {
  return onion_cheat_build_flat_name_for_source(filename, NULL, out, out_size);
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

static size_t count_flatten_files(const char *dir, const char *relative_dir,
                                  onion_cheat_cancel_fn should_cancel,
                                  void *cancel_user, int *cancelled) {
  DIR *d = opendir(dir);
  struct dirent *ent;
  size_t count = 0;

  if (flatten_cancel_requested(should_cancel, cancel_user)) {
    if (cancelled != NULL) {
      *cancelled = 1;
    }
    return 0;
  }
  if (d == NULL) {
    return 0;
  }
  while ((ent = readdir(d)) != NULL) {
    char path[512];
    char flat[256];
    char relative[1024];
    struct stat st;

    if (flatten_cancel_requested(should_cancel, cancel_user)) {
      if (cancelled != NULL) {
        *cancelled = 1;
      }
      break;
    }
    if (ent->d_name[0] == '.') {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
    if (stat(path, &st) != 0) {
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      if (relative_dir[0] == '\0') {
        snprintf(relative, sizeof(relative), "%s", ent->d_name);
      } else {
        snprintf(relative, sizeof(relative), "%s/%s", relative_dir,
                 ent->d_name);
      }
      count += count_flatten_files(path, relative, should_cancel, cancel_user,
                                   cancelled);
      if (cancelled != NULL && *cancelled) {
        break;
      }
    } else if (S_ISREG(st.st_mode)) {
      if (relative_dir[0] == '\0') {
        snprintf(relative, sizeof(relative), "%s", ent->d_name);
      } else {
        snprintf(relative, sizeof(relative), "%s/%s", relative_dir,
                 ent->d_name);
      }
      if (onion_cheat_build_flat_name_for_source(
              ent->d_name, relative, flat, sizeof(flat)) == 0) {
        ++count;
      }
    }
  }
  closedir(d);
  return count;
}

static int walk_and_flatten(const char *dir, const char *relative_dir,
                            int *copied, int *skipped,
                            size_t *completed, size_t total,
                            onion_cheat_progress_fn progress,
                            void *progress_user,
                            onion_cheat_cancel_fn should_cancel,
                            void *cancel_user) {
  DIR *d = opendir(dir);
  struct dirent *ent;

  if (flatten_cancel_requested(should_cancel, cancel_user)) {
    return ONION_CHEAT_FLATTEN_CANCELLED;
  }
  if (d == NULL) {
    return ONION_CHEAT_FLATTEN_OK;
  }
  while ((ent = readdir(d)) != NULL) {
    char path[512];
    char flat[256];
    char dest[512];
    char relative[1024];
    struct stat st;

    if (flatten_cancel_requested(should_cancel, cancel_user)) {
      closedir(d);
      return ONION_CHEAT_FLATTEN_CANCELLED;
    }
    if (ent->d_name[0] == '.') {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
    if (stat(path, &st) != 0) {
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      if (relative_dir[0] == '\0') {
        snprintf(relative, sizeof(relative), "%s", ent->d_name);
      } else {
        snprintf(relative, sizeof(relative), "%s/%s", relative_dir,
                 ent->d_name);
      }
      const int result = walk_and_flatten(
          path, relative, copied, skipped, completed, total, progress,
          progress_user, should_cancel, cancel_user);
      if (result == ONION_CHEAT_FLATTEN_CANCELLED) {
        closedir(d);
        return result;
      }
      continue;
    }
    if (!S_ISREG(st.st_mode)) {
      continue;
    }
    if (relative_dir[0] == '\0') {
      snprintf(relative, sizeof(relative), "%s", ent->d_name);
    } else {
      snprintf(relative, sizeof(relative), "%s/%s", relative_dir,
               ent->d_name);
    }
    if (onion_cheat_build_flat_name_for_source(ent->d_name, relative, flat,
                                               sizeof(flat)) < 0) {
      continue;
    }
    snprintf(dest, sizeof(dest), ONION_CHEATS_DIR "/%s", flat);
    if (strcmp(path, dest) == 0) {
      ++(*completed);
      if (progress != NULL) {
        progress(*completed, total, progress_user);
      }
      continue;
    }
    if (copy_file(path, dest) == 0) {
      (*copied)++;
      LOG_TRACE("[flatten] %s -> %s", path, dest);
    } else {
      (*skipped)++;
    }
    ++(*completed);
    if (progress != NULL) {
      progress(*completed, total, progress_user);
    }
  }
  closedir(d);
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

/**
 * Walk a tree (typically after zip extract) and install flat cheat files into
 * ONION_CHEATS_DIR as TITLEID_VERSION[_PROCESS][_SOURCE_ID].ext.
 */
int onion_cheat_flatten_install_tree_cancellable(
    const char *root, onion_cheat_progress_fn progress, void *progress_user,
    onion_cheat_cancel_fn should_cancel, void *cancel_user) {
  int copied = 0;
  int skipped = 0;
  int cancelled = 0;
  size_t completed = 0;
  size_t total;

  mkdir(ONION_DATA_ROOT, 0777);
  mkdir(ONION_CHEATS_DIR, 0777);

  if (root == NULL || root[0] == '\0') {
    root = ONION_CHEATS_DIR;
  }
  total = count_flatten_files(root, "", should_cancel, cancel_user, &cancelled);
  if (cancelled) {
    return ONION_CHEAT_FLATTEN_CANCELLED;
  }
  if (progress != NULL) {
    progress(0, total, progress_user);
  }
  if (walk_and_flatten(root, "", &copied, &skipped, &completed, total,
                       progress, progress_user, should_cancel, cancel_user) ==
      ONION_CHEAT_FLATTEN_CANCELLED) {
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
