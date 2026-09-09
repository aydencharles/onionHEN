#include "test_harness.h"

#include "overlay_text_metrics.hpp"

namespace {

int test_spaces_are_narrower_than_digits() {
  const float spaced = onion::overlay::estimate_text_width("1 1 1 1 1 1 1 1");
  const float digits = onion::overlay::estimate_text_width("111111111111111");
  TEST_ASSERT_TRUE(spaced < digits);
  return 0;
}

int test_short_labels_keep_minimum_width() {
  TEST_ASSERT_EQ_INT(
      32, static_cast<int>(onion::overlay::estimate_text_width("|")));
  return 0;
}

int test_per_core_padding_does_not_expand_slot() {
  const char *usage = " 1%  2%  3%  4%  5%  6%  7%  8%";
  TEST_ASSERT_EQ_INT(
      289, static_cast<int>(onion::overlay::estimate_text_width(usage)));
  return 0;
}

int test_font_size_scales_width() {
  const float medium = onion::overlay::estimate_text_width("FPS");
  const float large = onion::overlay::estimate_text_width("FPS", 24.0f);
  TEST_ASSERT_TRUE(large > medium);
  TEST_ASSERT_EQ_INT(static_cast<int>(medium * 24.0f / 18.0f),
                     static_cast<int>(large));
  return 0;
}

} // namespace

extern "C" int test_overlay_text_metrics_suite(void) {
  int failures = 0;
  failures += onion_test_run("overlay_text_metrics.space_width",
                             test_spaces_are_narrower_than_digits);
  failures += onion_test_run("overlay_text_metrics.minimum_width",
                             test_short_labels_keep_minimum_width);
  failures += onion_test_run("overlay_text_metrics.per_core_padding",
                             test_per_core_padding_does_not_expand_slot);
  failures += onion_test_run("overlay_text_metrics.font_scale",
                             test_font_size_scales_width);
  return failures;
}
