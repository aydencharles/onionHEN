/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Reusable progress auto-return and dwell timing controller.
 * Handles post-completion dwell duration, stack transition guards,
 * and page pop execution for modal progress views.
 */

#pragma once

#include "toolbox_route.hpp"

#include <cstdint>
#include <memory>

namespace toolbox {

/**
 * High-level progress outcome.
 */
enum class ProgressOutcome {
  Ongoing,
  Success,
  Failed
};

/**
 * Abstract page stack navigation service.
 */
class IPageStackNavigator {
public:
  virtual ~IPageStackNavigator() = default;

  /** Return true if the UI page stack is animating a transition. */
  virtual bool is_under_transition() const = 0;

  /** Pop the topmost page from the stack. Returns true if successful. */
  virtual bool pop() = 0;

  /** Return current active toolbox page. */
  virtual Page current_active_page() const = 0;

  /** Monotonic time in milliseconds. */
  virtual uint64_t now_ms() const;
};

/**
 * Production page stack navigator backed by ShellUI's UIManager.
 */
class DefaultPageStackNavigator : public IPageStackNavigator {
public:
  bool is_under_transition() const override;
  bool pop() override;
  Page current_active_page() const override;
};

/**
 * Configuration options for progress auto-return timing and page affinity.
 */
struct AutoReturnConfig {
  Page target_page = Page::None;
  uint64_t success_dwell_ms = 800;
  uint64_t error_dwell_ms = 2000;
};

/**
 * Progress auto-return controller.
 */
class ProgressAutoReturn {
public:
  explicit ProgressAutoReturn(
      AutoReturnConfig config = {},
      std::shared_ptr<IPageStackNavigator> navigator = nullptr);

  /** Reset dwell timing and trigger flags for a new session. */
  void reset();

  /**
   * Evaluate progress outcome and execute pop when dwell duration has elapsed.
   * If @p now_monotonic_ms is 0, navigator_->now_ms() is used.
   * @return true if pop was executed on this invocation; false otherwise.
   */
  bool poll(ProgressOutcome outcome, uint64_t now_monotonic_ms = 0);

  bool has_triggered() const { return triggered_; }
  uint64_t completed_time_ms() const { return completed_time_ms_; }
  const AutoReturnConfig &config() const { return config_; }

private:
  AutoReturnConfig config_;
  std::shared_ptr<IPageStackNavigator> navigator_;
  uint64_t completed_time_ms_ = 0;
  bool triggered_ = false;
};

} // namespace toolbox
