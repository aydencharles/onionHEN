#pragma once

#include <cmath>
#include <cstddef>

namespace onion::overlay {

inline constexpr float kOffscreen = -4096.0f;
inline constexpr float kFontH = 18.0f;
inline constexpr float kBarExtra = 6.0f;
inline constexpr float kTextTopInset = 5.0f;
inline constexpr float kGap = 28.0f;
inline constexpr float kEdgeInset = 24.0f;
inline constexpr float kCpuAvgWidth = 190.0f;
inline constexpr float kCpuAllWidth = 420.0f;
inline constexpr float kGpuWidth = 190.0f;
inline constexpr float kRamWidth = 170.0f;
inline constexpr float kIpWidth = 200.0f;
inline constexpr float kFanWidth = 130.0f;
inline constexpr float kFpsWidth = 130.0f;

enum class BarEdge { Top, Bottom };
enum class BarAlign { Left, Center, Right };

inline BarEdge bar_edge_from_pos(int pos) {
  return (pos == 2 || pos == 3) ? BarEdge::Bottom : BarEdge::Top;
}

inline BarAlign bar_align_from_value(int align) {
  if (align == 0) {
    return BarAlign::Left;
  }
  if (align == 2) {
    return BarAlign::Right;
  }
  return BarAlign::Center;
}

inline float pack_origin(float screen_w, float content_w, BarAlign align) {
  if (content_w <= 0.0f) {
    return kOffscreen;
  }
  if (align == BarAlign::Right) {
    const float x = screen_w - kEdgeInset - content_w;
    return x < kEdgeInset ? kEdgeInset : x;
  }
  if (align == BarAlign::Left) {
    return kEdgeInset;
  }
  return (screen_w - content_w) * 0.5f;
}

struct Layout {
  float screen_w = 0.0f;
  float screen_h = 0.0f;
  float bar_x = 0.0f;
  float bar_y = 0.0f;
  float bar_w = 0.0f;
  float bar_h = kFontH + kBarExtra;
  float label_margin_top = kTextTopInset;
  float overlay_cpu_x = 160.0f;
  float overlay_cpu_y = 12.0f;
  float overlay_gpu_x = 360.0f;
  float overlay_gpu_y = 12.0f;
  float overlay_ram_x = 560.0f;
  float overlay_ram_y = 12.0f;
  float overlay_ip_x = 740.0f;
  float overlay_ip_y = 12.0f;
  float overlay_fan_x = 960.0f;
  float overlay_fan_y = 12.0f;
  float overlay_fps_x = 40.0f;
  float overlay_fps_y = 12.0f;
};

struct Metrics {
  bool enabled = true;
  bool show_fps = true;
  bool show_cpu = true;
  bool show_gpu = true;
  bool show_ram = true;
  bool show_ip = false;
  bool show_fan = false;
  bool per_core_cpu = false;
};

inline bool valid_screen_dimension(float value) {
  return std::isfinite(value) && value > 1.0f;
}

inline Layout compute_overlay_layout(float screen_w, float screen_h,
                                     BarEdge edge, BarAlign align,
                                     const Metrics &metrics) {
  Layout out{};
  out.bar_h = kFontH + kBarExtra;
  out.label_margin_top = kTextTopInset;
  out.overlay_cpu_x = kOffscreen;
  out.overlay_gpu_x = kOffscreen;
  out.overlay_ram_x = kOffscreen;
  out.overlay_ip_x = kOffscreen;
  out.overlay_fan_x = kOffscreen;
  out.overlay_fps_x = kOffscreen;

  if (!valid_screen_dimension(screen_w) || !valid_screen_dimension(screen_h)) {
    return out;
  }

  out.screen_w = screen_w;
  out.screen_h = screen_h;
  out.bar_x = 0.0f;
  out.bar_w = screen_w;
  out.bar_y = (edge == BarEdge::Bottom) ? (screen_h - out.bar_h) : 0.0f;

  const bool show_cpu =
      metrics.enabled && (metrics.show_cpu || metrics.per_core_cpu);
  const bool show_gpu = metrics.enabled && metrics.show_gpu;
  const bool show_ram = metrics.enabled && metrics.show_ram;
  const bool show_ip = metrics.enabled && metrics.show_ip;
  const bool show_fan = metrics.enabled && metrics.show_fan;
  const bool show_fps = metrics.enabled && metrics.show_fps;
  const float cpu_w = metrics.per_core_cpu ? kCpuAllWidth : kCpuAvgWidth;

  struct Slot {
    bool on;
    float width;
    float *x;
    float *y;
  };
  Slot slots[] = {
      {show_fps, kFpsWidth, &out.overlay_fps_x, &out.overlay_fps_y},
      {show_cpu, cpu_w, &out.overlay_cpu_x, &out.overlay_cpu_y},
      {show_gpu, kGpuWidth, &out.overlay_gpu_x, &out.overlay_gpu_y},
      {show_ram, kRamWidth, &out.overlay_ram_x, &out.overlay_ram_y},
      {show_ip, kIpWidth, &out.overlay_ip_x, &out.overlay_ip_y},
      {show_fan, kFanWidth, &out.overlay_fan_x, &out.overlay_fan_y},
  };

  float content_w = 0.0f;
  int visible = 0;
  for (const Slot &slot : slots) {
    if (!slot.on) {
      continue;
    }
    if (visible++) {
      content_w += kGap;
    }
    content_w += slot.width;
  }

  float x = pack_origin(screen_w, content_w, align);

  for (Slot &slot : slots) {
    *slot.y = out.bar_y;
    if (!slot.on || content_w <= 0.0f) {
      *slot.x = kOffscreen;
      continue;
    }
    *slot.x = x;
    x += slot.width + kGap;
  }
  return out;
}

} // namespace onion::overlay
