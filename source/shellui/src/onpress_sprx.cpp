#include "onpress.hpp"

#include "external_sprx_ui.hpp"
#include "toolbox_i18n.hpp"

namespace {

OnPressResult external_sprx_control(OnPressContext &ctx) {
  ctx.dirty = false;
  const onion::shellui::external_sprx::DispatchResult result =
      onion::shellui::external_sprx::dispatch(ctx.id, ctx.value);
  if (!result.owned) return OnPressResult::NotMine;

  if (result.action == onion::shellui::external_sprx::Action::EnabledChanged &&
      result.success)
    return OnPressResult::Consumed;

  const char *key = result.success ? "sprx.deleted_fmt"
                                   : "sprx.operation_failed_fmt";
  const std::string message = toolbox_i18n::format(key, result.id.c_str());
  notify("%s", message.c_str());
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
