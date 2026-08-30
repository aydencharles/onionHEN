/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "hooked_funcs.hpp"
#include "homeui_top_nav_patch.hpp"
#include "settings_bundle_patch.hpp"
#include "toolbox_i18n.hpp"
#include <onion/platform.h>
#include "detour.h"
#include "ipc.hpp"
#include <climits>
#include <msg.hpp>
#include <pthread.h>
#include <sys/_pthreadtypes.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
extern "C"{
#include <ps5/kernel.h>
}

extern bool is_6xx, is_3xx;
/* ================================= ORIG HOOKED MONO FUNCS ============================================= */
int (*oOnPress)(MonoObject* Instance, MonoObject* element, MonoObject* e) = nullptr;
int (*oOnPreCreate)(MonoObject* Instance, MonoObject* element) = nullptr;
MonoString* (*CxmlUri)(MonoObject* obj, MonoString* uri) = nullptr;
uint64_t(*GetManifestResourceStream_Original)(uint64_t inst, MonoString* FileName) = nullptr;
void (*DebugSettings_GetModel_Orig)(MonoObject* instance, MonoObject* param, MonoObject* promise) = nullptr;
void (*ReactNavigatorManager_UpdateNavigationState_Orig)(MonoObject* instance, MonoObject* state) = nullptr;
GamePadData (*GetData)(int deviceIndex) = nullptr;

bool (*boot_orig)(MonoString* uri, int opt, MonoString* titleIdForBootAction) = nullptr;
void (*OnShareButton_orig)(MonoObject* data) = nullptr;
bool (*boot_orig_2)(MonoString* uri, int opt) = nullptr;

void (*CaptureScreen_orig_old)(MonoObject * inst, int userId, long deviceId, int capType, MonoObject* capacityInfo) = nullptr;
void (*CaptureScreen_orig_new)(MonoObject * inst, int userId, long deviceId, int capType, MonoString* format, MonoObject* capacityInfo) = nullptr;
void (*createJson)(MonoObject*, MonoObject* array, MonoString* id, MonoString* label, MonoString* actionUrl, MonoString* actionId, MonoString* messageId, MonoObject* subMenu, bool enable) = nullptr;

int (*__sys_regmgr_call)(long, long, int*, int*, long) = nullptr;

MonoString *(*oGetString)(MonoObject *Instance, MonoString *str) = nullptr;
int (*LaunchApp_orig)(MonoString* titleId, uint64_t* args, int argsSize, LaunchAppParam *param) = nullptr;

/* ================================= HOOKED GLOBAL VARS ============================================= */
MonoClass* MemoryStream_IO = nullptr;

// UI runtime state: g_ui (shellui_state.hpp / shellui_globals.cpp)

// widgets → shellui_overlay_widgets.cpp
extern "C"{
int sceShellCoreUtilIsUsbMassStorageMounted(int num);
int sceNetSend(int sockfd, const void *buf, size_t len, int flags);
}

/** Create MonoString on the *calling* domain (UI thread may not be Root_Domain). */
static MonoString *mono_str_ui(const char *utf8) {
  if (!utf8 || !mono_string_new)
    return nullptr;
  MonoDomain *dom =
      (mono_domain_get ? mono_domain_get() : nullptr);
  if (!dom)
    dom = Root_Domain;
  if (!dom)
    return nullptr;
  return mono_string_new(dom, utf8);
}

MonoString *GetString_Hook(MonoObject *Instance, MonoString *str) {
    if (!shellui_hooks_are_ready())
      return oGetString ? oGetString(Instance, str) : str;

    if (!str || !Instance) {
#if SHELL_DEBUG == 1
      LOG_ERROR("GetString_Hook: Invalid Parameters");
#endif
      /* Prefer original; never invent a string on a broken call. */
      if (oGetString)
        return oGetString(Instance, str);
      return str;
    }
    std::string resourceName = Mono_to_String(str);
#if SHELL_DEBUG == 1
    LOG_DEBUG("Resource Name: %s", resourceName.c_str());
#endif
    if (resourceName == "msg_options") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.options"));
    } else if (resourceName == "msg_installing") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.installing"));
    } else if (resourceName == "msg_yes") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.yes"));
    } else if (resourceName == "msg_no") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.no"));
    } else if (resourceName == "msg_sort") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.sort"));
    } else if (resourceName == "msg_sort_name_az") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.sort_az"));
    } else if (resourceName == "msg_sort_name_za") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.sort_za"));
    } else if (resourceName == "msg_updated") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.updated"));
    } else if (resourceName == "msg_wait") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.wait"));
    }
    else if (resourceName == "msg_ok"){
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.ok"));
    }
    else if (resourceName == "msg_cancel_vb"){
        return mono_str_ui(toolbox_i18n::tr("pkg.msg.cancel"));
    }
    //else if (resourceName == "msg_deselect_all") {
   //   return mono_str_ui("取消全选"); // IDK WHY BUT ONLY 1 CAN BE ACTIVE OR SHELLUI CRASHES
  //  }
    else if (resourceName == "msg_select_all") {
      return mono_str_ui(toolbox_i18n::tr("pkg.msg.select_all"));
    }

    // XML title/description literals (e.g. "★OnionHEN 工具箱") are already valid
    // MonoStrings. Re-allocating with mono_string_new(Root_Domain, ...) on the UI
    // thread has crashed ShellUI (wrong domain / GC). Pass the original through.
    if (resourceName.rfind("msg_", 0) != 0) {
#if SHELL_DEBUG == 1
      LOG_DEBUG("GetString_Hook: literal XML string, passthrough");
#endif
      return str;
    }

    if (!oGetString) {
#if SHELL_DEBUG == 1
      LOG_DEBUG("GetString_Hook: oGetString is null");
#endif
      return str;
    }
    return oGetString(Instance, str);
  }
  
int ioctl_hook(int fd, unsigned long request, void *argp) {
  const int IOCTL_SYSCALL = 0x36;
  const unsigned long  DECRYPT_RNPS_BUNDLE = 0xC0105203; // RNPS request code for ioctl

  int ret = __syscall(IOCTL_SYSCALL, fd, request, argp);
  if (shellui_hooks_are_ready() && ret == 0 && request == DECRYPT_RNPS_BUNDLE) {
      ioctl_C0105203_args *args = (ioctl_C0105203_args *)argp;
#if SHELL_DEBUG == 1
      LOG_DEBUG("ioctl_hook called with fd: %d, request: 0x%lX, argp: %p", fd, request, argp);
#endif
      if (!args || !args->buffer || args->size <= 0) {
#if SHELL_DEBUG == 1
          LOG_ERROR("homeui_top_nav_patch: ioctl RNPS args invalid");
#endif
          return ret;
      }
#if SHELL_DEBUG == 1
      const unsigned char *p = (const unsigned char *)args->buffer;
      const unsigned char b0 = args->size > 0 ? p[0] : 0;
      const unsigned char b1 = args->size > 1 ? p[1] : 0;
      const unsigned char b2 = args->size > 2 ? p[2] : 0;
      const unsigned char b3 = args->size > 3 ? p[3] : 0;
      LOG_DEBUG("homeui_top_nav_patch: ioctl RNPS buffer=%p size=%d "
                  "head=%02x %02x %02x %02x",
                  args->buffer, args->size, b0, b1, b2, b3);
#endif
      unsigned char *buffer = static_cast<unsigned char *>(args->buffer);
      patch_settings_bundle(buffer, args->size);
      patch_homeui_top_nav(buffer, &args->size, args->size);
  }
  return ret;
}

void ParseCheatID(const char* id, char* tid, int* cheat_id)
{
    sscanf(id, "id_cheat_%[^_]_%d", tid, cheat_id);
}

// threads → hook_background.cpp
MonoString * CxmlUri_Hook(MonoObject * Instance, MonoString * uri) {

  if (!shellui_hooks_are_ready())
    return CxmlUri ? CxmlUri(Instance, uri) : uri;

  if (!Instance || !uri) {
    #if SHELL_DEBUG==1 
    LOG_DEBUG("CxmlUri_Hook: args are null");
    #endif
    return CxmlUri(Instance, uri);
  }
  std::string uri_string = Mono_to_String(uri);
  #if SHELL_DEBUG==1 
  LOG_DEBUG("uri_string: %s", uri_string.c_str());
  #endif
  ///LOG_DEBUG("CxmlUri_Hook: %s", uri_string.c_str());
  /*
   * NPXS40008 registers its Debug Settings icon as icon_setting.png.  Do not
   * rewrite that asset on disk (or mutate the RNPS string table): the stock
   * file must remain visible whenever OnionHEN is not loaded.  CxmlUri is the
   * SettingsPlugin asset resolver, so redirect only this request while our
   * hooks are ready.
   */
  if (uri_string.find("icon_setting") != std::string::npos) {
#if SHELL_DEBUG == 1
    LOG_DEBUG("CxmlUri_Hook: intercepted Settings icon -> "
              "/system_ex/vsh_asset/onionhen.png");
#endif
    return mono_str_ui("/system_ex/vsh_asset/onionhen.png");
  }
  if (uri_string.rfind("tex_game_icon") != std::string::npos) {
    //LOG_DEBUG("CxmlUri_Hook: Returning store icon");
    std::string icon = "/user/appmeta/" + g_ui.running_tid + "/icon0.png";
    if(!if_exists(icon.c_str())){
        icon = "/user/appmeta/external/" + g_ui.running_tid + "/icon0.png";

        if(!if_exists(icon.c_str())){ // pirated PS5 Games
           std::string game_src = "/system_ex/app/" + g_ui.running_tid + "/sce_sys/icon0.png"; // shellui cant access this path
           icon = "/user/appmeta/" + g_ui.running_tid;
           mkdir(icon.c_str(), 0777);
           icon = "/user/appmeta/" + g_ui.running_tid + "/icon0.png";
           IPC_Client::getInstance(false).CopyFile(game_src, icon);
        }
    }
   // LOG_DEBUG("CxmlUri_Hook: %s", icon.c_str());
    return mono_str_ui(icon.c_str());
  }
  else if (uri_string.rfind("//usb") != std::string::npos || uri_string.rfind("//data") != std::string::npos || uri_string.rfind("//user//data") != std::string::npos){
    //replace // with//
    std::string new_uri = uri_string;
    size_t pos = 0;
    while (( pos = new_uri.find("//", pos)) != std::string::npos) {
        new_uri.replace(pos, 2, "/");
    }
    #if SHELL_DEBUG==1 
    LOG_DEBUG("CxmlUri_Hook: %s", new_uri.c_str());
    #endif
    return mono_str_ui(new_uri.c_str());
  }
  return CxmlUri(Instance, uri);
}
MonoObject* MemoryStream_Instance = nullptr;


// navigator → hook_navigator.cpp
// manifest → hook_manifest.cpp
extern "C" int sceKernelGetPs4SystemSwVersion(OrbisKernelSwVersion *);

MonoMethod* set_value_method = nullptr;
void CheckRunningOnMainThread() {
	//notify("Main thread check called!");
}
void Patch_Main_thread_Check(MonoImage * image_core) {

    uint64_t real_addr = Get_Address_of_Method(image_core, "Sce.PlayStation.Core.Runtime", "Diagnostics", "CheckRunningOnMainThread", 0);
    if (!real_addr) {
#if SHELL_DEBUG==1
        LOG_ERROR("Failed to get method address");
#endif
        return;
    }
#if SHELL_DEBUG==1
    LOG_DEBUG("changing permissions on (0x%llx).", (unsigned long long)real_addr);
#endif
    
    if (!DetourFunction(real_addr, (void*)&CheckRunningOnMainThread)) {
        LOG_ERROR("Main thread check detour failed");
        return;
    }
#if SHELL_DEBUG==1
    LOG_DEBUG("Main thread check patched");
#endif

}
// Common logic function

// launch → hook_launch.cpp
