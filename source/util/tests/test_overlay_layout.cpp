#include "test_harness.h"

#include "overlay_layout.hpp"

namespace {

using onion::overlay::BarAlign;
using onion::overlay::BarEdge;
using onion::overlay::compute_overlay_layout;
using onion::overlay::kCpuAllWidth;
using onion::overlay::kCpuAvgWidth;
using onion::overlay::kEdgeInset;
using onion::overlay::kFpsWidth;
using onion::overlay::kGap;
using onion::overlay::kGpuWidth;
using onion::overlay::kIpWidth;
using onion::overlay::kOffscreen;
using onion::overlay::kRamWidth;
using onion::overlay::Layout;
using onion::overlay::Metrics;

constexpr float kW = 1920.0f;
constexpr float kH = 1080.0f;

int as_int(float value) { return static_cast<int>(value); }

Metrics fps_only() {
  Metrics m{};
  m.enabled = true;
  m.show_fps = true;
  m.show_cpu = false;
  m.show_gpu = false;
  m.show_ram = false;
  m.show_ip = false;
  m.per_core_cpu = false;
  return m;
}

Metrics all_average() {
  Metrics m{};
  m.enabled = true;
  m.show_fps = true;
  m.show_cpu = true;
  m.show_gpu = true;
  m.show_ram = true;
  m.show_ip = true;
  m.per_core_cpu = false;
  return m;
}

int test_invalid_screen_hides_metrics() {
  const Layout layout =
      compute_overlay_layout(0.0f, 0.0f, BarEdge::Top, BarAlign::Center,
                             all_average());
  TEST_ASSERT_EQ_INT(as_int(kOffscreen), as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(as_int(kOffscreen), as_int(layout.overlay_cpu_x));
  TEST_ASSERT_EQ_INT(0, as_int(layout.screen_w));
  return 0;
}

int test_disabled_bar_hides_metrics() {
  Metrics m = all_average();
  m.enabled = false;
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Left, m);
  TEST_ASSERT_EQ_INT(as_int(kOffscreen), as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(as_int(kOffscreen), as_int(layout.overlay_ip_x));
  TEST_ASSERT_EQ_INT(0, as_int(layout.bar_y));
  TEST_ASSERT_EQ_INT(as_int(kW), as_int(layout.bar_w));
  return 0;
}

int test_top_left_packs_from_inset() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Left, fps_only());
  TEST_ASSERT_EQ_INT(as_int(kEdgeInset), as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(0, as_int(layout.bar_y));
  TEST_ASSERT_EQ_INT(as_int(kOffscreen), as_int(layout.overlay_cpu_x));
  return 0;
}

int test_top_center_packs_mid_screen() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Center,
                             fps_only());
  TEST_ASSERT_EQ_INT(as_int((kW - kFpsWidth) * 0.5f),
                     as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(0, as_int(layout.bar_y));
  TEST_ASSERT_TRUE(layout.overlay_fps_x > kEdgeInset);
  return 0;
}

int test_top_right_packs_to_right_inset() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Right,
                             fps_only());
  TEST_ASSERT_EQ_INT(as_int(kW - kEdgeInset - kFpsWidth),
                     as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(0, as_int(layout.bar_y));
  return 0;
}

int test_bottom_left_uses_bottom_edge() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Bottom, BarAlign::Left,
                             fps_only());
  TEST_ASSERT_EQ_INT(as_int(kEdgeInset), as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(as_int(kH - layout.bar_h), as_int(layout.bar_y));
  TEST_ASSERT_EQ_INT(as_int(layout.bar_y), as_int(layout.overlay_fps_y));
  return 0;
}

int test_bottom_center_packs_mid_screen() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Bottom, BarAlign::Center,
                             fps_only());
  TEST_ASSERT_EQ_INT(as_int((kW - kFpsWidth) * 0.5f),
                     as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(as_int(kH - layout.bar_h), as_int(layout.bar_y));
  return 0;
}

int test_bottom_right_packs_right_and_bottom() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Bottom, BarAlign::Right,
                             fps_only());
  TEST_ASSERT_EQ_INT(as_int(kW - kEdgeInset - kFpsWidth),
                     as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(as_int(kH - layout.bar_h), as_int(layout.bar_y));
  return 0;
}

int test_visible_order_is_fps_cpu_gpu_ram_ip() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Left,
                             all_average());
  TEST_ASSERT_EQ_INT(as_int(kEdgeInset), as_int(layout.overlay_fps_x));
  TEST_ASSERT_EQ_INT(as_int(kEdgeInset + kFpsWidth + kGap),
                     as_int(layout.overlay_cpu_x));
  TEST_ASSERT_EQ_INT(
      as_int(kEdgeInset + kFpsWidth + kGap + kCpuAvgWidth + kGap),
      as_int(layout.overlay_gpu_x));
  TEST_ASSERT_TRUE(layout.overlay_ram_x > layout.overlay_gpu_x);
  TEST_ASSERT_TRUE(layout.overlay_ip_x > layout.overlay_ram_x);
  return 0;
}

int test_right_pack_keeps_left_to_right_order() {
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Right,
                             all_average());
  const float content_w = kFpsWidth + kGap + kCpuAvgWidth + kGap + kGpuWidth +
                          kGap + kRamWidth + kGap + kIpWidth;
  const float start = kW - kEdgeInset - content_w;
  TEST_ASSERT_EQ_INT(as_int(start), as_int(layout.overlay_fps_x));
  TEST_ASSERT_TRUE(layout.overlay_fps_x < layout.overlay_cpu_x);
  TEST_ASSERT_TRUE(layout.overlay_cpu_x < layout.overlay_gpu_x);
  TEST_ASSERT_TRUE(layout.overlay_ip_x + kIpWidth <= kW - kEdgeInset + 0.1f);
  return 0;
}

int test_per_core_cpu_uses_wider_slot() {
  Metrics m = fps_only();
  m.show_cpu = true;
  m.per_core_cpu = true;
  const Layout avg =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Left, fps_only());
  Metrics cpu_avg = fps_only();
  cpu_avg.show_cpu = true;
  const Layout with_avg =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Left, cpu_avg);
  const Layout with_all = compute_overlay_layout(kW, kH, BarEdge::Top,
                                                 BarAlign::Left, m);
  TEST_ASSERT_EQ_INT(as_int(kOffscreen), as_int(avg.overlay_cpu_x));
  TEST_ASSERT_EQ_INT(as_int(kEdgeInset + kFpsWidth + kGap + kCpuAvgWidth),
                     as_int(with_avg.overlay_cpu_x + kCpuAvgWidth));
  TEST_ASSERT_EQ_INT(as_int(kEdgeInset + kFpsWidth + kGap + kCpuAllWidth),
                     as_int(with_all.overlay_cpu_x + kCpuAllWidth));
  TEST_ASSERT_TRUE(with_all.overlay_cpu_x + kCpuAllWidth >
                   with_avg.overlay_cpu_x + kCpuAvgWidth);
  return 0;
}

int test_hidden_metric_is_offscreen_between_neighbors() {
  Metrics m = all_average();
  m.show_gpu = false;
  const Layout layout =
      compute_overlay_layout(kW, kH, BarEdge::Top, BarAlign::Left, m);
  TEST_ASSERT_EQ_INT(as_int(kOffscreen), as_int(layout.overlay_gpu_x));
  TEST_ASSERT_EQ_INT(
      as_int(layout.overlay_cpu_x + kCpuAvgWidth + kGap),
      as_int(layout.overlay_ram_x));
  return 0;
}

int test_pack_origin_left_center_right() {
  constexpr float content = 200.0f;
  TEST_ASSERT_EQ_INT(as_int(kEdgeInset),
                     as_int(onion::overlay::pack_origin(kW, content,
                                                        BarAlign::Left)));
  TEST_ASSERT_EQ_INT(
      as_int((kW - content) * 0.5f),
      as_int(onion::overlay::pack_origin(kW, content, BarAlign::Center)));
  TEST_ASSERT_EQ_INT(as_int(kW - kEdgeInset - content),
                     as_int(onion::overlay::pack_origin(kW, content,
                                                        BarAlign::Right)));
  TEST_ASSERT_EQ_INT(
      as_int(kOffscreen),
      as_int(onion::overlay::pack_origin(kW, 0.0f, BarAlign::Left)));
  return 0;
}

int test_edge_and_align_mapping() {
  TEST_ASSERT_TRUE(onion::overlay::bar_edge_from_pos(0) == BarEdge::Top);
  TEST_ASSERT_TRUE(onion::overlay::bar_edge_from_pos(1) == BarEdge::Top);
  TEST_ASSERT_TRUE(onion::overlay::bar_edge_from_pos(2) == BarEdge::Bottom);
  TEST_ASSERT_TRUE(onion::overlay::bar_edge_from_pos(3) == BarEdge::Bottom);
  TEST_ASSERT_TRUE(onion::overlay::bar_align_from_value(0) == BarAlign::Left);
  TEST_ASSERT_TRUE(onion::overlay::bar_align_from_value(1) == BarAlign::Center);
  TEST_ASSERT_TRUE(onion::overlay::bar_align_from_value(2) == BarAlign::Right);
  TEST_ASSERT_TRUE(onion::overlay::bar_align_from_value(99) ==
                   BarAlign::Center);
  return 0;
}

} // namespace

extern "C" int test_overlay_layout_suite(void) {
  int failures = 0;
  failures += onion_test_run("overlay_layout.invalid_screen",
                             test_invalid_screen_hides_metrics);
  failures += onion_test_run("overlay_layout.disabled",
                             test_disabled_bar_hides_metrics);
  failures += onion_test_run("overlay_layout.top_left",
                             test_top_left_packs_from_inset);
  failures += onion_test_run("overlay_layout.top_center",
                             test_top_center_packs_mid_screen);
  failures += onion_test_run("overlay_layout.top_right",
                             test_top_right_packs_to_right_inset);
  failures += onion_test_run("overlay_layout.bottom_left",
                             test_bottom_left_uses_bottom_edge);
  failures += onion_test_run("overlay_layout.bottom_center",
                             test_bottom_center_packs_mid_screen);
  failures += onion_test_run("overlay_layout.bottom_right",
                             test_bottom_right_packs_right_and_bottom);
  failures += onion_test_run("overlay_layout.order",
                             test_visible_order_is_fps_cpu_gpu_ram_ip);
  failures += onion_test_run("overlay_layout.right_keeps_ltr",
                             test_right_pack_keeps_left_to_right_order);
  failures += onion_test_run("overlay_layout.per_core_width",
                             test_per_core_cpu_uses_wider_slot);
  failures += onion_test_run("overlay_layout.hidden_gap",
                             test_hidden_metric_is_offscreen_between_neighbors);
  failures += onion_test_run("overlay_layout.pack_origin",
                             test_pack_origin_left_center_right);
  failures += onion_test_run("overlay_layout.edge_align_mapping",
                             test_edge_and_align_mapping);
  return failures;
}
