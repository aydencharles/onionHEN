#include "onpress.hpp"

#include "external_sprx_ui.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_i18n.hpp"

namespace {

OnPressResult external_sprx_control(OnPressContext &ctx) {
  ctx.dirty = false;
  const onion::shellui::external_sprx::DispatchResult result =
      onion::shellui::external_sprx::dispatch(ctx.id, ctx.value);
  if (!result.owned)
    return OnPressResult::NotMine;

  onion::shellui::settings::refresh(ctx.instance);

  using onion::shellui::external_sprx::Action;
  if (!result.success) {
    const std::string target = result.id.empty() ? std::string("catalog")
                                                 : result.id;
    const std::string message =
        result.error.empty()
            ? toolbox_i18n::format("sprx.operation_failed_fmt", target.c_str())
            : toolbox_i18n::format("sprx.operation_failed_reason_fmt",
                                   target.c_str(), result.error.c_str());
    notify("%s", message.c_str());
    return OnPressResult::Consumed;
  }

  switch (result.action) {
  case Action::EnabledChanged:
    if (result.was_loaded && !result.toggle_value) {
      const std::string message = toolbox_i18n::format(
          "sprx.disabled_loaded_fmt", result.id.c_str());
      notify("%s", message.c_str());
    }
    return OnPressResult::Consumed;
  case Action::AutoStartChanged:
    return OnPressResult::Consumed;
  case Action::LoadNow: {
    const std::string message =
        toolbox_i18n::format("sprx.loaded_fmt", result.id.c_str());
    notify("%s", message.c_str());
    return OnPressResult::Consumed;
  }
  case Action::Reloaded: {
    const std::string message = toolbox_i18n::tr("sprx.reloaded");
    notify("%s", message.c_str());
    return OnPressResult::Consumed;
  }
  case Action::BoundCurrent:
  case Action::BoundAll: {
    const std::string message =
        toolbox_i18n::format("sprx.bound_fmt", result.id.c_str());
    notify("%s", message.c_str());
    return OnPressResult::Consumed;
  }
  case Action::Deleted: {
    const std::string message =
        toolbox_i18n::format("sprx.deleted_fmt", result.id.c_str());
    notify("%s", message.c_str());
    return OnPressResult::Consumed;
  }
  case Action::None:
    return OnPressResult::Consumed;
  }
  return OnPressResult::Consumed;
}

const OnPressPrefixEntry kSprxPrefix[] = {
    {"id_external_sprx_", external_sprx_control},
};

} // namespace

const OnPressPrefixEntry *onpress_sprx_prefix(size_t *count) {
  *count = sizeof(kSprxPrefix) / sizeof(kSprxPrefix[0]);
  return kSprxPrefix;
}
