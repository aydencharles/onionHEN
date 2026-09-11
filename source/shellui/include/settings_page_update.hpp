#pragma once

#include "ps5_settings_ui.hpp"

namespace onion::shellui::settings {

// The reconciler owns ordering; the view owns firmware-specific operations.
class PageView {
public:
  virtual ~PageView() = default;
  virtual bool set_title(const std::string &title) = 0;
  virtual bool remove(const std::string &id) = 0;
  virtual bool insert(const ps5ui::Node &node, const std::string &before) = 0;
  virtual bool update(const ps5ui::Node &previous, const ps5ui::Node &next) = 0;
};

bool valid_live_model(const ps5ui::Node &root);
// Advances the snapshot after each successful operation, allowing retries.
bool reconcile(PageView &view, ps5ui::Node &current, const ps5ui::Node &next);

} // namespace onion::shellui::settings
