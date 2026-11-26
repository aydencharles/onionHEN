/* Copyright (C) 2025 OnionHEN / LightningMods
 * Extracted from hook_functions.cpp — hook_capture
 */
#include "hooked_funcs.hpp"
#include "debug_settings_route_runtime.hpp"
#include "detour.h"
#include "ipc.hpp"
#include <msg.hpp>
#include <pthread.h>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <cstring>
#include <algorithm>


bool CaptureScreen();

bool CaptureScreen(){
  if(g_settings.cheats_shortcut_opt == CHEATS_LONG_SHARE){
    //LOG_DEBUG("CaptureScreen: Long Share Shortcut activated");
    GoToURI("OnionHEN?Cheats");
    return true;
  }
  else if (g_settings.toolbox_shortcut_opt == TOOLBOX_LONG_SHARE){
    //LOG_DEBUG("CaptureScreen: Long Share Shortcut activated");
    GoToURI(shellui_debug_settings_toolbox_uri());
    return true;
  }

  return false;
}
void CaptureScreen_old(MonoObject *inst, int userId, long deviceId, int capType, MonoObject* capInfo){
  if (!shellui_hooks_are_ready()) {
    if (CaptureScreen_orig_old)
      CaptureScreen_orig_old(inst, userId, deviceId, capType, capInfo);
    return;
  }

  LOG_DEBUG("CaptureScreen: userId: %d, deviceId: %ld, capType: %d", userId, deviceId, capType);

  if(CaptureScreen()){
    LOG_DEBUG("CaptureScreen: Shortcut activated, redirecting");
    return;
  }
  CaptureScreen_orig_old(inst, userId, deviceId, capType, capInfo);

}

void CaptureScreen_new(MonoObject * inst, int userId, long deviceId, int capType, MonoString* format, MonoObject* capInfo) {
  if (!shellui_hooks_are_ready()) {
    if (CaptureScreen_orig_new)
      CaptureScreen_orig_new(inst, userId, deviceId, capType, format, capInfo);
    return;
  }

  LOG_DEBUG("CaptureScreen_new: userId: %d, deviceId: %ld, capType: %d", userId, deviceId, capType);
  if(CaptureScreen()){
    LOG_DEBUG("CaptureScreen_new: Shortcut activated, redirecting");
    return;
  }
  CaptureScreen_orig_new(inst, userId, deviceId, capType, format, capInfo);
}

void OnShareButton(MonoObject * data) {
  if (!shellui_hooks_are_ready()) {
    if (OnShareButton_orig)
      OnShareButton_orig(data);
    return;
  }

  LOG_DEBUG("OnShareButton: data: %p", static_cast<void *>(data));

  if( g_settings.cheats_shortcut_opt == CHEATS_SINGLE_SHARE) {
    // LOG_DEBUG("Share Shortcut: Redirecting to Cheats");
    GoToURI("OnionHEN?Cheats");
    return;
  }
  else if (g_settings.toolbox_shortcut_opt == TOOLBOX_SINGLE_SHARE) {
    // LOG_DEBUG("Share Shortcut: Redirecting to Toolbox");
    GoToURI(shellui_debug_settings_toolbox_uri());
    return;
  }

  OnShareButton_orig(data);
}
