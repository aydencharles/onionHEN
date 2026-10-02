/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include "toolbox_i18n.hpp"
#include <onion/platform.h>
#include <onion/system_tmp.h>
#include <string>
#include <fstream>
#include <sys/stat.h>

#include "shellui_state.hpp"
#include <cstring>

void save_appid(int value, const char* filename) {
    mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
    std::ofstream file(filename);
    file << value;
}
bool app_launched = false;
int LaunchApp(MonoString* titleId, uint64_t* args, int argsSize, LaunchAppParam *param){
  if (!shellui_hooks_are_ready())
    return LaunchApp_orig ? LaunchApp_orig(titleId, args, argsSize, param) : -1;

#if 1
   if(!if_exists(ONION_SYSTEM_TMP_PATCH_PLUGIN)) {
      LOG_DEBUG("patch payload not running .. returning with orig");
	  unsigned int ret = LaunchApp_orig(titleId, args, argsSize, param);
      if (ret < 0) {
         notify("notify.app.launch_failed", ret);
         return ret;
      }

      app_launched = true;
      return ret;

   }
#endif
  LOG_DEBUG("LaunchApp called with titleId: %s, argsSize: %d, param->size: %d", mono_string_to_utf8(titleId), argsSize, param->size);
  notify("notify.app.launching", mono_string_to_utf8(titleId));

  unsigned int ret = LaunchApp_orig(titleId, args, argsSize, param);
  if (ret < 0) {
    notify("notify.app.launch_failed", ret);
    return ret;
  }

  app_launched = true;

  notify("notify.app.launch_returned", ret);

  save_appid(ret, ONION_SYSTEM_TMP_APP_LAUNCHED);
  return ret;

}

int sceRegMgrGetInt_hook(long regid, int* out_val){
  if (!shellui_hooks_are_ready()) {
    int original_ret = 0;
    if (!__sys_regmgr_call ||
        __sys_regmgr_call(2, regid, &original_ret, out_val,
                          SCE_REGMGR_INT_SIZE))
      return SCE_REGMGR_ERROR_PRM_REGID;
    return original_ret;
  }

  bool dis_tids = g_settings.display_tids;

  if(dis_tids && regid == SCE_REGMGR_ENT_KEY_DEVENV_TOOL_SHELLUI_disp_titleid){
    if (out_val) {
       *out_val = 1;
    }
    LOG_DEBUG("RegMGR lookup called for SHELLUI_disp_titleid, spoofing out_var to 1");
    return 0;
  }

  int ret = 0;
  if(__sys_regmgr_call(2, regid, &ret, out_val, SCE_REGMGR_INT_SIZE)){
    LOG_ERROR("sceRegMgrGetInt_hook: Failed to get regid 0x%lx, ret %d", regid, ret);
    ret = SCE_REGMGR_ERROR_PRM_REGID;
  }

  return ret;
}
static std::string extractTIDFromURI(const std::string& url) {
    const std::string prefix = "titleId=";
    size_t pos = url.find(prefix);
    
    if (pos != std::string::npos) {
        pos += prefix.length();
        size_t end = url.find('&', pos);
        if (end == std::string::npos) {
            return url.substr(pos);
        } else {
            return url.substr(pos, end - pos);
        }
    }
    return std::string(); // Not found
}

void createJson_hook(MonoObject* inst, MonoObject* array, MonoString* id, MonoString* label, MonoString* actionUrl, MonoString* actionId, MonoString* messageId, MonoObject* subMenu, bool enable) {

    if (!shellui_hooks_are_ready()) {
        if (createJson)
            createJson(inst, array, id, label, actionUrl, actionId, messageId,
                       subMenu, enable);
        return;
    }

    std::string id_str = Mono_to_String(id);

    LOG_DEBUG("createJson_hook: %p id: %s, label: %s, actionUrl: %s, actionId: %s, messageId: %s", 
               static_cast<void *>(inst), id_str.c_str(), 
               Mono_to_String(label).c_str(), 
               Mono_to_String(actionUrl).c_str(), 
               Mono_to_String(actionId).c_str(), 
               Mono_to_String(messageId).c_str());

    if(!g_settings.onionhen_game_opts) {
        createJson(inst, array, id, label, actionUrl, actionId, messageId, subMenu, enable);
        return;
    }

    // Only extract and update titleId if one is found in the current URL
    std::string extracted_tid = extractTIDFromURI(Mono_to_String(actionUrl));
    if (!extracted_tid.empty() && extracted_tid != g_ui.current_menu_tid) {
        g_ui.current_menu_tid = extracted_tid;
        //notify("Current menu titleId: %s", g_ui.current_menu_tid.c_str());
        LOG_DEBUG("Updated menu titleId: %s", g_ui.current_menu_tid.c_str());
    }
    if(id_str == "MENU_ID_CHECK_PATCH"){  
      //createJson_hook: 8815fec90 id: MENU_ID_CHECK_PATCH, label: , actionUrl: pspatchcheck:check-for-update?titleid=CUSA01127, actionId: , messageId: msgid_check_update
        createJson(inst, array,
                   mono_string_new(Root_Domain, "MENU_ID_CHEATS"),
                   mono_string_new(Root_Domain,
                                   toolbox_i18n::tr("cheats.game_menu")),
                   mono_string_new(Root_Domain, "OnionHEN?Cheats_not_open"),
                   actionId, nullptr, subMenu, enable);
        return;
    }

    createJson(inst, array, id, label, actionUrl, actionId, messageId, subMenu, enable);
}
