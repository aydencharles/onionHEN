/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_formula.hpp>
#include <onion/fps_sample.h>

#include <cstring>

namespace onion {
namespace fps {

double hz_from_delta(uint64_t dcount, double dt_sec) {
  if (dcount == 0 || dt_sec <= 0.0)
    return 0.0;
  const double hz = static_cast<double>(dcount) / dt_sec;
  /* Keep uncapped rates for PHU-style calibration. The final published FPS
   * still uses kFpsMax; rejecting them here loses the only multi-pass signal. */
  if (hz < static_cast<double>(kFpsMin) || hz > static_cast<double>(kRawFpsMax))
    return 0.0;
  return hz;
}

float rolling_mean(const float *samples, int n) {
  if (!samples || n <= 0)
    return 0.f;
  double sum = 0.0;
  for (int i = 0; i < n; ++i)
    sum += static_cast<double>(samples[i]);
  return static_cast<float>(sum / static_cast<double>(n));
}

HybridOut compose(const HybridIn &in) {
  HybridOut out;
  out.dead_ticks = in.dead_ticks;

  const bool scanout = in.scanout_ok && in.scanout >= kFpsMin &&
                       in.scanout <= kFpsMax;
  const bool ring = in.ring_ok && in.ring >= kFpsMin;
  const bool global = in.global_ok && in.global >= kFpsMin;
  if (!scanout && !ring && !global) {
    out.dead_ticks = in.dead_ticks + 1;
    return out;
  }
  out.dead_ticks = 0;

  /* PHU's Tier 1E ring is the primary render rate. Tier 1F is only used when
   * the ring is unavailable; never take max(ring, global), as that mixes two
   * counters with different semantics. */
  const bool render = ring || global;
  const float render_fps = ring ? in.ring : (global ? in.global : 0.f);

  float main = 0.f;
  uint8_t src = 0;
  if (scanout)
    src |= ONION_FPS_SRC_SCANOUT;
  if (ring)
    src |= ONION_FPS_SRC_RING;
  if (global)
    src |= ONION_FPS_SRC_GLOBAL;

  if (in.calibration_ready && in.multipass && scanout && render) {
    main = in.scanout;
    src |= ONION_FPS_SRC_MULTIPASS;
  } else {
    main = render ? render_fps : (scanout ? in.scanout : 0.f);
    if (scanout && render)
      src |= ONION_FPS_SRC_HYBRID;
  }

  /* Render counters may be uncapped. They remain useful for calibration but
   * are not displayable until a scanout fallback is available. */
  if (main < kFpsMin || main > kFpsMax) {
    out.dead_ticks = in.dead_ticks + 1;
    return out;
  }
  if (out.dead_ticks >= kDeadTicks)
    return out;

  out.valid = true;
  out.fps = main;
  out.source = src;
  return out;
}

bool is_ps5_native_title(const char *title_id) {
  if (!title_id || std::strlen(title_id) < 4)
    return false;
  /* PS5 native titles use the PPSA prefix. */
  return std::strncmp(title_id, "PPSA", 4) == 0;
}

bool is_ps4_bc_title(const char *title_id) {
  if (!title_id || std::strlen(title_id) < 4)
    return false;
  /* PS4 BC titles use the CUSA prefix. */
  return std::strncmp(title_id, "CUSA", 4) == 0;
}

} // namespace fps
} // namespace onion
