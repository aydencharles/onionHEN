#include "cheats/sync/cheat_sync_service.hpp"

#include "cheats/cheat_repository.hpp"
#include "cheats/runtime.h"
#include "cheats/sync/cheat_catalog_registry.hpp"
#include "cheats/sync/cheat_mirror_factory.hpp"
#include "cheats/sync/http_transport_ps5.hpp"
#include "util_language.h"

#include <onion/log.h>
#include <onion/notify.h>
#include <onion/notify_i18n.h>

#include <pthread.h>
#include <sys/statvfs.h>

#include <memory>
#include <string>
#include <utility>

namespace onion::cheats::sync {
namespace {

struct FlattenCancelBridge {
  SyncCancelFn should_cancel;
  void *user;
};

int flatten_cancel_bridge(void *user) {
  const auto *bridge = static_cast<const FlattenCancelBridge *>(user);
  return bridge && bridge->should_cancel && bridge->should_cancel(bridge->user)
             ? 1
             : 0;
}

SyncStatus flatten_existing(const char *root,
                            CheatSyncEngine::InstallProgressFn progress,
                            void *progress_user, SyncCancelFn should_cancel,
                            void *cancel_user) {
  CheatRepository::ensureCheatsDir();
  FlattenCancelBridge cancel_bridge{should_cancel, cancel_user};
  const int result = CheatRepository::flattenInstallTree(
      root ? root : "", progress, progress_user,
      should_cancel ? flatten_cancel_bridge : nullptr,
      should_cancel ? &cancel_bridge : nullptr);
  if (result == ONION_CHEAT_FLATTEN_CANCELLED)
    return SyncStatus::Cancelled;
  return result == ONION_CHEAT_FLATTEN_OK ? SyncStatus::Ok : SyncStatus::Io;
}

bool enough_free_space(const char *path, unsigned long long min_bytes) {
  struct statvfs st {};
  if (!path || statvfs(path, &st) != 0) {
    return true;
  }
  const unsigned long long avail =
      static_cast<unsigned long long>(st.f_bavail) *
      static_cast<unsigned long long>(st.f_frsize);
  return avail >= min_bytes;
}

struct WorkerArg {
  CheatSyncService *svc;
  onion::Settings settings;
  std::string catalog_id;
  std::string mirror_override;
  uint32_t task_id;
};

void *worker_thunk(void *raw) {
  std::unique_ptr<WorkerArg> arg(static_cast<WorkerArg *>(raw));
  arg->svc->worker(arg->settings, std::move(arg->catalog_id),
                   std::move(arg->mirror_override), arg->task_id);
  return nullptr;
}

void on_sync_progress(const char *phase, size_t completed, size_t total,
                      void *user) {
  auto *svc = static_cast<CheatSyncService *>(user);
  if (!svc) {
    return;
  }
  int percent = -1;
  if (total > 0) {
    percent = static_cast<int>(
        (static_cast<unsigned long long>(completed) * 100ull) / total);
    if (percent > 100) {
      percent = 100;
    }
  }
  svc->noteProgress(phase ? phase : "", percent, completed, total);
}

bool should_cancel_sync(void *user) {
  auto *svc = static_cast<CheatSyncService *>(user);
  return svc && svc->cancellationRequested();
}

} // namespace

CheatSyncService &CheatSyncService::instance() {
  static CheatSyncService svc;
  return svc;
}

CheatSyncService::CheatSyncService() = default;
CheatSyncService::~CheatSyncService() = default;

void CheatSyncService::setHttpTransportForTest(IHttpTransport *http) {
  std::lock_guard<std::mutex> lock(mu_);
  test_http_ = http;
}

IHttpTransport &CheatSyncService::httpTransport() {
  if (test_http_) {
    return *test_http_;
  }
  static Ps5HttpTransport http;
  return http;
}

CheatSyncStatus CheatSyncService::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

bool CheatSyncService::cancel(uint32_t task_id) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!running_ || cancel_requested_ || task_id == 0 ||
        task_id != status_.task_id) {
      return false;
    }
    cancel_requested_ = true;
    status_.phase = "cancel";
    status_.progress_percent = -1;
  }
  onion_notify_debug(/*show_watermark=*/0, "notify.cheats.sync.cancelling");
  LOG_INFO("cheat sync cancellation requested task_id=%u", task_id);
  return true;
}

bool CheatSyncService::cancellationRequested() const {
  std::lock_guard<std::mutex> lock(mu_);
  return cancel_requested_;
}

void CheatSyncService::noteProgress(const char *phase, int percent,
                                    size_t completed, size_t total) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!running_ || cancel_requested_) {
    return;
  }
  if (phase && phase[0]) {
    status_.phase = phase;
  }
  status_.progress_percent = percent;
  status_.completed = completed;
  status_.total = total;
}

CheatSyncService::StartResult
CheatSyncService::start(const onion::Settings &settings, const char *catalog_id,
                        const char *mirror_override, uint32_t *task_id) {
  std::lock_guard<std::mutex> lock(mu_);
  if (running_) {
    if (task_id)
      *task_id = status_.task_id;
    return StartResult::AlreadyRunning;
  }
  if (!enough_free_space(ONION_DATA_ROOT, 256ull * 1024ull * 1024ull)) {
    status_ = {};
    status_.state = CheatSyncStatus::State::Error;
    status_.error = "no_space";
    if (task_id)
      *task_id = 0;
    return StartResult::Rejected;
  }

  if (next_task_id_ >= 0x7fffffffu)
    next_task_id_ = 1;
  else
    ++next_task_id_;
  const uint32_t started_task_id = next_task_id_;
  running_ = true;
  cancel_requested_ = false;
  status_ = {};
  status_.state = CheatSyncStatus::State::Running;
  status_.task_id = started_task_id;
  status_.error.clear();
  status_.phase = "start";
  status_.progress_percent = 0;
  status_.completed = 0;
  status_.total = 0;
  status_.catalog_id = catalog_id ? catalog_id : "";

  if (task_id)
    *task_id = started_task_id;
  auto *arg =
      new WorkerArg{this, settings, catalog_id ? catalog_id : "",
                    mirror_override ? mirror_override : "", started_task_id};
  pthread_t tid;
  if (pthread_create(&tid, nullptr, worker_thunk, arg) != 0) {
    delete arg;
    running_ = false;
    cancel_requested_ = false;
    status_.state = CheatSyncStatus::State::Error;
    status_.error = "thread";
    return StartResult::Rejected;
  }
  pthread_detach(tid);
  return StartResult::Started;
}

void CheatSyncService::worker(onion::Settings settings, std::string catalog_id,
                              std::string mirror_override, uint32_t task_id) {
  const ICheatCatalog *catalog =
      CheatCatalogRegistry::find(catalog_id.empty() ? nullptr : catalog_id.c_str());
  CheatSyncStatus done;
  done.task_id = task_id;
  if (!catalog) {
    done.state = CheatSyncStatus::State::Error;
    done.error = "unknown_catalog";
    onion_notify(true, "notify.cheats.sync.error", done.error.c_str());
    std::lock_guard<std::mutex> lock(mu_);
    status_ = done;
    running_ = false;
    cancel_requested_ = false;
    return;
  }

  CheatMirrorPref pref =
      static_cast<CheatMirrorPref>(settings.cheats_mirror);
  if (!mirror_override.empty()) {
    pref = CheatMirrorFactory::parsePref(mirror_override.c_str(), pref);
  }

  const int system_lang = util_cached_system_language();
  CheatMirrorPick pick =
      CheatMirrorFactory::create(pref, settings.ui_lang, system_lang);

  onion_notify(true, "notify.cheats.sync.start", pick.primary->name());
  LOG_INFO("cheat sync started catalog=%s mirror=%s", catalog->id(),
           pick.primary->name());
  LOG_DEBUG("cheat sync config pref=%s fallback=%s url=%s",
            CheatMirrorFactory::prefName(pref),
            pick.fallback ? pick.fallback->name() : "-",
            pick.primary->archiveUrl(*catalog).c_str());

  CheatSyncEngine engine(httpTransport(), flatten_existing);
  engine.setProgressHandler(on_sync_progress, this);
  engine.setCancelHandler(should_cancel_sync, this);
  CheatSyncEngine::Result result =
      engine.run(*catalog, *pick.primary, pick.fallback.get(), ONION_DATA_ROOT);
  engine.setProgressHandler(nullptr, nullptr);
  engine.setCancelHandler(nullptr, nullptr);

  done.catalog_id = catalog->id();
  done.mirror = result.used_mirror;
  done.url = result.url;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (result.status == SyncStatus::Ok && cancel_requested_)
      result.status = SyncStatus::Cancelled;

    if (result.status == SyncStatus::Cancelled) {
      done.state = CheatSyncStatus::State::Idle;
    } else if (result.status == SyncStatus::Ok) {
      done.state = CheatSyncStatus::State::Ok;
    } else {
      done.state = CheatSyncStatus::State::Error;
      done.error = result.error.empty() ? "sync_failed" : result.error;
    }

    status_ = done;
    running_ = false;
    cancel_requested_ = false;
  }

  if (result.status == SyncStatus::Cancelled) {
    onion_notify_debug(/*show_watermark=*/0, "notify.cheats.sync.cancelled");
    LOG_INFO("cheat sync cancelled catalog=%s", catalog->id());
  } else if (result.status == SyncStatus::Ok) {
    onion_notify(true, "notify.cheats.sync.ok", catalog->id());
    LOG_INFO("cheat sync ok catalog=%s mirror=%s", catalog->id(),
             pick.primary->name());
  } else {
    if (result.status == SyncStatus::Clock)
      onion_notify(true, "notify.cheats.sync.clock");
    else
      onion_notify(true, "notify.cheats.sync.error", done.error.c_str());
    LOG_ERROR("cheat sync failed catalog=%s err=%s", catalog->id(),
              done.error.c_str());
  }
}

} // namespace onion::cheats::sync
