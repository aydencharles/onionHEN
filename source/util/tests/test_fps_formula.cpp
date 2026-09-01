/* Host unit tests for skip-hook FPS composition (no PS5 SDK). */
#include "test_harness.h"

#include <onion/fps_formula.hpp>
#include <onion/fps_sample.h>

static int test_hz_from_delta(void) {
  TEST_ASSERT_TRUE(onion::fps::hz_from_delta(0, 0.033) == 0.0);
  TEST_ASSERT_TRUE(onion::fps::hz_from_delta(2, 0.0) == 0.0);
  const double hz = onion::fps::hz_from_delta(2, 1.0 / 30.0);
  TEST_ASSERT_TRUE(hz > 59.0 && hz < 61.0);
  TEST_ASSERT_TRUE(onion::fps::hz_from_delta(1, 0.001) > 999.0); /* raw uncapped rate */
  return 0;
}

static int test_rolling_mean(void) {
  const float s[] = {60.f, 60.f, 60.f, 60.f, 60.f};
  TEST_ASSERT_TRUE(onion::fps::rolling_mean(s, 5) > 59.9f);
  TEST_ASSERT_TRUE(onion::fps::rolling_mean(nullptr, 5) == 0.f);
  TEST_ASSERT_TRUE(onion::fps::rolling_mean(s, 0) == 0.f);
  return 0;
}

static int test_compose_hybrid_and_multipass(void) {
  onion::fps::HybridIn in;
  in.scanout_ok = true;
  in.scanout = 60.f;
  in.ring_ok = true;
  in.ring = 60.f;
  auto out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(out.valid);
  TEST_ASSERT_TRUE(out.fps >= 59.f && out.fps <= 61.f);

  in.ring = 120.f;
  out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(out.valid);
  TEST_ASSERT_TRUE(out.fps >= 119.f); /* uncap hybrid max */

  in.ring_ok = false;
  in.global_ok = true;
  in.global = 180.f; /* 3x scanout → multi-pass */
  in.calibration_ready = true;
  in.multipass = true;
  out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(out.valid);
  TEST_ASSERT_TRUE(out.fps >= 59.f && out.fps <= 61.f);
  TEST_ASSERT_TRUE((out.source & ONION_FPS_SRC_MULTIPASS) != 0);
  return 0;
}

static int test_compose_dead_ticks(void) {
  onion::fps::HybridIn in;
  in.dead_ticks = onion::fps::kDeadTicks - 1;
  auto out = onion::fps::compose(in);
  TEST_ASSERT_TRUE(!out.valid);
  TEST_ASSERT_EQ_INT(onion::fps::kDeadTicks, out.dead_ticks);
  return 0;
}

static int test_title_prefixes(void) {
  TEST_ASSERT_TRUE(onion::fps::is_ps5_native_title("PPSA12345"));
  TEST_ASSERT_TRUE(onion::fps::is_ps5_native_title("PPSB00001"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps5_native_title("ECAS00006"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps5_native_title("CUSA12345"));
  TEST_ASSERT_TRUE(onion::fps::is_ps4_bc_title("CUSA12345"));
  TEST_ASSERT_TRUE(onion::fps::is_ps4_bc_title("PCAS00001"));
  TEST_ASSERT_TRUE(onion::fps::is_ps4_bc_title("CUHJ10000"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps4_bc_title("PLJM80005"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps4_bc_title("PPSA12345"));
  TEST_ASSERT_TRUE(!onion::fps::is_ps5_native_title(""));
  return 0;
}

static int test_sample_size(void) {
  TEST_ASSERT_EQ_INT(ONION_FPS_SAMPLE_BYTES,
                     static_cast<int>(sizeof(OnionFpsSample)));
  return 0;
}

extern "C" int test_fps_formula_suite(void) {
  int failures = 0;
  failures += onion_test_run("fps_hz_from_delta", test_hz_from_delta);
  failures += onion_test_run("fps_rolling_mean", test_rolling_mean);
  failures += onion_test_run("fps_compose_hybrid_multipass",
                             test_compose_hybrid_and_multipass);
  failures += onion_test_run("fps_compose_dead_ticks", test_compose_dead_ticks);
  failures += onion_test_run("fps_title_prefixes", test_title_prefixes);
  failures += onion_test_run("fps_sample_size", test_sample_size);
  return failures;
}
