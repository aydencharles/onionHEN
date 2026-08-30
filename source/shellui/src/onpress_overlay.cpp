/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress overlay domain */
#include "onpress.hpp"
#include <cstdlib>
#include <unistd.h>

void RemoveGameWidget(RemoveWidget widget);
bool CreateGameWidget(CreateWidget widget);

/** Tear down all segments, recompute horizontal packing, rebuild enabled ones. */
static void rebuild_overlay_bar() {
  RemoveGameWidget(REMOVE_ALL_OVERLAYS);
  apply_overlay_layout();
  if (!g_settings.overlay_enabled)
    return;
  if (g_settings.overlay_fps)
    CreateGameWidget(CREATE_FPS_OVERLAY);
  if (g_settings.overlay_cpu || g_settings.all_cpu_usage)
    CreateGameWidget(CREATE_CPU_OVERLAY);
  if (g_settings.overlay_gpu)
    CreateGameWidget(CREATE_GPU_OVERLAY);
  if (g_settings.overlay_ram)
    CreateGameWidget(CREATE_RAM_OVERLAY);
  if (g_settings.overlay_ip)
    CreateGameWidget(CREATE_IP_OVERLAY);
}

static OnPressResult toggle_overlay_flag(OnPressContext &ctx, bool &flag) {
  if (atoi(ctx.value.c_str()) == flag) {
    return OnPressResult::EarlyReturn;
  }
  flag = !flag;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_enabled(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_enabled);
}

static OnPressResult id_overlay_background(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_background);
}

static OnPressResult id_overlay_gpu(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_gpu);
}

static OnPressResult id_overlay_cpu(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.overlay_cpu) {
    return OnPressResult::EarlyReturn;
  }
  if (!atoi(ctx.value.c_str()) && g_settings.all_cpu_usage) {
    notify("notify.overlay.disable_cpu_first");
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_cpu = !g_settings.overlay_cpu;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_fps(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_fps);
}

static OnPressResult id_overlay_ram(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_ram);
}

static OnPressResult id_overlay_ip(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_ip);
}

static OnPressResult id_all_cpu_usage(OnPressContext &ctx) {
  if (g_settings.all_cpu_usage == atoi(ctx.value.c_str())) {
    return OnPressResult::EarlyReturn;
  }
  if (!g_settings.overlay_cpu) {
    notify("notify.overlay.enable_cpu_first");
    return OnPressResult::EarlyReturn;
  }
  /* Persisted via settings_commit as overlay.cpu_usage_mode in config.ini. */
  g_settings.all_cpu_usage = !g_settings.all_cpu_usage;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_change_pos(OnPressContext &ctx) {
  if ((overlay_positions)atoi(ctx.value.c_str()) == g_settings.overlay_pos) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_pos = atoi(ctx.value.c_str());
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_align(OnPressContext &ctx) {
  const int align = atoi(ctx.value.c_str());
  if (align == g_settings.overlay_align) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_align = align;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static const OnPressExactEntry kExact[] = {
    {"id_overlay_enabled", id_overlay_enabled},
    {"id_overlay_background", id_overlay_background},
    {"id_overlay_gpu", id_overlay_gpu},
    {"id_overlay_cpu", id_overlay_cpu},
    {"id_overlay_fps", id_overlay_fps},
    {"id_overlay_ram", id_overlay_ram},
    {"id_overlay_ip", id_overlay_ip},
    {"id_all_cpu_usage", id_all_cpu_usage},
    {"id_overlay_change_pos", id_overlay_change_pos},
    {"id_overlay_align", id_overlay_align},
};

const OnPressExactEntry *onpress_overlay_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
