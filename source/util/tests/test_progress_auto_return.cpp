/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "test_harness.h"
#include "progress_auto_return.hpp"

#include <memory>

namespace {

class MockPageStackNavigator : public toolbox::IPageStackNavigator {
public:
  bool under_transition = false;
  bool pop_success = true;
  int pop_calls = 0;
  toolbox::Page active_page = toolbox::Page::PluginProgress;
  uint64_t simulated_time_ms = 1000;

  bool is_under_transition() const override { return under_transition; }
  bool pop() override {
    ++pop_calls;
    return pop_success;
  }
  toolbox::Page current_active_page() const override { return active_page; }
  uint64_t now_ms() const override { return simulated_time_ms; }
};

} // namespace

static int test_ongoing_does_not_trigger(void) {
  auto mock_nav = std::make_shared<MockPageStackNavigator>();
  toolbox::ProgressAutoReturn auto_return(
      {toolbox::Page::PluginProgress, 800, 2000}, mock_nav);

  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Ongoing));
  TEST_ASSERT_TRUE(auto_return.completed_time_ms() == 0);
  TEST_ASSERT_TRUE(!auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 0);
  return 0;
}

static int test_success_dwell_timing(void) {
  auto mock_nav = std::make_shared<MockPageStackNavigator>();
  mock_nav->simulated_time_ms = 1000;
  toolbox::ProgressAutoReturn auto_return(
      {toolbox::Page::PluginProgress, 800, 2000}, mock_nav);

  // First success frame records timestamp
  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(auto_return.completed_time_ms() == 1000);
  TEST_ASSERT_TRUE(!auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 0);

  // Advance by 799ms (total elapsed 799ms < 800ms)
  mock_nav->simulated_time_ms = 1799;
  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(!auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 0);

  // Advance to 1800ms (elapsed 800ms >= 800ms) -> triggers pop
  mock_nav->simulated_time_ms = 1800;
  TEST_ASSERT_TRUE(auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 1);

  // Subsequent polls do not trigger again
  mock_nav->simulated_time_ms = 2500;
  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 1);
  return 0;
}

static int test_error_dwell_timing(void) {
  auto mock_nav = std::make_shared<MockPageStackNavigator>();
  mock_nav->simulated_time_ms = 2000;
  toolbox::ProgressAutoReturn auto_return(
      {toolbox::Page::PluginProgress, 800, 2000}, mock_nav);

  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Failed));
  TEST_ASSERT_TRUE(auto_return.completed_time_ms() == 2000);

  // Advance by 1500ms (less than error dwell limit 2000ms)
  mock_nav->simulated_time_ms = 3500;
  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Failed));
  TEST_ASSERT_TRUE(!auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 0);

  // Advance to 4000ms (elapsed 2000ms) -> triggers pop
  mock_nav->simulated_time_ms = 4000;
  TEST_ASSERT_TRUE(auto_return.poll(toolbox::ProgressOutcome::Failed));
  TEST_ASSERT_TRUE(auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 1);
  return 0;
}

static int test_transition_deferral(void) {
  auto mock_nav = std::make_shared<MockPageStackNavigator>();
  mock_nav->simulated_time_ms = 1000;
  mock_nav->under_transition = true;
  toolbox::ProgressAutoReturn auto_return(
      {toolbox::Page::PluginProgress, 800, 2000}, mock_nav);

  auto_return.poll(toolbox::ProgressOutcome::Success);
  mock_nav->simulated_time_ms = 2000;

  // Elapsed >= 800ms, but stack is under transition -> pop deferred
  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(!auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 0);

  // Transition ends on next tick -> pop succeeds
  mock_nav->under_transition = false;
  TEST_ASSERT_TRUE(auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 1);
  return 0;
}

static int test_wrong_active_page_aborts(void) {
  auto mock_nav = std::make_shared<MockPageStackNavigator>();
  mock_nav->simulated_time_ms = 1000;
  mock_nav->active_page = toolbox::Page::PluginProgress;
  toolbox::ProgressAutoReturn auto_return(
      {toolbox::Page::PluginProgress, 800, 2000}, mock_nav);

  auto_return.poll(toolbox::ProgressOutcome::Success);

  // User manually backed out to a different page before timer expired
  mock_nav->simulated_time_ms = 2000;
  mock_nav->active_page = toolbox::Page::Plugins;

  TEST_ASSERT_TRUE(!auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 0);
  return 0;
}

static int test_reset_and_cheat_progress(void) {
  auto mock_nav = std::make_shared<MockPageStackNavigator>();
  mock_nav->simulated_time_ms = 500;
  mock_nav->active_page = toolbox::Page::CheatProgress;

  toolbox::ProgressAutoReturn auto_return(
      {toolbox::Page::CheatProgress, 800, 2000}, mock_nav);

  auto_return.poll(toolbox::ProgressOutcome::Success);
  mock_nav->simulated_time_ms = 1500;
  TEST_ASSERT_TRUE(auto_return.poll(toolbox::ProgressOutcome::Success));
  TEST_ASSERT_TRUE(auto_return.has_triggered());
  TEST_ASSERT_TRUE(mock_nav->pop_calls == 1);

  // Reset for next session
  auto_return.reset();
  TEST_ASSERT_TRUE(!auto_return.has_triggered());
  TEST_ASSERT_TRUE(auto_return.completed_time_ms() == 0);
  return 0;
}

extern "C" int test_progress_auto_return_suite(void) {
  int fails = 0;
  fails += onion_test_run("progress_auto_return.ongoing",
                          test_ongoing_does_not_trigger);
  fails += onion_test_run("progress_auto_return.success_dwell",
                          test_success_dwell_timing);
  fails += onion_test_run("progress_auto_return.error_dwell",
                          test_error_dwell_timing);
  fails += onion_test_run("progress_auto_return.transition_deferral",
                          test_transition_deferral);
  fails += onion_test_run("progress_auto_return.page_affinity",
                          test_wrong_active_page_aborts);
  fails += onion_test_run("progress_auto_return.reset_and_cheat",
                          test_reset_and_cheat_progress);
  return fails;
}
