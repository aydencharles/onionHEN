/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Skip-hook FPS sampler. Does not inject the game and does not load an SPRX.
 * Reads /dev/dce + DMAP of the game's already-loaded libSceAgcDriver.sprx.
 * Sampling approach follows PHU Games Tools by ArkSama
 * (https://github.com/ArkSama).
 */
#include "daemon_ops.hpp"
#include "globalconf.hpp"

#include <onion/fps_agc.hpp>
#include <onion/fps_bc.hpp>
#include <onion/fps_dce.hpp>
#include <onion/fps_formula.hpp>
#include <onion/fps_publish.hpp>
#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/settings.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

extern "C" int sceKernelGetProcessName(int pid, char *name);

namespace {

constexpr useconds_t kSampleUs = 33333;
constexpr unsigned kIdleSleepSec = 1;
constexpr unsigned kPidRefreshSec = 1;
constexpr uint64_t kVsyncStaleNs = 2000000000ULL;
constexpr uint64_t kDiagIntervalNs = 2000000000ULL;
constexpr int kBcDeadTicks = 15;

struct CounterState {
  uint64_t count = 0;
  double t = 0.0;
  bool have = false;
};

enum class RateStatus : uint8_t {
  NotAttempted,
  Ok,
  Priming,
  ClockInvalid,
  CounterStalled,
  CounterRegressed,
  RawOutOfRange,
};

struct RateDiag {
  RateStatus status = RateStatus::NotAttempted;
  uint64_t count = 0;
  uint64_t delta = 0;
  double dt = 0.0;
  double raw_hz = 0.0;
};

const char *rate_status_name(RateStatus status) {
  switch (status) {
  case RateStatus::NotAttempted:
    return "not-attempted";
  case RateStatus::Ok:
    return "ok";
  case RateStatus::Priming:
    return "priming";
  case RateStatus::ClockInvalid:
    return "clock-invalid";
  case RateStatus::CounterStalled:
    return "counter-stalled";
  case RateStatus::CounterRegressed:
    return "counter-regressed";
  case RateStatus::RawOutOfRange:
    return "raw-out-of-range";
  }
  return "unknown";
}

struct CalibrationState {
  double ratio_sum = 0.0;
  unsigned samples = 0;
  bool ready = false;
  bool multipass = false;

  void reset() { *this = {}; }

  double average() const {
    return samples == 0 ? 0.0 : ratio_sum / static_cast<double>(samples);
  }

  void update(float render, float scanout) {
    if (ready || render < onion::fps::kFpsMin || scanout < 25.0f ||
        scanout > onion::fps::kFpsMax)
      return;
    const double ratio = static_cast<double>(render) /
                         static_cast<double>(scanout);
    /* PHU ignores implausible pairs while accumulating its 100 samples. */
    if (ratio < 0.5 || ratio > 200.0)
      return;
    ratio_sum += ratio;
    if (++samples < 100)
      return;
    const double average = ratio_sum / static_cast<double>(samples);
    multipass = average > onion::fps::kMultiPassRatio;
    ready = true;
    LOG_INFO("fps: calibration ready ratio=%.2f mode=%s", average,
             multipass ? "multi-pass" : "single-pass");
  }
};

/*
 * The V-sync sampler and the render sampler are independent. Keep their
 * hand-off in process memory so only the render sampler writes the published
 * FPS record. The sequence also keeps a reader from observing a mixed sample.
 */
struct VsyncBus {
  std::atomic<uint32_t> seq{0};
  std::atomic<uint32_t> fps_bits{0};
  std::atomic<uint64_t> title_hash{0};
  std::atomic<uint64_t> timestamp_ns{0};
  std::atomic<uint8_t> valid{0};
};

VsyncBus g_vsync_bus;

enum class VsyncReadStatus : uint8_t {
  NotAttempted,
  Ok,
  InvalidArgument,
  BusInvalid,
  TitleMismatch,
  TimestampInvalid,
  Stale,
  FpsOutOfRange,
  Contended,
};

struct VsyncReadDiag {
  VsyncReadStatus status = VsyncReadStatus::NotAttempted;
  double age_ms = 0.0;
  float fps = 0.f;
};

const char *vsync_read_status_name(VsyncReadStatus status) {
  switch (status) {
  case VsyncReadStatus::NotAttempted:
    return "not-attempted";
  case VsyncReadStatus::Ok:
    return "ok";
  case VsyncReadStatus::InvalidArgument:
    return "invalid-argument";
  case VsyncReadStatus::BusInvalid:
    return "bus-invalid";
  case VsyncReadStatus::TitleMismatch:
    return "title-mismatch";
  case VsyncReadStatus::TimestampInvalid:
    return "timestamp-invalid";
  case VsyncReadStatus::Stale:
    return "stale";
  case VsyncReadStatus::FpsOutOfRange:
    return "fps-out-of-range";
  case VsyncReadStatus::Contended:
    return "contended";
  }
  return "unknown";
}

double monotonic_sec() {
  struct timespec ts {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0.0;
  return static_cast<double>(ts.tv_sec) +
         static_cast<double>(ts.tv_nsec) * 1e-9;
}

uint64_t monotonic_ns() {
  struct timespec ts {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

bool diag_due(uint64_t &last_ns) {
  const uint64_t now = monotonic_ns();
  if (now == 0)
    return false;
  if (last_ns != 0 && now >= last_ns && now - last_ns < kDiagIntervalNs)
    return false;
  last_ns = now;
  return true;
}

uint64_t title_hash(const char *tid) {
  if (!tid)
    return 0;
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(tid);
       *p != 0; ++p) {
    hash ^= static_cast<uint64_t>(*p);
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint32_t float_bits(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float bits_float(uint32_t bits) {
  float value = 0.f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void publish_vsync_state(bool valid, const char *tid, float fps) {
  uint32_t seq = g_vsync_bus.seq.load(std::memory_order_relaxed);
  if (seq & 1u)
    ++seq;
  g_vsync_bus.seq.store(seq + 1u, std::memory_order_release);
  g_vsync_bus.fps_bits.store(float_bits(fps), std::memory_order_relaxed);
  g_vsync_bus.title_hash.store(title_hash(tid), std::memory_order_relaxed);
  g_vsync_bus.timestamp_ns.store(monotonic_ns(), std::memory_order_relaxed);
  g_vsync_bus.valid.store(valid ? 1 : 0, std::memory_order_relaxed);
  g_vsync_bus.seq.store(seq + 2u, std::memory_order_release);
}

void clear_vsync_state() { publish_vsync_state(false, nullptr, 0.f); }

bool read_vsync_fps(const char *tid, float *out, VsyncReadDiag *diag) {
  if (diag)
    *diag = {};
  if (!tid || !out) {
    if (diag)
      diag->status = VsyncReadStatus::InvalidArgument;
    return false;
  }

  const uint64_t wanted_title = title_hash(tid);
  for (int attempt = 0; attempt < 4; ++attempt) {
    const uint32_t seq1 = g_vsync_bus.seq.load(std::memory_order_acquire);
    if (seq1 & 1u)
      continue;
    const uint32_t fps_bits =
        g_vsync_bus.fps_bits.load(std::memory_order_relaxed);
    const uint64_t sample_title =
        g_vsync_bus.title_hash.load(std::memory_order_relaxed);
    const uint64_t timestamp =
        g_vsync_bus.timestamp_ns.load(std::memory_order_relaxed);
    const bool valid = g_vsync_bus.valid.load(std::memory_order_relaxed) != 0;
    const uint32_t seq2 = g_vsync_bus.seq.load(std::memory_order_acquire);
    if (seq1 != seq2 || (seq2 & 1u))
      continue;
    if (diag)
      diag->fps = bits_float(fps_bits);
    if (!valid) {
      if (diag)
        diag->status = VsyncReadStatus::BusInvalid;
      return false;
    }
    if (sample_title != wanted_title) {
      if (diag)
        diag->status = VsyncReadStatus::TitleMismatch;
      return false;
    }
    const uint64_t now = monotonic_ns();
    if (timestamp == 0 || now == 0 || now < timestamp) {
      if (diag)
        diag->status = VsyncReadStatus::TimestampInvalid;
      return false;
    }
    if (diag)
      diag->age_ms = static_cast<double>(now - timestamp) / 1000000.0;
    if (now - timestamp > kVsyncStaleNs) {
      if (diag)
        diag->status = VsyncReadStatus::Stale;
      return false;
    }
    const float fps = bits_float(fps_bits);
    if (fps < onion::fps::kFpsMin || fps > onion::fps::kFpsMax) {
      if (diag)
        diag->status = VsyncReadStatus::FpsOutOfRange;
      return false;
    }
    *out = fps;
    if (diag)
      diag->status = VsyncReadStatus::Ok;
    return true;
  }
  if (diag)
    diag->status = VsyncReadStatus::Contended;
  return false;
}

RateDiag sample_hz(CounterState &st, uint64_t count, float *out) {
  RateDiag diag;
  diag.count = count;
  const double now = monotonic_sec();
  if (now <= 0.0) {
    diag.status = RateStatus::ClockInvalid;
    return diag;
  }
  if (!st.have) {
    st.count = count;
    st.t = now;
    st.have = true;
    diag.status = RateStatus::Priming;
    return diag;
  }
  diag.dt = now - st.t;
  if (now <= st.t) {
    st.count = count;
    st.t = now;
    diag.status = RateStatus::ClockInvalid;
    return diag;
  }
  if (count <= st.count) {
    diag.status = count == st.count ? RateStatus::CounterStalled
                                    : RateStatus::CounterRegressed;
    st.count = count;
    st.t = now;
    return diag;
  }
  diag.delta = count - st.count;
  diag.raw_hz = static_cast<double>(diag.delta) / diag.dt;
  const double hz = onion::fps::hz_from_delta(diag.delta, diag.dt);
  st.count = count;
  st.t = now;
  if (hz <= 0.0) {
    diag.status = RateStatus::RawOutOfRange;
    return diag;
  }
  *out = static_cast<float>(hz);
  diag.status = RateStatus::Ok;
  return diag;
}

void publish_invalid(int pid, const char *tid) {
  OnionFpsSample s {};
  s.pid = pid;
  s.valid = 0;
  s.unix_ns = onion_fps_realtime_ns();
  if (tid)
    std::strncpy(s.title_id, tid, sizeof(s.title_id) - 1);
  onion::fps::publish(s);
}

} // namespace

void *vsync_fps_sampler_thread(void *args) noexcept {
  (void)args;
  LOG_INFO("vsync fps sampler started (scanout, %u us)",
           static_cast<unsigned>(kSampleUs));

  onion::fps::DceSource dce;
  CounterState scanout_st;
  std::string cached_tid;
  bool logged_active = false;
  bool had_app = false;
  int last_enabled = -1;
  uint64_t last_diag_ns = 0;

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const onion::Settings cfg = g_settings.snapshot();
    const bool enabled = cfg.overlay_enabled && cfg.overlay_fps;
    if (last_enabled != static_cast<int>(enabled)) {
      LOG_INFO("fps-diag: vsync config overlay=%d fps=%d enabled=%d",
               cfg.overlay_enabled ? 1 : 0, cfg.overlay_fps ? 1 : 0,
               enabled ? 1 : 0);
      last_enabled = static_cast<int>(enabled);
    }
    if (!enabled) {
      cached_tid.clear();
      scanout_st = {};
      logged_active = false;
      had_app = false;
      clear_vsync_state();
      sleep(kIdleSleepSec);
      continue;
    }

    std::string tid;
    int app_id = 0;
    if (!Get_Running_App_TID(tid, app_id)) {
      if (had_app)
        LOG_INFO("fps-diag: vsync target cleared (no running big app)");
      had_app = false;
      cached_tid.clear();
      scanout_st = {};
      logged_active = false;
      clear_vsync_state();
      if (diag_due(last_diag_ns))
        LOG_INFO("fps-diag: vsync state=no-app dce_open=%d unavailable=%d "
                 "errno=%d",
                 dce.is_open() ? 1 : 0, dce.unavailable() ? 1 : 0,
                 dce.last_errno());
      sleep(kIdleSleepSec);
      continue;
    }

    if (tid != cached_tid) {
      scanout_st = {};
      logged_active = false;
      clear_vsync_state();
      cached_tid = tid;
      LOG_INFO("fps-diag: vsync target tid=%s app=%d native=%d bc=%d",
               tid.c_str(), app_id,
               onion::fps::is_ps5_native_title(tid.c_str()) ? 1 : 0,
               onion::fps::is_ps4_bc_title(tid.c_str()) ? 1 : 0);
    }
    had_app = true;

    if (onion::fps::is_ps4_bc_title(tid.c_str())) {
      clear_vsync_state();
      if (diag_due(last_diag_ns))
        LOG_INFO("fps-diag: vsync tid=%s app=%d state=ps4-bc-skipped",
                 tid.c_str(), app_id);
      usleep(kSampleUs);
      continue;
    }

    uint64_t count = 0;
    float hz = 0.f;
    onion::fps::DceSampleStatus sample_status =
        onion::fps::DceSampleStatus::NotAttempted;
    const bool sample_ok = dce.sample(&count, &sample_status);
    RateDiag rate_diag;
    if (sample_ok)
      rate_diag = sample_hz(scanout_st, count, &hz);
    const bool bus_updated = sample_ok && rate_diag.status == RateStatus::Ok;
    if (bus_updated) {
      publish_vsync_state(true, tid.c_str(), hz);
      if (!logged_active) {
        LOG_INFO("fps: vsync source active tid=%s fps=%.2f", tid.c_str(), hz);
        logged_active = true;
      }
    }
    if (diag_due(last_diag_ns)) {
      LOG_INFO("fps-diag: vsync tid=%s app=%d dce_open=%d unavailable=%d "
               "abi=%s sample=%s errno=%d count=%llu rate=%s delta=%llu "
               "dt=%.4f raw_hz=%.2f bus_update=%d fps=%.2f",
               tid.c_str(), app_id, dce.is_open() ? 1 : 0,
               dce.unavailable() ? 1 : 0,
               dce.request_abi_name(),
               onion::fps::dce_sample_status_name(sample_status),
               dce.last_errno(), static_cast<unsigned long long>(count),
               rate_status_name(rate_diag.status),
               static_cast<unsigned long long>(rate_diag.delta), rate_diag.dt,
               rate_diag.raw_hz, bus_updated ? 1 : 0,
               bus_updated ? hz : 0.f);
    }
    usleep(kSampleUs);
  }

  clear_vsync_state();
  return nullptr;
}

void *fps_sampler_thread(void *args) noexcept {
  (void)args;
  LOG_INFO("fps sampler started (skip-hook, %u us)",
           static_cast<unsigned>(kSampleUs));

  onion::fps::AgcSources agc;
  onion::fps::BcGnmHook bc_hook;
  onion::fps::BcHookStatus bc_last_status =
      onion::fps::BcHookStatus::NotAttempted;
  CounterState bc_st;
  uint64_t last_bc_install_ns = 0;
  int bc_dead = 0;
  CounterState ring_st;
  CounterState global_st;
  CalibrationState calibration;
  float window[onion::fps::kWindow] {};
  int window_n = 0;
  int window_i = 0;
  int dead_ticks = 0;
  bool logged_vsync_fallback = false;
  bool had_app = false;
  pid_t cached_pid = -1;
  std::string cached_tid;
  std::string cached_proc_name;
  time_t last_pid_check = 0;
  time_t last_cfg_check = 0;
  int last_enabled = -1;
  int last_publish_valid = -1;
  uint64_t last_diag_ns = 0;
  uint64_t last_publish_diag_ns = 0;
  uint64_t last_fallback_diag_ns = 0;

  (void)onion::fps::publish_open();

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const time_t now_wall = time(nullptr);
    if (now_wall - last_cfg_check >= 1) {
      (void)LoadSettings(false);
      last_cfg_check = now_wall;
    }
    const onion::Settings cfg = g_settings.snapshot();
    const bool enabled = cfg.overlay_enabled && cfg.overlay_fps;
    if (last_enabled != static_cast<int>(enabled)) {
      LOG_INFO("fps-diag: render config overlay=%d fps=%d enabled=%d",
               cfg.overlay_enabled ? 1 : 0, cfg.overlay_fps ? 1 : 0,
               enabled ? 1 : 0);
      last_enabled = static_cast<int>(enabled);
    }
    if (!enabled) {
      last_publish_valid = -1;
      publish_invalid(-1, nullptr);
      sleep(kIdleSleepSec);
      continue;
    }

    std::string tid;
    int app_id = 0;
    if (!Get_Running_App_TID(tid, app_id)) {
      if (had_app)
        LOG_INFO("fps-diag: render target cleared (no running big app)");
      had_app = false;
      cached_pid = -1;
      cached_tid.clear();
      cached_proc_name.clear();
      ring_st = {};
      global_st = {};
      calibration.reset();
      agc.reset();
      bc_hook.reset();
      bc_st = {};
      bc_last_status = onion::fps::BcHookStatus::NotAttempted;
      last_bc_install_ns = 0;
      bc_dead = 0;
      window_n = 0;
      window_i = 0;
      dead_ticks = 0;
      logged_vsync_fallback = false;
      last_publish_valid = -1;
      publish_invalid(-1, nullptr);
      if (diag_due(last_diag_ns))
        LOG_INFO("fps-diag: render state=no-app publish_valid=0");
      sleep(kIdleSleepSec);
      continue;
    }
    had_app = true;

    const time_t now = time(nullptr);
    const bool cached_alive =
        cached_pid > 0 && onion_proc_is_alive(cached_pid);
    if (cached_pid <= 0 || !cached_alive ||
        tid != cached_tid || now - last_pid_check >= kPidRefreshSec) {
      const pid_t old_pid = cached_pid;
      const std::string old_tid = cached_tid;
      const pid_t pid = onion_find_pid_ex("", false, true, false);
      last_pid_check = now;
      if (pid != cached_pid || tid != cached_tid) {
        ring_st = {};
        global_st = {};
        calibration.reset();
        agc.reset();
        bc_hook.reset();
        bc_st = {};
        bc_last_status = onion::fps::BcHookStatus::NotAttempted;
        last_bc_install_ns = 0;
        bc_dead = 0;
        window_n = 0;
        window_i = 0;
        dead_ticks = 0;
        logged_vsync_fallback = false;
        last_publish_valid = -1;
      }
      cached_pid = pid;
      cached_tid = tid;
      char proc_name[64]{};
      if (pid > 0 && sceKernelGetProcessName(pid, proc_name) >= 0)
        cached_proc_name = proc_name;
      else
        cached_proc_name = "?";
      if (pid != old_pid || tid != old_tid) {
        LOG_INFO("fps-diag: render target tid=%s app=%d pid=%d name=%s "
                 "old_pid=%d pid_alive=%d native=%d bc=%d",
                 tid.c_str(), app_id, static_cast<int>(pid),
                 cached_proc_name.c_str(),
                 static_cast<int>(old_pid),
                 pid > 0 && onion_proc_is_alive(pid) ? 1 : 0,
                 onion::fps::is_ps5_native_title(tid.c_str()) ? 1 : 0,
                 onion::fps::is_ps4_bc_title(tid.c_str()) ? 1 : 0);
      }
    }

    if (cached_pid <= 0) {
      publish_invalid(-1, nullptr);
      if (diag_due(last_diag_ns))
        LOG_INFO("fps-diag: render tid=%s app=%d pid=%d name=%s state=%s "
                 "publish_valid=0",
                 tid.c_str(), app_id, static_cast<int>(cached_pid),
                 cached_proc_name.c_str(), "pid-not-found");
      usleep(kSampleUs);
      continue;
    }

    if (onion::fps::is_ps4_bc_title(tid.c_str())) {
      /* PS4 BC: measure the GNM flip counter exposed by the remote detour. */
      float bc_hz = 0.f;
      bool bc_ok = false;
      uint64_t bc_count = 0;
      RateDiag bc_rate_diag;
      onion::fps::BcHookStatus bc_status =
          onion::fps::BcHookStatus::NotAttempted;
      if (!bc_hook.installed()) {
        const uint64_t now_ns = monotonic_ns();
        if (last_bc_install_ns == 0 ||
            now_ns - last_bc_install_ns >= 500000000ULL) {
          last_bc_install_ns = now_ns;
          bc_status = bc_hook.install(cached_pid);
          bc_last_status = bc_status;
          LOG_INFO("fps-diag: bc-hook install tid=%s pid=%d status=%s "
                   "target=0x%llx counter=0x%llx len=%u",
                   tid.c_str(), static_cast<int>(cached_pid),
                   onion::fps::bc_hook_status_name(bc_status),
                   static_cast<unsigned long long>(bc_hook.target_addr()),
                   static_cast<unsigned long long>(bc_hook.counter_addr()),
                   static_cast<unsigned>(bc_hook.hook_len()));
        }
      }
      if (bc_hook.installed()) {
        if (bc_hook.sample(&bc_count, &bc_status)) {
          bc_rate_diag = sample_hz(bc_st, bc_count, &bc_hz);
          bc_ok = bc_rate_diag.status == RateStatus::Ok;
        }
        if (!bc_ok && bc_last_status != bc_status) {
          bc_last_status = bc_status;
          LOG_INFO("fps-diag: bc-hook sample tid=%s pid=%d status=%s "
                   "counter=%llu",
                   tid.c_str(), static_cast<int>(cached_pid),
                   onion::fps::bc_hook_status_name(bc_status),
                   static_cast<unsigned long long>(bc_count));
        }
      }
      if (bc_ok) {
        bc_dead = 0;
        window[window_i] = bc_hz;
        window_i = (window_i + 1) % onion::fps::kWindow;
        if (window_n < onion::fps::kWindow)
          ++window_n;
        const float smoothed = onion::fps::rolling_mean(window, window_n);
        OnionFpsSample bc_sample {};
        bc_sample.pid = static_cast<int>(cached_pid);
        bc_sample.valid = 1;
        bc_sample.source = ONION_FPS_SRC_BC;
        bc_sample.fps = smoothed;
        bc_sample.unix_ns = onion_fps_realtime_ns();
        std::strncpy(bc_sample.title_id, tid.c_str(),
                     sizeof(bc_sample.title_id) - 1);
        onion::fps::publish(bc_sample);
        if (last_publish_valid != 1) {
          LOG_INFO("fps: bc source active tid=%s pid=%d fps=%.2f "
                   "counter=%llu",
                   tid.c_str(), static_cast<int>(cached_pid), smoothed,
                   static_cast<unsigned long long>(bc_count));
          last_publish_valid = 1;
        }
      } else if (last_publish_valid == 1) {
        ++bc_dead;
        if (bc_dead >= kBcDeadTicks) {
          last_publish_valid = -1;
          publish_invalid(static_cast<int>(cached_pid), tid.c_str());
        }
      } else {
        publish_invalid(static_cast<int>(cached_pid), tid.c_str());
      }
      if (diag_due(last_diag_ns))
        LOG_INFO("fps-diag: bc-render tid=%s pid=%d installed=%d "
                 "counter=%llu rate=%s hz=%.2f publish_valid=%d dead=%d",
                 tid.c_str(), static_cast<int>(cached_pid),
                 bc_hook.installed() ? 1 : 0,
                 static_cast<unsigned long long>(bc_count),
                 rate_status_name(bc_rate_diag.status), bc_hz,
                 bc_ok ? 1 : 0, bc_dead);
      usleep(kSampleUs);
      continue;
    }

    onion::fps::HybridIn hin;
    hin.dead_ticks = dead_ticks;
    uint64_t ring_count = 0;
    uint64_t global_count = 0;
    float ring_hz = 0.f;
    float global_hz = 0.f;
    onion::fps::AgcSampleStatus ring_sample_status =
        onion::fps::AgcSampleStatus::NotAttempted;
    onion::fps::AgcSampleStatus global_sample_status =
        onion::fps::AgcSampleStatus::NotAttempted;
    RateDiag ring_rate_diag;
    RateDiag global_rate_diag;
    uint32_t ring_size_diag = 0;
    uint32_t ring_idx_diag = 0;
    uint64_t global_va_diag = 0;
    if (onion::fps::is_ps5_native_title(tid.c_str())) {
      if (agc.sample_ring(cached_pid, &ring_count, &ring_sample_status)) {
        ring_rate_diag = sample_hz(ring_st, ring_count, &ring_hz);
      }
      ring_size_diag = agc.ring_size();
      ring_idx_diag = agc.ring_write_index();
      if (ring_rate_diag.status == RateStatus::Ok) {
        hin.ring_ok = true;
        hin.ring = ring_hz;
      }
      if (agc.sample_global(cached_pid, &global_count,
                            &global_sample_status)) {
        global_rate_diag = sample_hz(global_st, global_count, &global_hz);
      }
      if (global_rate_diag.status == RateStatus::Ok) {
        hin.global_ok = true;
        hin.global = global_hz;
      }
      global_va_diag = agc.global_va();
    }

    float vsync_fps = 0.f;
    VsyncReadDiag vsync_diag;
    const bool have_vsync =
        read_vsync_fps(tid.c_str(), &vsync_fps, &vsync_diag);
    const bool render_live = hin.ring_ok || hin.global_ok;
    if (have_vsync) {
      hin.scanout_ok = true;
      hin.scanout = vsync_fps;
      const float render_fps = hin.ring_ok ? hin.ring : hin.global;
      if (render_live)
        calibration.update(render_fps, vsync_fps);
    }
    hin.calibration_ready = calibration.ready;
    hin.multipass = calibration.multipass;

    onion::fps::HybridOut hout = onion::fps::compose(hin);
    bool used_vsync_fallback = false;
    if (have_vsync && (!render_live || !hout.valid)) {
      /* Render can be absent or outside the displayable range. PHU exposes
       * scanout as the useful FPS in that case; keep that hand-off explicit. */
      hout.valid = true;
      hout.fps = vsync_fps;
      hout.source = ONION_FPS_SRC_SCANOUT;
      hout.dead_ticks = 0;
      used_vsync_fallback = true;
      if (!logged_vsync_fallback) {
        if (diag_due(last_fallback_diag_ns))
          LOG_INFO("fps: using vsync fallback tid=%s", tid.c_str());
        logged_vsync_fallback = true;
      }
    } else if (hout.valid) {
      logged_vsync_fallback = false;
    }
    dead_ticks = hout.dead_ticks;
    if (hout.valid) {
      window[window_i] = hout.fps;
      window_i = (window_i + 1) % onion::fps::kWindow;
      if (window_n < onion::fps::kWindow)
        ++window_n;
      hout.fps = onion::fps::rolling_mean(window, window_n);
    }

    OnionFpsSample sample {};
    sample.pid = static_cast<int>(cached_pid);
    sample.valid = hout.valid ? 1 : 0;
    sample.source = hout.source;
    sample.fps = hout.valid ? hout.fps : 0.f;
    sample.unix_ns = onion_fps_realtime_ns();
    std::strncpy(sample.title_id, tid.c_str(), sizeof(sample.title_id) - 1);
    onion::fps::publish(sample);

    if (last_publish_valid != static_cast<int>(sample.valid)) {
      if (diag_due(last_publish_diag_ns))
        LOG_INFO("fps-diag: publish state tid=%s pid=%d valid=%d fps=%.2f "
                 "source=0x%02x dead=%d",
                 tid.c_str(), sample.pid, sample.valid ? 1 : 0, sample.fps,
                 static_cast<unsigned>(sample.source), dead_ticks);
      last_publish_valid = static_cast<int>(sample.valid);
    }
    if (diag_due(last_diag_ns)) {
      LOG_INFO("fps-diag: render-source tid=%s app=%d pid=%d name=%s alive=%d "
               "native=%d ring_sample=%s ring_size=%u ring_idx=%u "
               "ring_count=%llu ring_rate=%s "
               "ring_delta=%llu ring_dt=%.4f ring_hz=%.2f global_sample=%s "
               "global_va=0x%llx global_count=%llu global_rate=%s "
               "global_delta=%llu "
               "global_dt=%.4f global_hz=%.2f",
               tid.c_str(), app_id, static_cast<int>(cached_pid),
               cached_proc_name.c_str(),
               onion_proc_is_alive(cached_pid) ? 1 : 0,
               onion::fps::is_ps5_native_title(tid.c_str()) ? 1 : 0,
               onion::fps::agc_sample_status_name(ring_sample_status),
               ring_size_diag, ring_idx_diag,
               static_cast<unsigned long long>(ring_count),
               rate_status_name(ring_rate_diag.status),
               static_cast<unsigned long long>(ring_rate_diag.delta),
               ring_rate_diag.dt, ring_rate_diag.raw_hz,
               onion::fps::agc_sample_status_name(global_sample_status),
               static_cast<unsigned long long>(global_va_diag),
               static_cast<unsigned long long>(global_count),
               rate_status_name(global_rate_diag.status),
               static_cast<unsigned long long>(global_rate_diag.delta),
               global_rate_diag.dt, global_rate_diag.raw_hz);
      LOG_INFO("fps-diag: render-output tid=%s pid=%d vsync=%s age_ms=%.1f "
               "vsync_fps=%.2f calibration=%u/100 avg=%.2f ready=%d "
               "multipass=%d fallback=%d publish_valid=%d publish_fps=%.2f "
               "source=0x%02x dead=%d",
               tid.c_str(), static_cast<int>(cached_pid),
               vsync_read_status_name(vsync_diag.status), vsync_diag.age_ms,
               vsync_diag.fps, calibration.samples, calibration.average(),
               calibration.ready ? 1 : 0, calibration.multipass ? 1 : 0,
               used_vsync_fallback ? 1 : 0, sample.valid ? 1 : 0,
               sample.fps, static_cast<unsigned>(sample.source), dead_ticks);
    }

    usleep(kSampleUs);
  }

  onion::fps::publish_close();
  return nullptr;
}
