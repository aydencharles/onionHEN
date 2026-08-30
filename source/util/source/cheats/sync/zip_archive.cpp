#include "cheats/sync/zip_archive.hpp"

#include "cheats/runtime.h"

#include <onion/fs.h>
#include <onion/log.h>

#include <miniz.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace onion::cheats::sync {
namespace {

constexpr size_t kMaxEntries = 50000;
constexpr mz_uint64 kMaxCheatFileBytes = 8ull * 1024ull * 1024ull;
constexpr mz_uint64 kMaxExtractedBytes = 128ull * 1024ull * 1024ull;

struct SelectedEntry {
  mz_uint index = 0;
  mz_uint64 size = 0;
  std::string relative;
};

bool safe_relative_path(const std::string &path) {
  if (path.empty() || path.size() >= 768 || path.front() == '/' ||
      path.find('\\') != std::string::npos) {
    return false;
  }
  size_t start = 0;
  while (start < path.size()) {
    const size_t slash = path.find('/', start);
    const size_t end = slash == std::string::npos ? path.size() : slash;
    const std::string part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") {
      return false;
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return true;
}

bool selected_relative_path(std::string *out, const char *name,
                            const char *const *roots, size_t root_count) {
  if (!out || !name || !name[0] || !roots || root_count == 0) {
    return false;
  }
  const std::string entry(name);
  for (size_t i = 0; i < root_count; ++i) {
    if (!roots[i] || !roots[i][0]) {
      continue;
    }
    std::string marker = roots[i];
    while (!marker.empty() && marker.back() == '/') {
      marker.pop_back();
    }
    marker += '/';

    size_t begin = std::string::npos;
    if (entry.rfind(marker, 0) == 0) {
      begin = 0;
    } else {
      const std::string nested = '/' + marker;
      const size_t found = entry.find(nested);
      if (found != std::string::npos) {
        begin = found + 1;
      }
    }
    if (begin == std::string::npos) {
      continue;
    }
    std::string relative = entry.substr(begin);
    if (!safe_relative_path(relative)) {
      return false;
    }
    const size_t slash = relative.find_last_of('/');
    const char *base = relative.c_str() +
                       (slash == std::string::npos ? 0 : slash + 1);
    if (onion_cheat_extension_rank(base, nullptr) < 0) {
      return false;
    }
    *out = std::move(relative);
    return true;
  }
  return false;
}

bool is_symlink(const mz_zip_archive_file_stat &stat) {
  const mode_t mode = static_cast<mode_t>(stat.m_external_attr >> 16);
  return (mode & S_IFMT) == S_IFLNK;
}

std::string parent_path(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

SyncStatus extract_cheat_zip(const char *zip_path, const char *dest_root,
                             const char *const *roots, size_t root_count,
                             SyncProgressFn progress, void *progress_user,
                             SyncCancelFn should_cancel, void *cancel_user) {
  if (!zip_path || !zip_path[0] || !dest_root || !dest_root[0] || !roots ||
      root_count == 0) {
    return SyncStatus::Rejected;
  }

  mz_zip_archive zip {};
  if (!mz_zip_reader_init_file(&zip, zip_path, 0)) {
    LOG_ERROR("cheat zip open failed path=%s err=%s", zip_path,
              mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    return SyncStatus::Protocol;
  }

  SyncStatus result = SyncStatus::Protocol;
  const mz_uint count = mz_zip_reader_get_num_files(&zip);
  if (count == 0 || count > kMaxEntries) {
    LOG_ERROR("cheat zip entry count rejected count=%u", count);
    mz_zip_reader_end(&zip);
    return SyncStatus::Protocol;
  }

  std::vector<SelectedEntry> selected;
  std::set<std::string> paths;
  mz_uint64 total = 0;
  for (mz_uint i = 0; i < count; ++i) {
    if (should_cancel && should_cancel(cancel_user)) {
      result = SyncStatus::Cancelled;
      goto done;
    }
    mz_zip_archive_file_stat stat {};
    if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
      goto done;
    }
    if (stat.m_is_directory) {
      continue;
    }
    std::string relative;
    if (!selected_relative_path(&relative, stat.m_filename, roots, root_count)) {
      continue;
    }
    if (!stat.m_is_supported || stat.m_is_encrypted || is_symlink(stat) ||
        stat.m_uncomp_size > kMaxCheatFileBytes ||
        total > kMaxExtractedBytes - stat.m_uncomp_size ||
        !paths.insert(relative).second) {
      LOG_ERROR("cheat zip entry rejected name=%s size=%llu", stat.m_filename,
                static_cast<unsigned long long>(stat.m_uncomp_size));
      goto done;
    }
    total += stat.m_uncomp_size;
    selected.push_back(SelectedEntry{i, stat.m_uncomp_size, std::move(relative)});
  }
  if (selected.empty() || !mkdir_tree(dest_root)) {
    result = selected.empty() ? SyncStatus::Protocol : SyncStatus::Io;
    goto done;
  }

  if (progress) {
    progress("extract", 0, static_cast<size_t>(total), progress_user);
  }
  {
    mz_uint64 completed = 0;
    for (const SelectedEntry &entry : selected) {
      if (should_cancel && should_cancel(cancel_user)) {
        result = SyncStatus::Cancelled;
        goto done;
      }
      const std::string dest = std::string(dest_root) + '/' + entry.relative;
      if (!mkdir_tree(parent_path(dest).c_str()) ||
          !mz_zip_reader_extract_to_file(&zip, entry.index, dest.c_str(), 0)) {
        (void)unlink(dest.c_str());
        LOG_ERROR("cheat zip extract failed path=%s err=%s", dest.c_str(),
                  mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        result = SyncStatus::Io;
        goto done;
      }
      completed += entry.size;
      if (progress) {
        progress("extract", static_cast<size_t>(completed),
                 static_cast<size_t>(total), progress_user);
      }
    }
  }
  LOG_DEBUG("cheat zip extracted files=%zu bytes=%llu", selected.size(),
            static_cast<unsigned long long>(total));
  result = SyncStatus::Ok;

done:
  mz_zip_reader_end(&zip);
  return result;
}

} // namespace onion::cheats::sync
