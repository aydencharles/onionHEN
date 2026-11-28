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

#include "detour.h"
#include "debug_settings_route_runtime.hpp"
#include "dynamic_ui_runtime.hpp"
#include "hooked_funcs.hpp"
#include "control_center_sdk.hpp"
#include "defs.h"
#include "external_symbols.hpp"
#include "homeui_top_nav_patch.hpp"
#include "ipc.hpp"
#include "onion_icon_source_hook.hpp"
#include "onion_web_server.hpp"
#include "plugin_ui_bridge_client.hpp"
#include "proc.h"
#include "ps5/kernel.h"
#include "startup_guard.hpp"
#include "toolbox_i18n.hpp"

#include <onion/log.h>
#include <onion/notify.h>
#include <onion/proc_query.h>
#include <onion/ready.h>

#include <cstring>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// Globals (referenced by other shellui translation units)
// ---------------------------------------------------------------------------

std::string UI3_dec;
std::string legacy_dec;
std::string appsystem_dll;
std::string uilib;
std::string Sysinfo;
std::string display_info;
std::string uilib_dll;
std::string payloads_xml;
std::string debug_settings_xml;
std::string cheats_xml;

MonoImage* pui_img = nullptr;
MonoImage* AppSystem_img = nullptr;
MonoObject* Game = nullptr;
MonoImage* react_common_img = nullptr;

bool hooked = false;
bool has_hv_bypass = false;
bool is_6xx = false;
bool is_3xx = false;

extern "C" long ptr_syscall = 0;

void __syscall() {
  asm(".intel_syntax noprefix\n"
      "  mov rax, rdi\n"
      "  mov rdi, rsi\n"
      "  mov rsi, rdx\n"
      "  mov rdx, rcx\n"
      "  mov r10, r8\n"
      "  mov r8,  r9\n"
      "  mov r9,  qword ptr [rsp + 8]\n"
      "  call qword ptr [rip + ptr_syscall]\n"
      "  ret\n");
}

void (*OnRender_orig)(MonoObject* instance);
void (*oUserCustomElementReset)(MonoObject* instance, MonoObject* item) = nullptr;
void (*oSettingPageStackOnPopping)(MonoObject* instance, MonoObject* outgoing,
                                   MonoObject* incoming) = nullptr;
MonoObject* rootWidget = nullptr;
MonoObject* font = nullptr;
void (*Orig_ReloadApp)(MonoString* str) = nullptr;

// Defined in prx_install / prx_overlay
void ReloadApp(MonoString* str);
void OnRender_Hook(MonoObject* instance);
int KillAppWithReason_Hook(int appId, int reason);

// ---------------------------------------------------------------------------
// Init constants
// ---------------------------------------------------------------------------

namespace {

constexpr int kSyscallInstrOffset = 0x0A;
constexpr size_t kMprotectProbeSize = 100;
constexpr int kProtRwx = 0x7; // PROT_READ | PROT_WRITE | PROT_EXEC
constexpr uint32_t kFw3xxMaxExclusive = 0x4000042;
constexpr uint32_t kFw6xxMin = 0x6000000;
// Same magnitude as the previous sleep(0x100000) keep-alive.
constexpr unsigned kKeepAliveSleepSec = 0x100000;
constexpr uint64_t kMainThreadHandoffTimeoutMs = 2000;
constexpr uint64_t kReloadWaitCapMs = 5000;
constexpr useconds_t kStartupWaitPollUs = 20 * 1000;

OrbisKernelSwVersion g_boot_sw{};

// XOR key material used for version string + toolbox XML (base64 of "SISTR0_I_SEE_YOU").
constexpr const char* kXorKeyB64 = "U0lTVFIwX0lfU0VFX1lPVQ==";

// ---------------------------------------------------------------------------
// Detour helpers
// ---------------------------------------------------------------------------

template <typename Fn>
bool install_detour(const char* name, uint64_t target, void* hook, Fn& out_orig,
                    bool required) {
  if (!target) {
    if (required) {
      LOG_ERROR("Hook target missing: %s", name);
      notify("notify.hook.target");
    } else {
      LOG_WARN("Optional hook target unavailable: %s", name);
    }
    return !required;
  }

  if (!InstallDetour(target, hook, reinterpret_cast<void**>(&out_orig),
                     required)) {
    if (required) {
      LOG_ERROR("Detour failed: %s", name);
      notify("notify.hook.install");
    } else {
      LOG_WARN("Optional hook detour failed: %s", name);
    }
    return !required;
  }

  return true;
}

template <typename Fn>
bool install_detour_native(const char* name, void* target, void* hook, Fn& out_orig,
                           bool required) {
  return install_detour(name, reinterpret_cast<uint64_t>(target), hook, out_orig,
                        required);
}

// ---------------------------------------------------------------------------
// Mono images used during init
// ---------------------------------------------------------------------------

struct ShellImages {
  MonoImage* legacy = nullptr;
  MonoImage* mscorlib = nullptr;
  MonoImage* react_pui = nullptr;
  MonoImage* app_system = nullptr;
  MonoImage* core = nullptr;
  MonoImage* capture_menu = nullptr;
  MonoImage* lnc = nullptr; // optional
  MonoImage* react_common = nullptr;
  MonoImage* rn_shell = nullptr;
  MonoImage* pui = nullptr;
};

MonoImage* require_dll(const char* name) {
  MonoImage* img = getDLLimage(name);
  if (!img) {
    LOG_ERROR("Failed to load Mono assembly: %s", name);
    notify("notify.hook.load_assembly");
  }
  return img;
}

// ---------------------------------------------------------------------------
// Phase 1: native dynlib symbols
// ---------------------------------------------------------------------------

bool resolve_native_symbols(pid_t pid) {
  int libkernelsys = get_module_handle(pid, "libkernel_sys.sprx");
  KERNEL_DLSYM(libkernelsys, sceKernelDebugOutText);
  KERNEL_DLSYM(libkernelsys, sceKernelMkdir);
  KERNEL_DLSYM(libkernelsys, scePthreadCreate);
  KERNEL_DLSYM(libkernelsys, sceKernelMprotect);
  KERNEL_DLSYM(libkernelsys, sceKernelSendNotificationRequest);
  KERNEL_DLSYM(libkernelsys, sceKernelGetProsperoSystemSwVersion);
  KERNEL_DLSYM(libkernelsys, sceKernelGetAppInfo);
  KERNEL_DLSYM(libkernelsys, sceKernelGetProcessName);
  KERNEL_DLSYM(libkernelsys, sceKernelGetCurrentFanDuty);

  /* libonion_platform must not CALL sceKernelSendNotificationRequest by name —
   * here that symbol is a dlsym'd *function pointer*. Register a trampoline. */
  onion_notify_set_send(+[](int32_t device, void *req, size_t size,
                            int32_t blocking) -> int32_t {
    if (!sceKernelSendNotificationRequest)
      return -1;
    return sceKernelSendNotificationRequest(
        device, static_cast<OrbisNotificationRequest *>(req),
        static_cast<int>(size), blocking);
  });

  KERNEL_DLSYM(libSceKernelHandle, sceKernelJitCreateSharedMemory);
  KERNEL_DLSYM(libSceKernelHandle, sceKernelJitCreateAliasOfSharedMemory);
  KERNEL_DLSYM(libSceKernelHandle, sceKernelJitMapSharedMemory);
  KERNEL_DLSYM(libSceKernelHandle, ioctl);
  KERNEL_DLSYM(libSceKernelHandle, __sys_regmgr_call);

  // Point ptr_syscall at the syscall instruction inside getpid.
  // Variable name must be `getpid` for KERNEL_DLSYM's #sym string.
  {
    static __attribute__((used)) long getpid = 0;
    KERNEL_DLSYM(libSceKernelHandle, getpid);
    ptr_syscall = getpid + kSyscallInstrOffset;
  }

  int shellui_util = get_module_handle(pid, "libSceShellUIUtil.sprx");
  KERNEL_DLSYM(shellui_util, sceShellUIUtilLaunchByUri);
  KERNEL_DLSYM(shellui_util, sceShellUIUtilInitialize);

  int system_service = get_module_handle(pid, "libSceSystemService.sprx");
  KERNEL_DLSYM(system_service, sceSystemServiceGetAppIdOfRunningBigApp);
  KERNEL_DLSYM(system_service, sceSystemServiceParamGetInt);
  KERNEL_DLSYM(system_service, sceSystemServiceGetAppTitleId);
  {
    void* sceSystemServiceLaunchApp = nullptr;
    KERNEL_DLSYM(system_service, sceSystemServiceLaunchApp);
    if (!sceSystemServiceLaunchApp)
      LOG_ERROR("Failed to resolve sceSystemServiceLaunchApp");
  }

  int reg = get_module_handle(pid, "libSceRegMgr.sprx");
  KERNEL_DLSYM(reg, sceRegMgrGetInt);

  int remoteplay = get_module_handle(pid, "libSceRemoteplay.sprx");
  LOG_DEBUG("Remote Play module handle: %d", remoteplay);
  KERNEL_DLSYM(remoteplay, sceRemoteplayInitialize);
  KERNEL_DLSYM(remoteplay, sceRemoteplayGeneratePinCode);
  KERNEL_DLSYM(remoteplay, sceRemoteplayConfirmDeviceRegist);
  KERNEL_DLSYM(remoteplay, sceRemoteplayNotifyPinCodeError);
  LOG_DEBUG("Remote Play symbols: init=%p pin=%p confirm=%p invalidate=%p",
            reinterpret_cast<void *>(sceRemoteplayInitialize),
            reinterpret_cast<void *>(sceRemoteplayGeneratePinCode),
            reinterpret_cast<void *>(sceRemoteplayConfirmDeviceRegist),
            reinterpret_cast<void *>(sceRemoteplayNotifyPinCodeError));

  return true;
}

// ---------------------------------------------------------------------------
// Phase 2: mono runtime symbols
// ---------------------------------------------------------------------------

bool resolve_mono_symbols(pid_t pid) {
  int libmono = get_module_handle(pid, "libmonosgen-2.0.sprx");

  KERNEL_DLSYM(libmono, mono_object_to_string);
  KERNEL_DLSYM(libmono, mono_get_root_domain);
  KERNEL_DLSYM(libmono, mono_property_get_get_method);
  KERNEL_DLSYM(libmono, mono_property_get_set_method);
  KERNEL_DLSYM(libmono, mono_class_get_property_from_name);
  KERNEL_DLSYM(libmono, mono_class_from_name);
  KERNEL_DLSYM(libmono, mono_raise_exception);
  KERNEL_DLSYM(libmono, mono_runtime_invoke);
  KERNEL_DLSYM(libmono, mono_array_new);
  KERNEL_DLSYM(libmono, mono_string_new);
  KERNEL_DLSYM(libmono, mono_jit_set_aot_only);
  KERNEL_DLSYM(libmono, mono_jit_init_version);
  KERNEL_DLSYM(libmono, mono_object_new);
  KERNEL_DLSYM(libmono, mono_object_unbox);
  KERNEL_DLSYM(libmono, mono_set_dirs);
  KERNEL_DLSYM(libmono, mono_compile_method);
  KERNEL_DLSYM(libmono, mono_assembly_get_image);
  KERNEL_DLSYM(libmono, mono_domain_assembly_open);
  KERNEL_DLSYM(libmono, mono_get_byte_class);
  KERNEL_DLSYM(libmono, mono_thread_attach);
  KERNEL_DLSYM(libmono, mono_object_get_class);
  KERNEL_DLSYM(libmono, mono_vtable_get_static_field_data);
  KERNEL_DLSYM(libmono, mono_class_get_method_from_name);
  KERNEL_DLSYM(libmono, mono_class_get_field_from_name);
  KERNEL_DLSYM(libmono, mono_field_get_value);
  KERNEL_DLSYM(libmono, mono_aot_get_method);
  KERNEL_DLSYM(libmono, mono_field_static_set_value);
  KERNEL_DLSYM(libmono, mono_assembly_setrootdir);
  KERNEL_DLSYM(libmono, mono_free);
  KERNEL_DLSYM(libmono, mono_gchandle_new);
  KERNEL_DLSYM(libmono, mono_gchandle_free); /* optional; null if missing */
  KERNEL_DLSYM(libmono, mono_image_open_from_data);
  KERNEL_DLSYM(libmono, mono_runtime_object_init);
  KERNEL_DLSYM(libmono, mono_domain_get);
  KERNEL_DLSYM(libmono, mono_assembly_load_from);
  KERNEL_DLSYM(libmono, mono_method_desc_new);
  KERNEL_DLSYM(libmono, mono_method_desc_search_in_class);
  KERNEL_DLSYM(libmono, mono_method_desc_free);
  KERNEL_DLSYM(libmono, mono_object_new_specific);
  KERNEL_DLSYM(libmono, mono_thread_detach);
  KERNEL_DLSYM(libmono, mono_array_addr_with_size);
  KERNEL_DLSYM(libmono, mono_thread_current);
  KERNEL_DLSYM(libmono, mono_class_vtable);
  KERNEL_DLSYM(libmono, mono_domain_unload);
  KERNEL_DLSYM(libmono, mono_string_to_utf8);

  const bool ok =
      mono_object_to_string && mono_get_root_domain && mono_property_get_get_method &&
      mono_property_get_set_method && mono_class_get_property_from_name &&
      mono_class_from_name && mono_runtime_invoke && mono_array_new && mono_string_new &&
      mono_jit_set_aot_only && mono_jit_init_version && mono_object_new &&
      mono_object_unbox && mono_set_dirs && mono_compile_method &&
      mono_assembly_get_image && mono_domain_assembly_open && mono_get_byte_class &&
      mono_thread_attach && mono_object_get_class && mono_vtable_get_static_field_data &&
      mono_class_get_method_from_name && mono_class_get_field_from_name &&
      mono_aot_get_method && mono_field_static_set_value && mono_assembly_setrootdir &&
      mono_free && mono_gchandle_new && mono_image_open_from_data &&
      mono_runtime_object_init && mono_domain_get && mono_assembly_load_from &&
      mono_method_desc_new && mono_method_desc_search_in_class && mono_method_desc_free &&
      mono_object_new_specific && mono_thread_detach && mono_array_addr_with_size &&
      mono_thread_current && mono_class_vtable && mono_domain_unload &&
      mono_string_to_utf8;

  if (!ok)
    LOG_ERROR("Failed to resolve mono symbols");
  return ok;
}

// ---------------------------------------------------------------------------
// Phase 3: resource / type name strings (kept as base64 at rest)
// ---------------------------------------------------------------------------

void init_resource_names() {
  // Manifest resource paths (used by GetManifestResourceStream hook)
  payloads_xml = base64_decode(
      "U2NlLlZzaC5TaGVsbFVJLkxlZ2FjeS5zcmMuU2NlLlZzaC5T"
      "aGVsbFVJLlNldHRpbmdzLlBsdWdpbnMucGF5bG9hZHMueG1s");
  cheats_xml = base64_decode(
      "U2NlLlZzaC5TaGVsbFVJLkxlZ2FjeS5zcmMuU2NlLlZzaC5Ta"
      "GVsbFVJLlNldHRpbmdzLlBsdWdpbnMuY2hlYXRzLnhtbA==");
  debug_settings_xml = base64_decode(
      "U2NlLlZzaC5TaGVsbFVJLkxlZ2FjeS5zcmMuU2NlLlZzaC5TaGVsbFVJLlNldHRpbmdzLlBs"
      "dWdpbnMuRGVidWdTZXR0aW5ncy5kYXRhLmRlYnVnX3NldHRpbmdzLnhtbA==");

  // Assembly / type names used across hooks
  legacy_dec = base64_decode("U2NlLlZzaC5TaGVsbFVJLkxlZ2FjeS5kbGw="); // Legacy.dll
  UI3_dec = base64_decode("U2NlLlZzaC5TaGVsbFVJLlNldHRpbmdzLkNvcmVVSTM="); // Settings.CoreUI3
  appsystem_dll = base64_decode("U2NlLlZzaC5TaGVsbFVJLkFwcFN5c3RlbS5kbGw=");
  uilib = base64_decode("U2NlLlZzaC5VSUxpYg==");
  Sysinfo = base64_decode("U3lzdGVtU29mdHdhcmVWZXJzaW9uSW5mbw==");
  display_info = base64_decode("c2V0X0Rpc3BsYXlWZXJzaW9u");
  uilib_dll = base64_decode(
      "L3N5c3RlbV9leC9jb21tb25fZXgvbGliL1NjZS5Wc2guVUlMaWIuZGxs");

  LOG_DEBUG("[GMRS-INIT] expected resource names:");
  LOG_DEBUG("[GMRS-INIT]   debug_settings_xml=\"%s\"", debug_settings_xml.c_str());
  LOG_DEBUG("[GMRS-INIT]   payloads_xml=\"%s\"", payloads_xml.c_str());
  LOG_DEBUG("[GMRS-INIT]   cheats_xml=\"%s\"", cheats_xml.c_str());
}

// ---------------------------------------------------------------------------
// Phase 4: version banner
// ---------------------------------------------------------------------------

bool init_version_string(const OrbisKernelSwVersion& sw) {
  /* XOR with base64_decode(kXorKeyB64) == "SISTR0_I_SEE_YOU" (not the b64 text). */
  /* XOR "OnionHEN " with SISTR0_I_SEE_YOU. Regenerate: encryptver.py "OnionHEN " */
  const char enc_ver[] = "\x1c\x27\x3a\x3b\x3c\x78\x1a\x07\x7f"; /* "OnionHEN " */
  const std::string key = base64_decode(kXorKeyB64);
  auto dev_ver_bytes =
      encrypt_decrypt(reinterpret_cast<const unsigned char*>(enc_ver),
                      sizeof(enc_ver) - 1, key);
  std::string dec_ver(dev_ver_bytes.begin(), dev_ver_bytes.end());
  dec_ver += ONIONHEN_VERSION;

  std::string final_ver;
#if PUBLIC_TEST == 1
  final_ver = dec_ver + "-PUBLIC_TEST (" + sw.version_str + " )";
#elif PRE_RELEASE == 1
  final_ver = dec_ver + " PRE_RELEASE (" + sw.version_str + " )";
#else
  final_ver = dec_ver + " (" + sw.version_str + " )";
#endif

  LOG_DEBUG("Decrypted Version: %s", final_ver.c_str());
  if (!SetVersionString(final_ver.c_str())) {
    LOG_ERROR("Failed to set version string");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Phase 5: load Mono assemblies
// ---------------------------------------------------------------------------

bool load_shell_images(ShellImages& out) {
  out.legacy = require_dll(legacy_dec.c_str());
  out.mscorlib = require_dll("mscorlib.dll");
  out.react_pui = require_dll("ReactNative.PUI.dll");
  out.app_system = require_dll(appsystem_dll.c_str());
  out.core = require_dll("Sce.PlayStation.Core.dll");
  out.capture_menu = require_dll("Sce.Vsh.ShellUI.CaptureMenu.dll");

  // Optional: LaunchApp / KillAppWithReason only when present
  out.lnc = getDLLimage("Sce.Vsh.LncUtilWrapper.dll");
  if (!out.lnc)
    notify("notify.hook.lnc_image");

  out.react_common = require_dll("ReactNative.Vsh.Common.dll");
  out.rn_shell = require_dll("Sce.Vsh.ShellUI.ReactNativeShellApp.dll");
  out.pui = require_dll("Sce.PlayStation.PUI.dll");

  // Publish images other modules need
  AppSystem_img = out.app_system;
  react_common_img = out.react_common;
  pui_img = out.pui;

  return out.legacy && out.mscorlib && out.react_pui && out.app_system && out.core &&
         out.capture_menu && out.react_common && out.rn_shell && out.pui;
}

// ---------------------------------------------------------------------------
// Phase 6: resolve Game container scene
// ---------------------------------------------------------------------------

bool resolve_game_container(MonoImage* app_system) {
  MonoClass* layer_manager =
      mono_class_from_name(app_system, "Sce.Vsh.ShellUI.AppSystem", "LayerManager");
  if (!layer_manager) {
    notify("notify.hook.layer_manager");
    return false;
  }

  MonoMethod* find =
      mono_class_get_method_from_name(layer_manager, "FindContainerSceneByPath", 1);
  if (!find) {
    notify("notify.hook.find_scene");
    return false;
  }

  MonoDomain* dom = (mono_domain_get ? mono_domain_get() : nullptr);
  if (!dom)
    dom = Root_Domain;
  MonoString* path_arg = mono_string_new(dom, "Game");
  void* args[1] = {path_arg};
  MonoObject* exception = nullptr;
  Game = mono_runtime_invoke(find, nullptr, args, &exception);

  if (exception) {
    notify("notify.hook.find_scene_exc");
    return false;
  }
  if (!Game) {
    notify("notify.hook.game_scene");
    return false;
  }

  LOG_DEBUG("Game ContainerScene: %p", static_cast<void *>(Game));
  return true;
}

// ---------------------------------------------------------------------------
// Phase 7: install hooks (table-driven mono methods + special cases)
// ---------------------------------------------------------------------------

/** Mono method hook descriptor; `orig` is address of the typed trampoline ptr. */
struct MonoHookSpec {
  const char* name;
  MonoImage* image;
  const char* name_space;
  const char* klass;
  const char* method;
  int argc;
  void* hook;
  void** orig; // &SomeOrigFn
  bool required;
};

bool install_mono_hook(const MonoHookSpec& h, bool expected_missing = false) {
  const uint64_t addr =
      Get_Address_of_Method(h.image, h.name_space, h.klass, h.method, h.argc,
                            false);
  if (!addr) {
    if (h.required) {
      LOG_ERROR("Hook target missing: %s", h.name);
      notify("notify.hook.target");
    } else if (!expected_missing) {
      LOG_WARN("Optional hook target unavailable: %s", h.name);
    }
    return !h.required;
  }
  LOG_DEBUG("Installing mono hook: %s target=%#02lx hook=%p", h.name, addr,
              h.hook);
  if (!InstallDetour(addr, h.hook, h.orig, h.required)) {
    if (h.required) {
      LOG_ERROR("Detour failed: %s", h.name);
      notify("notify.hook.install");
    } else {
      LOG_WARN("Optional hook detour failed: %s", h.name);
    }
    return !h.required;
  }
  return true;
}

bool install_optional_diag(const char* tag, MonoImage* image, const char* ns,
                           const char* klass, const char* method, int argc,
                           void* hook, void** orig) {
  const uint64_t addr = Get_Address_of_Method(image, ns, klass, method, argc,
                                               false);
  if (!addr) {
    LOG_WARN("%s unavailable (non-fatal)", tag);
    return false;
  }
  const bool installed = InstallDetour(addr, hook, orig, false);
  if (!installed) {
    LOG_WARN("%s detour unavailable (non-fatal)", tag);
    return false;
  }
  LOG_DEBUG("%s hooked", tag);
  return true;
}

void probe_hv_bypass() {
  char probe[kMprotectProbeSize];
  has_hv_bypass = (sceKernelMprotect(probe, sizeof(probe), kProtRwx) == 0);
  LOG_DEBUG("has_hv_bypass=%d (mprotect for detours)", has_hv_bypass ? 1 : 0);
}

bool install_early_guard_hooks() {
  /*
   * Arm the input gate (GamePad.GetData) first so controller events are
   * neutralized while remaining hooks and initial RNPS reloads run.
   */
  MonoImage* core_img = getDLLimage("Sce.PlayStation.Core.dll");
  if (core_img) {
    (void)install_mono_hook(
        {"GamePad.GetData", core_img, "Sce.PlayStation.Core.Input", "GamePad",
         "GetData", 1, reinterpret_cast<void*>(&GetData_hook),
         reinterpret_cast<void**>(&GetData), false});
  } else {
    LOG_WARN("Early guard: Sce.PlayStation.Core.dll unavailable; will retry");
  }

  /*
   * Hook PUI.Application.Update early so the UI main thread claims Mono UI
   * initialization and remaining detours on the next frame instead of racing
   * the background inject thread.
   */
  MonoImage* early_pui = getDLLimage("Sce.PlayStation.PUI.dll");
  if (!early_pui) {
    LOG_WARN("Early guard: Sce.PlayStation.PUI.dll unavailable; worker fallback");
    return false;
  }
  pui_img = early_pui;
  const bool render_hooked = install_mono_hook(
      {"PUI.Application.Update", early_pui, "Sce.PlayStation.PUI",
       "Application", "Update", 0, reinterpret_cast<void*>(&OnRender_Hook),
       reinterpret_cast<void**>(&OnRender_orig), false});
  return render_hooked && OnRender_orig != nullptr;
}

bool install_hooks(const ShellImages& img) {
  Patch_Main_thread_Check(img.core);

  // --- Native (required) ---
  if (!sceRegMgrGetInt) {
    notify("notify.hook.regmgr");
    return false;
  }
  if (!install_detour_native("sceRegMgrGetInt",
                             reinterpret_cast<void*>(sceRegMgrGetInt),
                             reinterpret_cast<void*>(&sceRegMgrGetInt_hook),
                             sceRegMgrGetInt, true))
    return false;

  // Retry GamePad.GetData if Core.Input was not ready during the early pass.
  if (!GetData) {
    (void)install_mono_hook(
        {"GamePad.GetData", img.core, "Sce.PlayStation.Core.Input", "GamePad",
         "GetData", 1, reinterpret_cast<void*>(&GetData_hook),
         reinterpret_cast<void**>(&GetData), false});
  }

  // --- Standard mono hooks ---
  // UI3_dec / Settings types use runtime-decoded namespace strings.
  const MonoHookSpec mono_hooks[] = {
      {"OptionMenu.createJson", img.rn_shell, "ReactNative.Modules.ShellUI.HomeUI",
       "OptionMenu", "createJson", 8, reinterpret_cast<void*>(&createJson_hook),
       reinterpret_cast<void**>(&createJson), true},
      {"SettingsPlugin.CxmlUri", img.legacy, UI3_dec.c_str(), "SettingsPlugin",
       "CxmlUri", 1, reinterpret_cast<void*>(&CxmlUri_Hook),
       reinterpret_cast<void**>(&CxmlUri), false},
      {"SettingPage.OnPressed", img.legacy, UI3_dec.c_str(), "SettingPage", "OnPressed",
       2, reinterpret_cast<void*>(&OnPress_Hook), reinterpret_cast<void**>(&oOnPress),
       false},
      {"SettingPage.OnCreating", img.legacy, UI3_dec.c_str(), "SettingPage",
       "OnCreating", 1, reinterpret_cast<void*>(&OnPreCreate_Hook),
       reinterpret_cast<void**>(&oOnPreCreate), false},
      {"SettingPage.OnActivated", img.legacy, UI3_dec.c_str(), "SettingPage",
       "OnActivated", 1, reinterpret_cast<void *>(&SettingPageOnActivated_Hook),
       reinterpret_cast<void **>(&oSettingPageOnActivated), false},
      {"SettingList.Cleanup", img.legacy, UI3_dec.c_str(), "SettingList",
       "Cleanup", 0, reinterpret_cast<void *>(&SettingListCleanup_Hook),
       reinterpret_cast<void **>(&oSettingListCleanup), false},
      {"UserCustomElementUI.Reset", img.legacy, UI3_dec.c_str(),
       "UserCustomElementUI", "Reset", 1,
       reinterpret_cast<void*>(&UserCustomElementReset_Hook),
       reinterpret_cast<void**>(&oUserCustomElementReset), false},
      {"SettingPageStack.OnPopping", img.legacy, UI3_dec.c_str(),
       "SettingPageStack", "OnPopping", 2,
       reinterpret_cast<void *>(&SettingPageStackOnPopping_Hook),
       reinterpret_cast<void **>(&oSettingPageStackOnPopping), false},
      {"SettingsPlugin.GetString", img.legacy, UI3_dec.c_str(), "SettingsPlugin",
       "GetString", 1, reinterpret_cast<void*>(&GetString_Hook),
       reinterpret_cast<void**>(&oGetString), true},
      {"EventManager.OnShareButton", img.capture_menu, "Sce.Vsh.ShellUI.CaptureMenu",
       "EventManager", "OnShareButton", 1, reinterpret_cast<void*>(&OnShareButton),
       reinterpret_cast<void**>(&OnShareButton_orig), false},
  };

  for (const MonoHookSpec& h : mono_hooks) {
    if (!install_mono_hook(h))
      return false;
  }

  // --- Optional diagnostics (log only) ---
  (void)install_optional_diag(
      "[DBG-NAV] UpdateNavigationState", img.react_pui, "ReactNative.Views.UI3.View",
      "ReactNavigatorManager", "UpdateNavigationState", 1,
      reinterpret_cast<void*>(&ReactNavigatorManager_UpdateNavigationState_Hook),
      reinterpret_cast<void**>(&ReactNavigatorManager_UpdateNavigationState_Orig));

  install_onion_icon_source_hook(img.react_pui);

  (void)install_optional_diag(
      "[DBG-GETMODEL] GetModel", img.rn_shell, "ReactNative.Modules.ShellUI.Settings",
      "DebugSettingsModule", "GetModel", 2,
      reinterpret_cast<void*>(&DebugSettings_GetModel_Hook),
      reinterpret_cast<void**>(&DebugSettings_GetModel_Orig));

  // --- LncUtil (optional image) ---
  if (img.lnc) {
    (void)install_mono_hook(
        {"LncUtilWrapper.LaunchApp", img.lnc, "Sce.Vsh.LncUtil", "LncUtilWrapper",
         "LaunchApp", 4, reinterpret_cast<void*>(&LaunchApp),
         reinterpret_cast<void**>(&LaunchApp_orig), false});
    const uint64_t kill_addr = Get_Address_of_Method(
        img.lnc, "Sce.Vsh.LncUtil", "LncUtilWrapper", "KillAppWithReason", 2,
        false);
    if (!kill_addr) {
      LOG_WARN("Optional hook target unavailable: "
               "LncUtilWrapper.KillAppWithReason");
    } else if (!DetourFunction(
                   kill_addr,
                   reinterpret_cast<void*>(&KillAppWithReason_Hook), false)) {
      LOG_WARN("Optional hook detour failed: "
               "LncUtilWrapper.KillAppWithReason");
      notify("notify.hook.kill_app");
    }
  }

  // --- RNPS bundle decrypt path ---
  if (!ioctl) {
    notify("notify.hook.rnps_find");
    return false;
  }
  LOG_DEBUG("Found ioctl at %p", reinterpret_cast<void *>(ioctl));
  if (!DetourFunction(reinterpret_cast<uintptr_t>(ioctl),
                      reinterpret_cast<void*>(&ioctl_hook))) {
    notify("notify.hook.rnps_detour");
    return false;
  }
  LOG_DEBUG("Detoured ioctl to ioctl_hook");

  // --- BootHelper.Boot: 3-arg then 2-arg ---
  if (!install_mono_hook({"BootHelper.Boot(3)", img.app_system, "Sce.Vsh.ShellUI.AppSystem",
                          "BootHelper", "Boot", 3, reinterpret_cast<void*>(&uri_boot_hook),
                          reinterpret_cast<void**>(&boot_orig), false}, true) ||
      !boot_orig) {
    if (!install_mono_hook(
            {"BootHelper.Boot(2)", img.app_system, "Sce.Vsh.ShellUI.AppSystem",
             "BootHelper", "Boot", 2, reinterpret_cast<void*>(&uri_boot_hook_2),
             reinterpret_cast<void**>(&boot_orig_2), false}, true) ||
        !boot_orig_2) {
      LOG_WARN("No supported BootHelper.Boot overload could be hooked "
               "(3 or 2 arguments)");
      notify("notify.hook.boot");
    }
  }

  // --- CaptureScreen: 4-arg then 5-arg ---
  if (!install_mono_hook(
          {"CaptureScreen(4)", img.capture_menu, "Sce.Vsh.ShellUI.CaptureMenu",
           "CaptureController", "CaptureScreen", 4,
           reinterpret_cast<void*>(&CaptureScreen_old),
           reinterpret_cast<void**>(&CaptureScreen_orig_old), false}, true) ||
      !CaptureScreen_orig_old) {
    if (!install_mono_hook(
            {"CaptureScreen(5)", img.capture_menu, "Sce.Vsh.ShellUI.CaptureMenu",
             "CaptureController", "CaptureScreen", 5,
             reinterpret_cast<void*>(&CaptureScreen_new),
             reinterpret_cast<void**>(&CaptureScreen_orig_new), false}, true) ||
        !CaptureScreen_orig_new) {
      LOG_WARN("No supported CaptureScreen overload could be hooked "
               "(4 or 5 arguments)");
      notify("notify.hook.capture");
    }
  }

  // --- GetManifestResourceStream (class name differs on 3.xx) ---
  {
    const char* klass = is_3xx ? "Assembly" : "RuntimeAssembly";
    const uint64_t method = Get_Address_of_Method(
        img.mscorlib, "System.Reflection", klass, "GetManifestResourceStream", 1);
    if (!method) {
      notify("notify.hook.master");
      return false;
    }
    (void)install_mono_hook(
        {"GetManifestResourceStream", img.mscorlib, "System.Reflection", klass,
         "GetManifestResourceStream", 1,
         reinterpret_cast<void*>(&GetManifestResourceStream_Hook),
         reinterpret_cast<void**>(&GetManifestResourceStream_Original), false});
  }

  /*
   * PUI.Application.Update is normally installed early for main-thread startup
   * handoff; install it here only if the early pass skipped or failed it.
   */
  if (!OnRender_orig) {
    (void)install_mono_hook(
        {"PUI.Application.Update", img.pui, "Sce.PlayStation.PUI",
         "Application", "Update", 0, reinterpret_cast<void*>(&OnRender_Hook),
         reinterpret_cast<void**>(&OnRender_orig), false});
  }

  return true;
}

// ---------------------------------------------------------------------------
// Phase 8: process query hooks + deferred init + keep-alive
// ---------------------------------------------------------------------------

void setup_proc_hooks() {
  onion_proc_set_sce_hooks(
      [](int pid, char* name) -> int {
        return sceKernelGetProcessName ? sceKernelGetProcessName(pid, name) : -1;
      },
      [](pid_t pid, void* info) -> int {
        return sceKernelGetAppInfo
                   ? sceKernelGetAppInfo(pid, static_cast<app_info_t*>(info))
                   : -1;
      },
      []() -> int {
        return sceSystemServiceGetAppIdOfRunningBigApp
                   ? sceSystemServiceGetAppIdOfRunningBigApp()
                   : -1;
      });
}

bool run_deferred_shellui_init(const char* thread_tag) {
  LOG_DEBUG("Running ShellUI Mono init & hooks on %s...", thread_tag);

  if (!init_version_string(g_boot_sw))
    return false;

  ShellImages images;
  if (!load_shell_images(images))
    return false;
  if (!resolve_game_container(images.app_system))
    return false;

  if (!install_hooks(images))
    return false;

  LOG_DEBUG("Performing Magic ....");
  LOG_DEBUG("Performed Magic");
  setup_proc_hooks();

  toolbox_i18n::apply_system_or_ui_lang(g_settings.ui_lang);

  shellui_hooks_publish_ready();
  onion::control_center::initialize_builtin_control();
  /*
   * Home already cached pre-inject state. Display-TID spoof and the top-nav
   * RNPS patch both need NPXS40002 ReloadApp on the UI thread (OnRender),
   * staggered ahead of NPXS40003 Control Center reload.
   */
  shellui_request_home_reload();
  return true;
}

void run_keep_alive() {
  // Publish the initialized SceShellUI process instance.  The daemon keeps
  // this marker so a restart cannot inject a second Toolbox into the same PID.
  onion_ready_signal_pid(ONION_READY_TOOLBOX, getpid());

  while (true) {
    LOG_DEBUG("sleeping ....");
    sleep(kKeepAliveSleepSec);
  }
}

} // namespace

void shellui_startup_on_render_tick(void) {
  using namespace onion::shellui;
  thread_local bool in_startup_tick = false;
  if (in_startup_tick)
    return;
  in_startup_tick = true;

  auto& coordinator = global_startup_coordinator();
  const uint64_t now_ms = startup_monotonic_ms();

  if (coordinator.try_claim_main_thread_init()) {
    if (run_deferred_shellui_init("UI main thread")) {
      const bool need_home = shellui_has_pending_home_reload();
      const bool need_cc = onion::control_center::has_pending_reload();
      coordinator.mark_main_thread_init_done(need_home, need_cc, now_ms);
      LOG_DEBUG("Main-thread init complete (need_home=%d need_cc=%d)",
                need_home ? 1 : 0, need_cc ? 1 : 0);
    } else {
      shellui_hooks_publish_failed();
      coordinator.mark_failed();
      LOG_ERROR("Main-thread ShellUI init failed");
    }
    in_startup_tick = false;
    return;
  }

  const StartupTickAction action = coordinator.tick_ui_frame(now_ms);
  switch (action) {
  case StartupTickAction::ReloadHome:
    LOG_DEBUG("Startup staggered reload: triggering NPXS40002 (HomeUI)");
    (void)shellui_poll_home_reload();
    break;
  case StartupTickAction::ReloadControlCenter:
    LOG_DEBUG("Startup staggered reload: triggering NPXS40003 (Control Center)");
    (void)onion::control_center::poll_reload();
    break;
  case StartupTickAction::None:
    break;
  }

  in_startup_tick = false;
}

// ---------------------------------------------------------------------------
// main — orchestration only
// ---------------------------------------------------------------------------

int main(int argc, char const* argv[]) {
  (void)argc;
  (void)argv;

  if (hooked)
    return 0;

  const pid_t pid = getpid();

  /*
   * Open the sinks before the first record. The logger's state is per-process
   * and ShellUI runs in its own process, so without this call every LOG_*
   * below would only reach klog and stdout — never OnionHEN.log.
   *
   * The path is the sandbox view on purpose. ShellUI runs inside the vsh
   * sandbox, which exposes /user/data; /data is the elevated view of the same
   * directory and does not resolve here. Opening /data/OnionHEN/... fails and leaves
   * the file sink closed — silently, which is how ShellUI ended up with no
   * records in the shared log. Everything else in shellui already uses
   * /user/data (assets, payloads, cheats), so this is the same file the daemon
   * opens as /data/OnionHEN/OnionHEN.log.
   *
   * OnionHEN.log is shared with the daemon; log.c serialises rotation across
   * processes and emits each record with a single O_APPEND write. The tag stays
   * distinct so records are attributable.
   *
   * The crash sink is deliberately NOT configured here: nothing in ShellUI
   * installs a signal handler, and onion_log_emergency() is only reachable from
   * one, so the sink would never be written. Wiring up a fault handler inside
   * the vsh process is its own decision — until then an unhandled fault in
   * ShellUI is simply not captured.
   */
  onion_log_configure("ShellUI", "/user/data/OnionHEN/OnionHEN.log");

  if (!resolve_native_symbols(pid))
    return -1;

  LOG_DEBUG("Starting ShellUI Module ....");

  if (!resolve_mono_symbols(pid))
    return -1;

  init_resource_names();

  OrbisKernelSwVersion sw{};
  sceKernelGetProsperoSystemSwVersion(&sw);
  g_boot_sw = sw;
  is_3xx = (sw.version < kFw3xxMaxExclusive);
  is_6xx = (sw.version >= kFw6xxMin);
  shellui_configure_debug_settings_route(sw.version);
  onion::shellui::dynamic_ui::configure(sw.version);
  if (!onion::shellui::plugin_ui_bridge::start())
    LOG_ERROR("Failed to start dynamic plugin UI bridge");
  LOG_DEBUG("System Software Version: %s is_3xx: %s debug_settings_old: %s",
              sw.version_str, is_3xx ? "Yes" : "No",
              shellui_debug_settings_uses_old_route() ? "Yes" : "No");

  if (!mono_get_root_domain)
    return -1;

  LOG_DEBUG("loading settings");
  if (!LoadSettings()) {
    LOG_ERROR("Failed to load settings");
    return -1;
  }
  LOG_DEBUG("Settings loaded successfully");

  Root_Domain = mono_get_root_domain();
  if (!Root_Domain) {
    LOG_ERROR("failed to get shellui root domain");
    return -1;
  }
  LOG_DEBUG("Shellui Root Domain: %p", static_cast<void *>(Root_Domain));
  mono_thread_attach(Root_Domain);

  /*
   * Bring the loopback web UI up before registering the built-in control. The
   * control-center tile's popup is a native WebViewPS pointed at web_ui::url(),
   * so the server has to be listening before the UI thread registers the tile.
   */
  onion::web_ui::start();

  probe_hv_bypass();
  shellui_hooks_begin_install();

  auto& coordinator = onion::shellui::global_startup_coordinator();
  const uint64_t begin_ms = onion::shellui::startup_monotonic_ms();
  coordinator.begin(begin_ms);

  const bool render_hooked = install_early_guard_hooks();
  if (render_hooked) {
    LOG_DEBUG("Waiting for PUI main thread to run ShellUI init...");
    while (coordinator.phase() ==
               onion::shellui::StartupPhase::WaitingForMainThread &&
           (onion::shellui::startup_monotonic_ms() - begin_ms) <
               kMainThreadHandoffTimeoutMs) {
      usleep(kStartupWaitPollUs);
    }
  }

  /*
   * If PUI.Application.Update could not be hooked or did not tick within the
   * handoff window (e.g., ShellUI paused behind a game), atomically claim the
   * worker fallback so boot never stalls.
   */
  if (coordinator.try_claim_worker_fallback_init()) {
    LOG_WARN("PUI main-thread handoff unavailable/timed out; falling back to "
             "inject worker");
    if (run_deferred_shellui_init("inject worker fallback")) {
      coordinator.mark_worker_fallback_done();
    } else {
      shellui_hooks_publish_failed();
      coordinator.mark_failed();
      return -1;
    }
  }

  /*
   * Wait until main-thread init and staggered RNPS reloads finish (and the
   * input suppression gate lifts) before publishing ONION_READY_TOOLBOX.
   */
  const uint64_t wait_start_ms = onion::shellui::startup_monotonic_ms();
  while (!coordinator.is_completed() && !coordinator.is_failed()) {
    if ((onion::shellui::startup_monotonic_ms() - wait_start_ms) >=
        kReloadWaitCapMs) {
      if (coordinator.force_complete_timeout()) {
        LOG_WARN("Startup RNPS reload wait hit %llu ms cap; releasing input "
                 "gate and publishing readiness",
                 static_cast<unsigned long long>(kReloadWaitCapMs));
        break;
      }
    }
    usleep(kStartupWaitPollUs);
  }

  if (coordinator.is_failed()) {
    return -1;
  }

  LOG_DEBUG("Startup guard complete; publishing Toolbox readiness");
  hooked = true;
  run_keep_alive();
  return 0;
}
