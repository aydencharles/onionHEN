#include "cheats/sync/cheat_sync_engine.hpp"

#include "cheats/sync/zip_archive.hpp"

#include <onion/fs.h>
#include <onion/log.h>

#include <cerrno>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace onion::cheats::sync {
namespace {

constexpr size_t kMaxArchiveBytes = 64ull * 1024ull * 1024ull;

bool valid_catalog_id(const char *id) {
  if (!id || !id[0]) {
    return false;
  }
  for (const char *p = id; *p; ++p) {
    const unsigned char ch = static_cast<unsigned char>(*p);
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                    ch == '-' || ch == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool starts_with_https(const std::string &url) {
  return url.rfind("https://", 0) == 0;
}

std::string join_path(const std::string &a, const char *b) {
  std::string out = a;
  if (!out.empty() && out.back() == '/') {
    out.pop_back();
  }
  out += '/';
  if (b && b[0] == '/') {
    ++b;
  }
  if (b) {
    out += b;
  }
  return out;
}

struct PhaseProgress {
  const char *phase = nullptr;
  SyncProgressFn fn = nullptr;
  void *user = nullptr;
};

void on_phase_progress(size_t completed, size_t total, void *user) {
  const auto *progress = static_cast<const PhaseProgress *>(user);
  if (progress && progress->fn) {
    progress->fn(progress->phase, completed, total, progress->user);
  }
}

void cleanup_temp(const std::string &root, const std::string &parent,
                  SyncProgressFn progress = nullptr,
                  void *progress_user = nullptr) {
  if (if_exists(root.c_str())) {
    PhaseProgress bridge{"cleanup", progress, progress_user};
    if (progress) {
      progress("cleanup", 0, 0, progress_user);
    }
    (void)rmtree_with_progress(root.c_str(),
                               progress ? on_phase_progress : nullptr,
                               &bridge);
  }
  (void)rmdir(parent.c_str());
}

struct DownloadProgress {
  SyncProgressFn fn = nullptr;
  void *user = nullptr;
};

void on_download_progress(size_t completed, size_t total, void *user) {
  const auto *progress = static_cast<const DownloadProgress *>(user);
  if (progress && progress->fn) {
    progress->fn("download", completed, total, progress->user);
  }
}

SyncStatus download_archive(IHttpTransport &http, const char *url,
                            const char *host, const char *path,
                            SyncProgressFn progress, void *progress_user,
                            SyncCancelFn should_cancel, void *cancel_user) {
  FILE *file = std::fopen(path, "wb");
  if (!file) {
    LOG_ERROR("cheat archive create failed path=%s errno=%d", path, errno);
    return SyncStatus::Io;
  }

  DownloadProgress bridge{progress, progress_user};
  HttpRequest req;
  req.url = url;
  req.method = "GET";
  req.user_agent = "OnionHEN";
  req.host_allow = host;
  req.status_min = 200;
  req.status_max = 299;
  req.max_body_bytes = kMaxArchiveBytes;
  req.on_progress = on_download_progress;
  req.progress_user = &bridge;
  req.should_cancel = should_cancel;
  req.cancel_user = cancel_user;

  const SyncStatus status = http.perform(
      req, [file, should_cancel, cancel_user](const void *data, size_t len) {
        if (should_cancel && should_cancel(cancel_user))
          return SyncStatus::Cancelled;
        if (!data || len == 0) {
          return SyncStatus::Ok;
        }
        return std::fwrite(data, 1, len, file) == len ? SyncStatus::Ok
                                                      : SyncStatus::Io;
      });
  const bool closed = std::fclose(file) == 0;
  if (status != SyncStatus::Ok || !closed) {
    (void)unlink(path);
    return status != SyncStatus::Ok ? status : SyncStatus::Io;
  }
  return SyncStatus::Ok;
}

} // namespace

CheatSyncEngine::CheatSyncEngine(IHttpTransport &http, FlattenFn flatten)
    : http_(http), flatten_(flatten) {}

void CheatSyncEngine::setProgressHandler(SyncProgressFn fn, void *user) {
  progress_ = fn;
  progress_user_ = user;
}

void CheatSyncEngine::setCancelHandler(SyncCancelFn fn, void *user) {
  should_cancel_ = fn;
  cancel_user_ = user;
}

SyncStatus CheatSyncEngine::tryOne(const ICheatCatalog &catalog,
                                   const ICheatMirror &mirror,
                                   const char *data_root, Result &out) {
  const std::string url = mirror.archiveUrl(catalog);
  if (!starts_with_https(url) || !mirror.archiveHost() ||
      !mirror.archiveHost()[0]) {
    out.error = "refusing non-https archive";
    return SyncStatus::Rejected;
  }
  out.url = url;
  out.used_mirror = mirror.id();

  const std::string temp_parent = join_path(data_root, "cheats_tmp");
  const std::string temp_root = join_path(temp_parent, catalog.id());
  const std::string zip_path = join_path(temp_root, "archive.zip");
  const std::string extract_root = join_path(temp_root, "extract");
  cleanup_temp(temp_root, temp_parent);
  if (should_cancel_ && should_cancel_(cancel_user_)) {
    return SyncStatus::Cancelled;
  }
  if (!mkdir_tree(temp_root.c_str())) {
    out.error = "temp directory failed";
    return SyncStatus::Io;
  }

  LOG_DEBUG("cheat archive download mirror=%s url=%s", mirror.name(),
            url.c_str());
  SyncStatus status = download_archive(http_, url.c_str(), mirror.archiveHost(),
                                       zip_path.c_str(), progress_,
                                       progress_user_, should_cancel_,
                                       cancel_user_);
  if (status != SyncStatus::Ok) {
    out.error = status == SyncStatus::Cancelled ? ""
                : status == SyncStatus::Tls      ? "tls_verify"
                : status == SyncStatus::Clock  ? "system_clock"
                                               : "archive download failed";
    cleanup_temp(temp_root, temp_parent, progress_, progress_user_);
    return status;
  }

  size_t root_count = 0;
  const char *const *roots = catalog.flattenRoots(&root_count);
  status = extract_cheat_zip(zip_path.c_str(), extract_root.c_str(), roots,
                             root_count, progress_, progress_user_,
                             should_cancel_, cancel_user_);
  if (status != SyncStatus::Ok) {
    out.error = status == SyncStatus::Cancelled ? ""
                                                : "archive extract failed";
    cleanup_temp(temp_root, temp_parent, progress_, progress_user_);
    return status;
  }

  for (size_t i = 0; i < root_count; ++i) {
    if (should_cancel_ && should_cancel_(cancel_user_)) {
      cleanup_temp(temp_root, temp_parent, progress_, progress_user_);
      return SyncStatus::Cancelled;
    }
    const std::string root = join_path(extract_root, roots[i]);
    PhaseProgress bridge{"install", progress_, progress_user_};
    if (progress_) {
      progress_("install", 0, 0, progress_user_);
    }
    status = flatten_(root.c_str(), progress_ ? on_phase_progress : nullptr,
                      &bridge, should_cancel_, cancel_user_);
    if (status != SyncStatus::Ok) {
      if (status == SyncStatus::Cancelled) {
        cleanup_temp(temp_root, temp_parent, progress_, progress_user_);
        return status;
      }
      out.error = "install failed";
      cleanup_temp(temp_root, temp_parent, progress_, progress_user_);
      return SyncStatus::Io;
    }
    if (should_cancel_ && should_cancel_(cancel_user_)) {
      cleanup_temp(temp_root, temp_parent, progress_, progress_user_);
      return SyncStatus::Cancelled;
    }
  }

  cleanup_temp(temp_root, temp_parent, progress_, progress_user_);
  out.error.clear();
  return SyncStatus::Ok;
}

CheatSyncEngine::Result CheatSyncEngine::run(const ICheatCatalog &catalog,
                                             const ICheatMirror &primary,
                                             const ICheatMirror *fallback,
                                             const char *data_root) {
  Result out;
  if (!flatten_ || !data_root || !data_root[0] ||
      !valid_catalog_id(catalog.id())) {
    out.error = "sync input rejected";
    return out;
  }

  SyncStatus status = tryOne(catalog, primary, data_root, out);
  if (is_source_failure(status) && fallback) {
    LOG_WARN("cheat archive mirror=%s failed status=%s; trying %s",
             primary.name(), sync_status_name(status), fallback->name());
    status = tryOne(catalog, *fallback, data_root, out);
  }
  out.status = status;
  return out;
}

} // namespace onion::cheats::sync
