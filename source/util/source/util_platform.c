#include <onion/fs.h>
#include <onion/log.h>
#include "util_platform.h"

#include "cJSON.hpp"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>

#include <ps5/kernel.h>


/* Same as NineS SYS_dl_get_list / SYS_dl_get_info_2 */
#ifndef SYS_dl_get_list
#define SYS_dl_get_list 0x217
#endif
#ifndef SYS_dl_get_info_2
#define SYS_dl_get_info_2 0x2cd
#endif

int sceKernelGetProcessName(int pid, char *name);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);

typedef struct {
  uint32_t app_id;
  uint64_t unknown1;
  char title_id[14];
  char unknown2[0x3c];
} app_info_t;

int sceKernelGetAppInfo(int pid, app_info_t *info);

typedef struct {
  uint64_t pad0;
  char version_str[0x1C];
  uint32_t version;
  uint64_t pad1;
} OrbisKernelSwVersion;

int sceKernelGetProsperoSystemSwVersion(OrbisKernelSwVersion *version);

#define PROC_SHARED_OBJECT_OFFSET 0x3e8
#define SHARED_LIB_IMAGEBASE_OFFSET 0x30

/* ---- firmware ----------------------------------------------------------- */

uint32_t util_system_fw_major(void) {
  OrbisKernelSwVersion sys_ver;

  memset(&sys_ver, 0, sizeof(sys_ver));
  if (sceKernelGetProsperoSystemSwVersion(&sys_ver) != 0) {
    return 0;
  }
  return sys_ver.version >> 16;
}

/* ---- file --------------------------------------------------------------- */

int util_file_read_alloc(const char *path, char **buf_out, size_t *size_out,
                         size_t max_size) {
  size_t size = 0;
  char *buf = NULL;

  if (path == NULL || buf_out == NULL) {
    return -1;
  }
  *buf_out = NULL;
  if (size_out != NULL) {
    *size_out = 0;
  }

  /* This helper's cap convention predates onion/fs.h: 0 means "1 MiB default"
   * and (size_t)-1 means unlimited, whereas read_file_alloc() uses 0 for
   * unlimited. Translate at the boundary so callers keep their meaning. */
  if (max_size == 0) {
    max_size = 1024u * 1024u;
  } else if (max_size == (size_t)-1) {
    max_size = 0;
  }

  if (!read_file_alloc_str(path, max_size, &buf, &size)) {
    return -1;
  }

  *buf_out = buf;
  if (size_out != NULL) {
    *size_out = size;
  }
  return 0;
}

/* ---- game meta ---------------------------------------------------------- */

void util_game_platform_from_title_id(const char *title_id, char *out,
                                      size_t out_size) {
  if (out == NULL || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (title_id == NULL) {
    return;
  }
  if (strncmp(title_id, "PPSA", 4) == 0 || strncmp(title_id, "PCSA", 4) == 0) {
    snprintf(out, out_size, "ps5");
  } else if (strncmp(title_id, "CUSA", 4) == 0 ||
             strncmp(title_id, "CUSB", 4) == 0 ||
             strncmp(title_id, "CUSC", 4) == 0 ||
             strncmp(title_id, "PCAS", 4) == 0) {
    snprintf(out, out_size, "ps4");
  } else {
    snprintf(out, out_size, "unknown");
  }
}

static int file_exists(const char *path) {
  struct stat st;
  return path != NULL && stat(path, &st) == 0;
}

static int extract_version_from_json(const char *json, size_t json_size,
                                     const char *const *keys,
                                     size_t key_count, char *out,
                                     size_t out_size) {
  const char *parse_end = NULL;
  cJSON *root;
  size_t i;
  if (json == NULL || out == NULL || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  root = cJSON_ParseWithLengthOpts(json, json_size, &parse_end, 0);
  while (root != NULL && parse_end < json + json_size &&
         isspace((unsigned char)*parse_end)) {
    ++parse_end;
  }
  if (root != NULL && parse_end != json + json_size) {
    cJSON_Delete(root);
    return -1;
  }
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return -1;
  }
  for (i = 0; i < key_count; ++i) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] != '\0') {
      snprintf(out, out_size, "%s", item->valuestring);
      cJSON_Delete(root);
      return 0;
    }
  }
  cJSON_Delete(root);
  return -1;
}

/* Minimal SFO string reader for APP_VER / VERSION (same need as GET_GAME_VER). */
static int extract_sfo_string(const unsigned char *buffer, size_t buffer_size,
                              const char *key, char *out, size_t out_size) {
  uint32_t key_offset, data_offset, entry_count;
  uint32_t i;

  if (buffer_size < 20 || out_size == 0) {
    return -1;
  }
  if (buffer[0] != 0x00 || buffer[1] != 0x50 || buffer[2] != 0x53 ||
      buffer[3] != 0x46) {
    return -1;
  }
  memcpy(&key_offset, buffer + 8, 4);
  memcpy(&data_offset, buffer + 12, 4);
  memcpy(&entry_count, buffer + 16, 4);
  if (key_offset >= buffer_size || data_offset >= buffer_size) {
    return -1;
  }

  for (i = 0; i < entry_count; i++) {
    uint32_t index_offset = 20 + i * 16;
    uint16_t key_name_offset;
    uint32_t param_length;
    uint32_t data_entry_offset;
    const char *current_key;
    size_t copy_len;

    if (index_offset + 16 > key_offset) {
      break;
    }
    memcpy(&key_name_offset, buffer + index_offset, 2);
    memcpy(&param_length, buffer + index_offset + 4, 4);
    memcpy(&data_entry_offset, buffer + index_offset + 12, 4);
    if (key_offset + key_name_offset >= buffer_size) {
      continue;
    }
    current_key = (const char *)(buffer + key_offset + key_name_offset);
    if (strcmp(current_key, key) != 0) {
      continue;
    }
    if (data_offset + data_entry_offset + param_length > buffer_size) {
      return -1;
    }
    copy_len = param_length;
    if (copy_len >= out_size) {
      copy_len = out_size - 1;
    }
    memcpy(out, buffer + data_offset + data_entry_offset, copy_len);
    out[copy_len] = '\0';
    /* trim trailing NULs already in SFO */
    return out[0] != '\0' ? 0 : -1;
  }
  return -1;
}

int util_resolve_game_version(const char *title_id, char *out, size_t out_size) {
  char path[256];
  char *buf = NULL;
  size_t buf_size = 0;
  int is_ps5;
  static const char *ps5_json_paths[] = {
      "/system_data/priv/appmeta/%s/param.json",
      "/system_data/priv/appmeta/external/%s/param.json",
      "/system_ex/app/%s/sce_sys/param.json",
      "/user/appmeta/%s/param.json",
  };
  static const char *ps4_sfo_paths[] = {
      "/system_data/priv/appmeta/%s/param.sfo",
      "/system_data/priv/appmeta/external/%s/param.sfo",
      "/user/appmeta/%s/param.sfo",
  };
  size_t i;

  if (title_id == NULL || out == NULL || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  is_ps5 = strncmp(title_id, "PPSA", 4) == 0 ||
           strncmp(title_id, "PCSA", 4) == 0;

  if (is_ps5) {
    static const char *keys[] = {"contentVersion", "content_version",
                                 "titleVersion", "version", "APP_VER"};
    for (i = 0; i < sizeof(ps5_json_paths) / sizeof(ps5_json_paths[0]); ++i) {
      snprintf(path, sizeof(path), ps5_json_paths[i], title_id);
      if (util_file_read_alloc(path, &buf, &buf_size, 256 * 1024) < 0) {
        continue;
      }
      if (extract_version_from_json(buf, buf_size, keys,
                                    sizeof(keys) / sizeof(keys[0]), out,
                                    out_size) == 0) {
        free(buf);
        return 0;
      }
      free(buf);
      buf = NULL;
    }
  } else {
    for (i = 0; i < sizeof(ps4_sfo_paths) / sizeof(ps4_sfo_paths[0]); ++i) {
      char ver[32] = {0};
      char app_ver[32] = {0};
      snprintf(path, sizeof(path), ps4_sfo_paths[i], title_id);
      if (util_file_read_alloc(path, &buf, &buf_size, 256 * 1024) < 0) {
        continue;
      }
      extract_sfo_string((const unsigned char *)buf, buf_size, "VERSION", ver,
                         sizeof(ver));
      extract_sfo_string((const unsigned char *)buf, buf_size, "APP_VER",
                         app_ver, sizeof(app_ver));
      free(buf);
      buf = NULL;
      if (ver[0] != '\0' && app_ver[0] != '\0') {
        /* Prefer higher of VERSION / APP_VER like GET_GAME_VER */
        double a = atof(ver);
        double b = atof(app_ver);
        snprintf(out, out_size, "%s", a > b ? ver : app_ver);
        return 0;
      }
      if (app_ver[0] != '\0') {
        snprintf(out, out_size, "%s", app_ver);
        return 0;
      }
      if (ver[0] != '\0') {
        snprintf(out, out_size, "%s", ver);
        return 0;
      }
    }
  }
  (void)file_exists;
  return -1;
}

static int read_process_table(void **buf_out, size_t *buf_size_out) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  void *buf = NULL;

  if (buf_out == NULL || buf_size_out == NULL) {
    return -1;
  }
  *buf_out = NULL;
  *buf_size_out = 0;
  if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0 || buf_size == 0) {
    return -1;
  }
  buf = malloc(buf_size);
  if (buf == NULL) {
    return -1;
  }
  if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
    free(buf);
    return -1;
  }
  *buf_out = buf;
  *buf_size_out = buf_size;
  return 0;
}

int util_get_running_bigapp(game_context_t *out) {
  int appid;
  void *proc_buf = NULL;
  size_t proc_buf_size = 0;

  if (out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  out->pid = -1;

  appid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (appid < 0) {
    return -1;
  }
  out->appid = appid;

  if (sceSystemServiceGetAppTitleId(appid, out->title_id) != 0) {
    out->title_id[0] = '\0';
  }
  util_game_platform_from_title_id(out->title_id, out->platform,
                                   sizeof(out->platform));

  if (read_process_table(&proc_buf, &proc_buf_size) == 0) {
    char *ptr = (char *)proc_buf;
    char *end = ptr + proc_buf_size;

    while (ptr < end) {
      struct kinfo_proc *ki = (struct kinfo_proc *)ptr;
      app_info_t app_info;
      char process_name[64];

      if (ki->ki_structsize <= 0) {
        break;
      }
      ptr += ki->ki_structsize;

      memset(&app_info, 0, sizeof(app_info));
      if (sceKernelGetAppInfo(ki->ki_pid, &app_info) < 0 ||
          (int)app_info.app_id != appid) {
        continue;
      }
      memset(process_name, 0, sizeof(process_name));
      if (sceKernelGetProcessName(ki->ki_pid, process_name) < 0) {
        continue;
      }
      if (strcmp(ki->ki_comm, process_name) != 0) {
        continue;
      }

      out->pid = ki->ki_pid;
      if (out->title_id[0] == '\0' && app_info.title_id[0] != '\0') {
        snprintf(out->title_id, sizeof(out->title_id), "%s", app_info.title_id);
        util_game_platform_from_title_id(out->title_id, out->platform,
                                         sizeof(out->platform));
      }
      snprintf(out->process_name, sizeof(out->process_name), "%s", process_name);
      free(proc_buf);
      if (util_resolve_game_version(out->title_id, out->version,
                                    sizeof(out->version)) < 0) {
        snprintf(out->version, sizeof(out->version), "unknown");
      }
      return 0;
    }
    free(proc_buf);
  }

  /* Fallback: scan pids */
  for (int pid = 1; pid <= 9999; ++pid) {
    app_info_t app_info;
    memset(&app_info, 0, sizeof(app_info));
    if (sceKernelGetAppInfo(pid, &app_info) < 0 ||
        (int)app_info.app_id != appid) {
      continue;
    }
    out->pid = pid;
    if (out->title_id[0] == '\0' && app_info.title_id[0] != '\0') {
      snprintf(out->title_id, sizeof(out->title_id), "%s", app_info.title_id);
      util_game_platform_from_title_id(out->title_id, out->platform,
                                       sizeof(out->platform));
    }
    if (sceKernelGetProcessName(pid, out->process_name) < 0) {
      out->process_name[0] = '\0';
    }
    if (util_resolve_game_version(out->title_id, out->version,
                                  sizeof(out->version)) < 0) {
      snprintf(out->version, sizeof(out->version), "unknown");
    }
    return 0;
  }
  return -1;
}

/* ---- module lookup (NineS + eboot fallback) ----------------------------- */

static int find_eboot_imagebase(pid_t pid, const char *module_name,
                                util_module_info_t *out) {
  intptr_t proc = 0;
  intptr_t shared_object = 0;
  intptr_t eboot = 0;
  uintptr_t imagebase = 0;
  char process_name[64];

  if (module_name == NULL || out == NULL) {
    return -1;
  }
  memset(process_name, 0, sizeof(process_name));
  sceKernelGetProcessName(pid, process_name);
  if (strcmp(module_name, "eboot") != 0 &&
      strcmp(module_name, "eboot.bin") != 0 &&
      strcmp(module_name, process_name) != 0) {
    return -1;
  }

  proc = (intptr_t)kernel_get_proc(pid);
  if (proc == 0) {
    return -1;
  }
  if (kernel_copyout(proc + PROC_SHARED_OBJECT_OFFSET, &shared_object,
                     sizeof(shared_object)) < 0 ||
      shared_object == 0) {
    return -1;
  }
  if (kernel_copyout(shared_object, &eboot, sizeof(eboot)) < 0 || eboot == 0) {
    return -1;
  }
  if (kernel_copyout(eboot + SHARED_LIB_IMAGEBASE_OFFSET, &imagebase,
                     sizeof(imagebase)) < 0 ||
      imagebase == 0) {
    uintptr_t fallback = 0;
    if (kernel_copyout(eboot + 0x38, &fallback, sizeof(fallback)) < 0 ||
        fallback == 0) {
      return -1;
    }
    imagebase = fallback;
  }

  memset(out, 0, sizeof(*out));
  snprintf(out->filename, sizeof(out->filename), "%s", module_name);
  snprintf(out->libname, sizeof(out->libname), "%s", module_name);
  out->handle = (uint64_t)eboot;
  out->sections[0].vaddr = imagebase;
  out->sections[0].prot = PROT_READ | PROT_EXEC;
  return 0;
}

static const char *path_basename_c(const char *path) {
  const char *slash;
  if (path == NULL || path[0] == '\0') {
    return "";
  }
  slash = strrchr(path, '/');
  return slash != NULL ? slash + 1 : path;
}

static int module_name_matches(const util_module_info_t *mod,
                               const char *module_name) {
  if (mod == NULL || module_name == NULL) {
    return 0;
  }
  return strcmp(mod->filename, module_name) == 0 ||
         strcmp(mod->libname, module_name) == 0 ||
         strcmp(path_basename_c(mod->sandboxed_path), module_name) == 0;
}

/*
 * Dynlib lookup — same matching rules as NineS get_module_info().
 * Implemented here to avoid pulling NineS freebsd-helper into util's TU.
 */
static int dynlib_find_module(pid_t pid, const char *module_name,
                              util_module_info_t *out) {
  size_t num_handles = 0;
  uintptr_t *handles = NULL;
  size_t i;

  memset(out, 0, sizeof(*out));
  syscall(SYS_dl_get_list, pid, NULL, 0, &num_handles);
  if (num_handles == 0) {
    return -1;
  }
  handles = (uintptr_t *)calloc(num_handles, sizeof(uintptr_t));
  if (handles == NULL) {
    return -1;
  }
  syscall(SYS_dl_get_list, pid, handles, num_handles, &num_handles);
  for (i = 0; i < num_handles; ++i) {
    util_module_info_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    syscall(SYS_dl_get_info_2, pid, 1, handles[i], &tmp);
    if (module_name_matches(&tmp, module_name)) {
      *out = tmp;
      free(handles);
      return 0;
    }
  }
  free(handles);
  return -1;
}

int util_find_module(pid_t pid, const char *module_name,
                     util_module_info_t *out) {
  if (out == NULL) {
    return -1;
  }
  if (dynlib_find_module(pid, module_name, out) == 0) {
    return 0;
  }
  return find_eboot_imagebase(pid, module_name, out);
}

int util_find_module_in_app(int appid, const char *module_name, pid_t *pid_out,
                            util_module_info_t *out) {
  void *proc_buf = NULL;
  size_t proc_buf_size = 0;

  if (module_name == NULL || out == NULL) {
    return -1;
  }

  if (read_process_table(&proc_buf, &proc_buf_size) == 0) {
    char *ptr = (char *)proc_buf;
    char *end = ptr + proc_buf_size;

    while (ptr < end) {
      struct kinfo_proc *ki = (struct kinfo_proc *)ptr;
      app_info_t app_info;

      if (ki->ki_structsize <= 0) {
        break;
      }
      ptr += ki->ki_structsize;
      memset(&app_info, 0, sizeof(app_info));
      if (sceKernelGetAppInfo(ki->ki_pid, &app_info) < 0 ||
          (int)app_info.app_id != appid) {
        continue;
      }
      if (util_find_module(ki->ki_pid, module_name, out) == 0) {
        if (pid_out != NULL) {
          *pid_out = ki->ki_pid;
        }
        free(proc_buf);
        return 0;
      }
    }
    free(proc_buf);
  }

  for (int pid = 1; pid <= 9999; ++pid) {
    app_info_t app_info;
    memset(&app_info, 0, sizeof(app_info));
    if (sceKernelGetAppInfo(pid, &app_info) < 0 ||
        (int)app_info.app_id != appid) {
      continue;
    }
    if (util_find_module(pid, module_name, out) == 0) {
      if (pid_out != NULL) {
        *pid_out = pid;
      }
      return 0;
    }
  }
  memset(out, 0, sizeof(*out));
  return -1;
}
