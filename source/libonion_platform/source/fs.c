/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/fs.h>
#include <onion/log.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool is_directory_entry(const char *path, unsigned char type) {
  struct stat st;
  if (type == DT_DIR) {
    return true;
  }
  if (type != DT_UNKNOWN || lstat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

bool if_exists(const char *path) {
  struct stat buffer;
  if (!path) {
    return false;
  }
  return stat(path, &buffer) == 0;
}

bool touch_file(const char *path) {
  if (!path) {
    return false;
  }
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd > 0) {
    close(fd);
    return true;
  }
  return false;
}

bool mkdir_tree(const char *path) {
  char current[1024];
  size_t n;
  size_t i;

  if (path == NULL || path[0] == '\0') {
    return false;
  }
  n = strlen(path);
  if (n >= sizeof(current)) {
    return false;
  }
  for (i = 0; i < n; ++i) {
    current[i] = path[i];
    current[i + 1] = '\0';
    if (path[i] != '/' || i == 0) {
      continue;
    }
    current[i] = '\0';
    if (current[0] != '\0' && mkdir(current, 0777) != 0 && errno != EEXIST) {
      return false;
    }
    current[i] = '/';
  }
  return mkdir(current, 0777) == 0 || errno == EEXIST;
}

static size_t count_tree_entries(const char *path) {
  DIR *dir = opendir(path);
  size_t count = 1; /* The directory itself. */
  struct dirent *entry;

  if (dir == NULL) {
    return count;
  }
  while ((entry = readdir(dir)) != NULL) {
    char child[1024];
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    count += is_directory_entry(child, entry->d_type)
                 ? count_tree_entries(child)
                 : 1;
  }
  closedir(dir);
  return count;
}

static bool rmtree_impl(const char *path, onion_fs_progress_fn progress,
                        void *user, size_t *completed, size_t total) {
  if (!path) {
    return false;
  }

  DIR *dir = opendir(path);
  if (dir == NULL) {
    LOG_ERROR("Error opening directory %s", path);
    return false;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    bool is_directory;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char child[1024];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    is_directory = is_directory_entry(child, entry->d_type);

    if (is_directory) {
      (void)rmtree_impl(child, progress, user, completed, total);
    } else if (unlink(child) != 0) {
      LOG_ERROR("Error deleting file %s", child);
    }
    if (!is_directory && completed != NULL) {
      ++(*completed);
      if (progress != NULL) {
        progress(*completed, total, user);
      }
    }
  }

  closedir(dir);

  if (rmdir(path) != 0) {
    LOG_ERROR("Error deleting folder %s", path);
  }
  if (completed != NULL) {
    ++(*completed);
    if (progress != NULL) {
      progress(*completed, total, user);
    }
  }
  return true;
}

bool rmtree(const char *path) {
  return rmtree_impl(path, NULL, NULL, NULL, 0);
}

bool rmtree_with_progress(const char *path, onion_fs_progress_fn progress,
                          void *user) {
  size_t completed = 0;
  size_t total;
  if (path == NULL) {
    return false;
  }
  total = count_tree_entries(path);
  if (progress != NULL) {
    progress(0, total, user);
  }
  return rmtree_impl(path, progress, user, &completed, total);
}
