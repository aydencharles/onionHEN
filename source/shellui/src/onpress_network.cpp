/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress network domain */
#include "onpress.hpp"
#include <cstdlib>

static OnPressResult id_disp_titleids(OnPressContext &ctx) {
  bool &dis_tids = g_settings.display_tids;
  if (atol(ctx.value.c_str()) == dis_tids) {
    LOG_WARN("Display TIDs already %s", dis_tids ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  dis_tids = !dis_tids;
  ReloadRNPSApp("NPXS40002");
  return OnPressResult::Handled;
}

static OnPressResult id_ftp_server(OnPressContext &ctx) {
  const bool enabled = atol(ctx.value.c_str()) != 0;
  if (enabled == g_settings.ftp_server)
    return OnPressResult::EarlyReturn;

  g_settings.ftp_server = enabled;
  ctx.reload_util = true;
  return OnPressResult::Handled;
}

static const OnPressExactEntry kExact[] = {
    {"id_disp_titleids", id_disp_titleids},
    {"id_ftp_server", id_ftp_server},
};

const OnPressExactEntry *onpress_network_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
