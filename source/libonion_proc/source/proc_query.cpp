/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * sysctl-based process lookup (single implementation for all OnionHEN bins).
 */

extern "C" {
#include <onion/proc_query.h>
}

#include "freebsd-helper.h"

#include <cstdlib>
#include <cstring>
#include <sys/sysctl.h>
#include <unistd.h>

#include <onion/log.h>

namespace {

uint64_t process_generation(const struct kinfo_proc *process) {
  if (!process) {
    return 0;
  }
  return (static_cast<uint64_t>(process->ki_start.tv_sec) << 32) |
         static_cast<uint64_t>(process->ki_start.tv_usec);
}

onion_get_process_name_fn g_get_name = nullptr;
onion_get_app_info_fn g_get_app_info = nullptr;
onion_get_bigapp_id_fn g_get_bigapp = nullptr;

// app_info_t starts with uint32_t app_id (see shellui external_symbols).

pid_t scan_kinfo(const char *name, bool substr) {
  if (!name || !*name) {
    return -1;
  }

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  if (sysctl(mib, 4, nullptr, &buf_size, nullptr, 0)) {
    return -1;
  }
  void *buf = malloc(buf_size);
  if (!buf) {
    return -1;
  }
  if (sysctl(mib, 4, buf, &buf_size, nullptr, 0)) {
    free(buf);
    return -1;
  }

  pid_t found = -1;
  for (char *ptr = static_cast<char *>(buf);
       ptr < static_cast<char *>(buf) + buf_size;) {
    auto *ki = reinterpret_cast<struct kinfo_proc *>(ptr);
    if (ki->ki_structsize <= 0) {
      break;
    }
    ptr += ki->ki_structsize;

    const char *comm = ki->ki_comm;
    bool match = false;
    if (substr) {
      match = (comm && strstr(comm, name) != nullptr);
    } else {
      match = (comm && strcmp(comm, name) == 0);
    }
    // Also match thread name when present (daemon historically used ki_tdname).
    if (!match && ki->ki_tdname[0]) {
      if (substr) {
        match = strstr(ki->ki_tdname, name) != nullptr;
      } else {
        match = strcmp(ki->ki_tdname, name) == 0;
      }
    }
    if (match) {
      found = ki->ki_pid;
      break;
    }
  }

  free(buf);
  return found;
}

} // namespace

extern "C" void onion_proc_set_sce_hooks(onion_get_process_name_fn get_name,
                                         onion_get_app_info_fn get_app_info,
                                         onion_get_bigapp_id_fn get_bigapp) {
  g_get_name = get_name;
  g_get_app_info = get_app_info;
  g_get_bigapp = get_bigapp;
}

extern "C" int onion_resolve_running_bigapp(onion_bigapp_process_t *out) {
  if (!out || !g_get_bigapp || !g_get_app_info) {
    return -1;
  }
  std::memset(out, 0, sizeof(*out));
  out->pid = -1;
  const int bigappid = g_get_bigapp();
  if (bigappid < 0) {
    return -1;
  }

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  if (sysctl(mib, 4, nullptr, &buf_size, nullptr, 0) != 0) {
    return -1;
  }
  void *buf = std::malloc(buf_size);
  if (!buf || sysctl(mib, 4, buf, &buf_size, nullptr, 0) != 0) {
    std::free(buf);
    return -1;
  }

  int best_score = -1;
  bool ambiguous = false;
  for (char *ptr = static_cast<char *>(buf);
       ptr < static_cast<char *>(buf) + buf_size;) {
    auto *ki = reinterpret_cast<struct kinfo_proc *>(ptr);
    if (ki->ki_structsize <= 0) {
      break;
    }
    ptr += ki->ki_structsize;

    char appinfo_buf[128]{};
    int32_t app_id = 0;
    if (g_get_app_info(ki->ki_pid, appinfo_buf) != 0) {
      continue;
    }
    std::memcpy(&app_id, appinfo_buf, sizeof(app_id));
    if (app_id != bigappid) {
      continue;
    }

    char process_name[ONION_PROC_PROCESS_NAME_LEN]{};
    if (g_get_name && g_get_name(ki->ki_pid, process_name) != 0) {
      continue;
    }
    if (!g_get_name) {
      std::strncpy(process_name, ki->ki_comm, sizeof(process_name) - 1);
    }

    int score = 1;
    const size_t comm_len = ::strnlen(ki->ki_comm, sizeof(ki->ki_comm));
    if (comm_len > 0 && std::strcmp(ki->ki_comm, process_name) == 0) {
      score = 4;
    } else if (comm_len > 0 &&
               std::strncmp(ki->ki_comm, process_name, comm_len) == 0) {
      score = 3;
    } else if (ki->ki_tdname[0] != '\0' &&
               std::strcmp(ki->ki_tdname, process_name) == 0) {
      score = 2;
    }

    if (score > best_score) {
      best_score = score;
      ambiguous = false;
      out->pid = ki->ki_pid;
      out->appid = app_id;
      std::strncpy(out->process_name, process_name,
                   sizeof(out->process_name) - 1);
      out->session_generation = process_generation(ki);
    } else if (score == best_score) {
      ambiguous = true;
    }
  }
  std::free(buf);
  if (best_score < 0) {
    return -1;
  }
  return ambiguous ? -2 : 0;
}

extern "C" pid_t onion_find_pid(const char *name) {
  return scan_kinfo(name, /*substr=*/false);
}

extern "C" pid_t find_pid(const char *name) {
  return onion_find_pid(name);
}

extern "C" pid_t onion_find_pid_substr(const char *substr) {
  return scan_kinfo(substr, /*substr=*/true);
}

extern "C" bool onion_proc_is_alive(pid_t pid) {
  if (pid <= 0) {
    return false;
  }
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(pid)};
  return sysctl(mib, 4, nullptr, nullptr, nullptr, 0) == 0;
}

extern "C" pid_t onion_find_pid_ex(const char *name, bool needle,
                                   bool for_bigapp) {
  if (!name) {
    return -1;
  }

  // Fast path: no big-app filter — name match only.
  if (!for_bigapp) {
    return needle ? onion_find_pid_substr(name) : onion_find_pid(name);
  }

  // Big-app path needs SCE hooks installed by the host binary (shellui).
  if (!g_get_bigapp) {
    return needle ? onion_find_pid_substr(name) : onion_find_pid(name);
  }

  int bigappid = g_get_bigapp();
  if (bigappid < 0) {
    return -1;
  }

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  if (sysctl(mib, 4, nullptr, &buf_size, nullptr, 0)) {
    return -1;
  }
  void *buf = malloc(buf_size);
  if (!buf) {
    return -1;
  }
  if (sysctl(mib, 4, buf, &buf_size, nullptr, 0)) {
    free(buf);
    return -1;
  }

  pid_t found = -1;
  char proc_name[ONION_PROC_PROCESS_NAME_LEN] = {0};

  for (char *ptr = static_cast<char *>(buf);
       ptr < static_cast<char *>(buf) + buf_size;) {
    auto *ki = reinterpret_cast<struct kinfo_proc *>(ptr);
    if (ki->ki_structsize <= 0) {
      break;
    }
    ptr += ki->ki_structsize;

    char appinfo_buf[128]{};
    int32_t app_id = 0;
    if (g_get_app_info) {
      if (g_get_app_info(ki->ki_pid, appinfo_buf) == 0) {
        app_id = *reinterpret_cast<int32_t *>(appinfo_buf);
      }
    }

    if (g_get_name) {
      if (g_get_name(ki->ki_pid, proc_name) != 0) {
        continue;
      }
    } else {
      strncpy(proc_name, ki->ki_comm, sizeof(proc_name) - 1);
    }

    if (bigappid != app_id) {
      continue;
    }
    bool success = true;
    if (name[0]) {
      success = needle ? strstr(proc_name, name) != nullptr
                       : strcmp(proc_name, name) == 0;
    }

    if (success) {
      LOG_DEBUG("[proc] matched bigapp pid=%d appid=%d process='%s' "
                "comm='%s'", (int)ki->ki_pid, app_id, proc_name,
                ki->ki_comm);
      found = ki->ki_pid;
      break;
    }
  }

  free(buf);
  return found;
}
