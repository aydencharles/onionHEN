/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Host-testable FPS composition (no kernel / ioctl).
 */
#pragma once

#include <cstdint>

namespace onion {
namespace fps {

inline constexpr int kWindow = 5;
inline constexpr int kDeadTicks = 99;
/* PHU keeps uncapped render rates for calibration; display output is capped. */
inline constexpr float kRawFpsMax = 10000.0f;
inline constexpr float kMultiPassRatio = 2.5f;
inline constexpr float kFpsMin = 1.0f;
inline constexpr float kFpsMax = 240.0f;

/** fps = Δcount / Δt. 0 if the interval is unusable. */
double hz_from_delta(uint64_t dcount, double dt_sec);

/** Arithmetic mean of n samples. 0 if n<=0. */
float rolling_mean(const float *samples, int n);

struct HybridIn {
  bool scanout_ok = false;
  float scanout = 0.f;
  bool ring_ok = false;
  float ring = 0.f;
  bool global_ok = false;
  float global = 0.f;
  /* PHU decides multi-pass from a 100-sample render/scanout average. */
  bool calibration_ready = false;
  bool multipass = false;
  int dead_ticks = 0;
};

struct HybridOut {
  bool valid = false;
  float fps = 0.f;
  uint8_t source = 0;
  int dead_ticks = 0;
};

/**
 * Main overlay value: ring is the render source, global is its fallback.
 * Once calibration identifies multi-pass, scanout is preferred. No live source
 * for kDeadTicks consecutive ticks -> valid=false.
 */
HybridOut compose(const HybridIn &in);

/** True for PPSA* / PPSB* (PS5 native retail + digital). */
bool is_ps5_native_title(const char *title_id);

/** True for CUSA* and other PS4 BC prefixes PHU treats as GnmCompat. */
bool is_ps4_bc_title(const char *title_id);

} // namespace fps
} // namespace onion
