/* Copyright (C) 2025 OnionHEN / LightningMods
 * ShellUI OnPress_Hook — table-driven toolbox press dispatch.
 */
#include "onpress.hpp"
#include "onpress_policy.hpp"
#include "hooked_funcs.hpp"
#include "shellui_state.hpp"
#include "dynamic_ui_runtime.hpp"

namespace {

int call_original(MonoObject *instance, MonoObject *element, MonoObject *event) {
  return oOnPress ? oOnPress(instance, element, event) : 0;
}

static OnPressResult try_exact(const OnPressExactEntry *table, size_t n,
                               OnPressContext &ctx) {
  for (size_t i = 0; i < n; ++i) {
    if (ctx.id == table[i].id) {
      return table[i].handler(ctx);
    }
  }
  return OnPressResult::NotMine;
}

static OnPressResult try_prefix(const OnPressPrefixEntry *table, size_t n,
                                OnPressContext &ctx) {
  for (size_t i = 0; i < n; ++i) {
    if (ctx.id.rfind(table[i].prefix, 0) == 0) {
      OnPressResult r = table[i].handler(ctx);
      if (r != OnPressResult::NotMine) {
        return r;
      }
    }
  }
  return OnPressResult::NotMine;
}

OnPressResult dispatch_toolbox_press(toolbox::OnPressDomain domain,
                                     OnPressContext &ctx) {
  OnPressResult result = OnPressResult::NotMine;
  size_t count = 0;

  auto run_prefix = [&](const OnPressPrefixEntry *(*provider)(size_t *)) {
    if (result == OnPressResult::NotMine) {
      const OnPressPrefixEntry *table = provider(&count);
      result = try_prefix(table, count, ctx);
    }
  };
  auto run_exact = [&](const OnPressExactEntry *(*provider)(size_t *)) {
    if (result == OnPressResult::NotMine) {
      const OnPressExactEntry *table = provider(&count);
      result = try_exact(table, count, ctx);
    }
  };

  switch (domain) {
  case toolbox::OnPressDomain::Root:
    run_exact(onpress_overlay_exact);
    run_exact(onpress_network_exact);
    run_exact(onpress_system_exact);
    run_exact(onpress_misc_root_exact);
    break;
  case toolbox::OnPressDomain::Payloads:
  case toolbox::OnPressDomain::AutoPayloads:
    run_prefix(onpress_payloads_prefix);
    break;
  case toolbox::OnPressDomain::Plugins:
  case toolbox::OnPressDomain::PluginConfig:
  case toolbox::OnPressDomain::PkgNet:
    run_prefix(onpress_plugins_prefix);
    run_exact(onpress_plugins_exact);
    break;
  case toolbox::OnPressDomain::Cheats:
    run_prefix(onpress_cheats_prefix);
    break;
  case toolbox::OnPressDomain::Account:
    run_exact(onpress_account_exact);
    break;
  case toolbox::OnPressDomain::Plapps:
    run_prefix(onpress_packages_prefix);
    break;
  case toolbox::OnPressDomain::Progress:
    break;
  case toolbox::OnPressDomain::RemotePlay:
    run_exact(onpress_network_exact);
    break;
  case toolbox::OnPressDomain::PassThrough:
  case toolbox::OnPressDomain::DynamicPlugin:
    break;
  }

  return result;
}

} // namespace

int OnPress_Hook(MonoObject *Instance, MonoObject *element, MonoObject *e) {
  if (!shellui_hooks_are_ready())
    return call_original(Instance, element, e);

  if (!Instance || !element) {
#if SHELL_DEBUG == 1
    LOG_DEBUG("[LM HOOK] OnPress_Hook: args are null");
#endif
    return call_original(Instance, element, e);
  }

  const toolbox::OnPressDomain domain =
      toolbox::onpress_domain_for_page(g_ui.active_page);
  if (domain == toolbox::OnPressDomain::PassThrough)
    return call_original(Instance, element, e);

  OnPressContext ctx;
  ctx.instance = Instance;
  ctx.element = element;
  ctx.event = e;
  ctx.id = GetPropertyValue(element, "Id");
  ctx.value = GetPropertyValue(element, "Value");
  ctx.title = GetPropertyValue(element, "Title");

  if (domain == toolbox::OnPressDomain::DynamicPlugin) {
    const auto dynamic_result = onion::shellui::dynamic_ui::dispatch_control(
        ctx.id, ctx.value);
    if (dynamic_result !=
        onion::shellui::dynamic_ui::DispatchResult::NotOwned) {
      return 0;
    }
    return call_original(Instance, element, e);
  }

#if SHELL_DEBUG == 1
  LOG_DEBUG("[LM HOOK] OnPress_Hook: page=%u Id=%s Value=%s",
              static_cast<unsigned>(g_ui.active_page), ctx.id.c_str(),
              ctx.value.c_str());
#endif

  const OnPressResult result = dispatch_toolbox_press(domain, ctx);

  if (result == OnPressResult::Consumed) {
    // Fully owned by OnionHEN (dynamic XML). Skip stock SettingPage.OnPressed.
    if (ctx.dirty) {
      settings_commit(ctx.reload_main, ctx.reload_util);
    }
    return 0;
  }
  if (result == OnPressResult::Handled && ctx.dirty) {
    settings_commit(ctx.reload_main, ctx.reload_util);
  }

  // Handled augments stock value/navigation behavior. EarlyReturn and NotMine
  // are side-effect-free pass-throughs.
  return call_original(Instance, element, e);
}
