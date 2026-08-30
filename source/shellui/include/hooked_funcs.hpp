/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * ShellUI API surface: types + Mono hooks.
 * Prefer including shellui_types.hpp alone when hooks are not needed.
 * Do NOT include this from ipc.hpp (breaks compile seam).
 */
#pragma once
#include "shellui_types.hpp"
#include "hook_lifecycle.hpp"
#include <cstdint>

enum GamePadButtons
	{
        None = 0,
		// Token: 0x040000D0 RID: 208
		Left = 1U,
		// Token: 0x040000D1 RID: 209
		Up = 2U,
		// Token: 0x040000D2 RID: 210
		Right = 4U,
		// Token: 0x040000D3 RID: 211
		Down = 8U,
		// Token: 0x040000D4 RID: 212
		Square = 16U,
		// Token: 0x040000D5 RID: 213
		Triangle = 32U,
		// Token: 0x040000D6 RID: 214
		Circle = 64U,
		// Token: 0x040000D7 RID: 215
		Cross = 128U,
		// Token: 0x040000D8 RID: 216
		Start = 256U,
		// Token: 0x040000D9 RID: 217
		Select = 512U,
		// Token: 0x040000DA RID: 218
		Option = 256U,
		// Token: 0x040000DB RID: 219
		L1 = 1024U,
		// Token: 0x040000DC RID: 220
		R1 = 2048U,
		// Token: 0x040000DD RID: 221
		L2 = 4096U,
		// Token: 0x040000DE RID: 222
		R2 = 8192U,
		// Token: 0x040000DF RID: 223
		L3 = 16384U,
		// Token: 0x040000E0 RID: 224
		R3 = 32768U,
		// Token: 0x040000E1 RID: 225
		Enter = 65536U,
		// Token: 0x040000E2 RID: 226
		Back = 131072U,
		// Token: 0x040000E3 RID: 227
		TouchPad = 262144U,
		// Token: 0x040000E4 RID: 228
		Move = 524288U,
		// Token: 0x040000E5 RID: 229
		Intercepted = 2147483648U
    };

struct GamePadData
{
  // Token: 0x040000E6 RID: 230
   bool Skip = false;

  // Token: 0x040000E7 RID: 231
   GamePadButtons Buttons = GamePadButtons(0);

  // Token: 0x040000E8 RID: 232
   GamePadButtons ButtonsPrev = GamePadButtons(0);

  // Token: 0x040000E9 RID: 233
   GamePadButtons ButtonsDown = GamePadButtons(0);

  // Token: 0x040000EA RID: 234
   GamePadButtons ButtonsUp = GamePadButtons(0);

  // Token: 0x040000EB RID: 235
   float AnalogLeftX = 0.0f;

  // Token: 0x040000EC RID: 236
   float AnalogLeftY = 0.0f;

  // Token: 0x040000ED RID: 237
   float AnalogRightX = 0.0f;
  // Token: 0x040000EE RID: 238
   float AnalogRightY = 0.0f;
};

struct ioctl_C0105203_args
{
  void* buffer;
  int size;
  int error;
};

void __syscall();
extern bool is_patches_plugin_running;

/* Minimal DIR surface used by payload scanners (not full BSD dirent). */
typedef struct _dirdesc {
    int dd_fd;
    long dd_loc;
    long dd_size;
    char *dd_buf;
    int dd_len;
    long dd_seek;
    long dd_rewind;
    int dd_flags;
    struct pthread_mutex *dd_lock;
    struct _telldir *dd_td;
} DIR;

extern "C" DIR * opendir(const char*);
extern "C" struct dirent* readdir(DIR*);
extern "C" int closedir(DIR*);

void notify(const char* text, ...);

extern uint64_t(*GetManifestResourceStream_Original)(uint64_t inst, MonoString* FileName);
extern void (*DebugSettings_GetModel_Orig)(MonoObject* instance, MonoObject* param, MonoObject* promise);
extern void (*ReactNavigatorManager_UpdateNavigationState_Orig)(MonoObject* instance, MonoObject* state);
extern  void (*OnShareButton_orig)(MonoObject* data);
extern void (*CaptureScreen_orig_old)(MonoObject * inst, int userId, long deviceId, int capType, MonoObject* capacityInfo);
extern void (*CaptureScreen_orig_new)(MonoObject* inst, int userId, long deviceId, int capType,  MonoString* format, MonoObject* capInfo);
extern int (*LaunchApp_orig)(MonoString* titleId, uint64_t* args, int argsSize, LaunchAppParam *param);
extern void (*OnRender_orig)(MonoObject* instance);
extern void (*Orig_ReloadApp)(MonoString* str);
extern MonoMethod* set_value_method;
extern MonoImage * react_common_img;

/* =============================== mono utils =============================================================================*/
std::string Mono_to_String(MonoString* str);
std::string GetPropertyValue(MonoObject* element, const char* propertyName);
std::string base64_decode(const std::string &encoded_string);
std::vector<unsigned char> encrypt_decrypt(const unsigned char *data, size_t size, const std::string &key);
void ReloadRNPSApp(const char* title_id);

void generate_payload_xml(std::string& xml_buffer, bool list_page);
void generate_account_xml(std::string& xml_buffer);
void generate_toolbox_xml(std::string& new_xml);
void Patch_Main_thread_Check(MonoImage * image_core);
uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, const char* Name_Space, const char* Class_Name, const char* Method_Name, int Param_Count);
uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, MonoClass* klass, const char* Method_Name, int Param_Count);
uint64_t GetManifestResourceStream_Hook(uint64_t inst, MonoString* FileName);
void DebugSettings_GetModel_Hook(MonoObject* instance, MonoObject* param, MonoObject* promise);
void ReactNavigatorManager_UpdateNavigationState_Hook(MonoObject* instance, MonoObject* state);
MonoObject* New_Mono_XML_From_String(std::string xml_doc, MonoDomain* domain);
bool write_asset(const char* path, const void* start, uint32_t size);
int ini_parser_load(IniParser* parser, const char* filename);
const char* ini_parser_get(IniParser* parser, const char* key, const char* default_value);
bool LoadSettings();
bool SaveSettings();
/** Recompute g_overlay_layout from overlay edge and align. */
void apply_overlay_layout();
/** Apply the overlay layout using the live ShellUI logical canvas dimensions. */
void apply_overlay_layout(float screen_w, float screen_h);
/** Read both logical canvas dimensions from a ShellUI RootWidget. */
bool resolve_root_dimensions(MonoObject *root, float *screen_w, float *screen_h);
/** Persist g_settings and optionally reload daemon/util settings via IPC. */
void settings_commit(bool reload_main = false, bool reload_util = false);

/**
 * Queue one NPXS40002 ReloadApp for the next OnRender. Display-TID spoof and
 * HomeUI top-nav patch both need the same Home scene refresh; coalescing
 * avoids two ReloadApp calls on one tick. Never call ReloadRNPSApp from the
 * inject worker. Not timed — readiness is hooks-ready + UI thread, not
 * onion_ready files (those are daemon/process handshake only).
 */
void shellui_request_home_reload(void);
/** Drain the one-shot Home reload; call only from UI thread after hooks ready. */
void shellui_poll_home_reload(void);

/** UI-thread ticker for the cheat-download XML progress page. */
void shellui_poll_cheat_progress(void);

bool SetVersionString(const char* str);
int SendShelluiNotify();

template <typename result>
result Get_Property(MonoClass* Klass, MonoObject* Instance, const char* Property_Name)
{
    if (Klass == 0)
    {
        return (result)0;
    }

    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);

    if (Prop == 0)
    {
        return (result)0;
    }

    MonoMethod* Get_Method = mono_property_get_get_method(Prop);

    if (Get_Method == 0)
    {
        return (result)0;
    }

    uint64_t Get_Method_Thunk = (uint64_t)mono_compile_method(Get_Method);

    if (Get_Method_Thunk == 0)
    {
        return (result)0;
    }

    if (Instance != 0)
    {
        result(*Method)(MonoObject* Instance) = decltype(Method)(Get_Method_Thunk);
        return Method(Instance);
    }
    else
    {
        result(*Method)() = decltype(Method)(Get_Method_Thunk);
        return Method();
    }
}

template <typename result>
result Get_Property(MonoImage* Assembly_Image, const char* Namespace, const char* Class_Name, MonoObject* Instance, const char* Property_Name)
{
    return Get_Property<result>(mono_class_from_name(Assembly_Image, Namespace, Class_Name), Instance, Property_Name);
}

template <typename Param>
void Set_Property(MonoClass* Klass, MonoObject* Instance, const char* Property_Name, Param Value)
{
    if (Klass == nullptr)
    {
        return;
    }

    if (Instance == nullptr)
    {
        return;
    }

    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);

    if (Prop == nullptr)
    {
        return;
    }

    MonoMethod* Set_Method = mono_property_get_set_method(Prop);

    if (Set_Method == nullptr)
    {
        return;
    }

    uint64_t Set_Method_Thunk = (uint64_t)mono_compile_method(Set_Method);

    if (Set_Method_Thunk == 0)
    {
        return;
    }

    void(*Method)(MonoObject* Instance, Param Value) = decltype(Method)(Set_Method_Thunk);
    Method(Instance, Value);
}

template <typename Param>
void Set_Property_Invoke(MonoClass* Klass, MonoObject* Instance, const char* Property_Name, Param Value)
{
    if (Klass == nullptr)
    {
        return;
    }

    if (Instance == nullptr)
    {
        return;
    }

    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);

    if (Prop == nullptr)
    {
        return;
    }

    MonoMethod* Set_Method = mono_property_get_set_method(Prop);

    if (Set_Method == nullptr)
    {
        return;
    }

    mono_runtime_invoke(Set_Method, Instance, (void**)&Value, 0);
}

#define ARRAY_COUNT(arry) sizeof(arry) / sizeof(arry[0])

template <typename result, typename... Args>
result Invoke(MonoImage* Assembly_Image, MonoClass* klass, MonoObject* Instance, const char* Method_Name, Args... args)
{
    void* Argsv[] = { &args... };
    uint64_t ThunkAddress = Get_Address_of_Method(Assembly_Image, klass, Method_Name, ARRAY_COUNT(Argsv));

    if (!ThunkAddress)
    {
        return (result)0;
    }

    if (Instance)
    {
        result(*Method)(MonoObject* Instance, Args... args) = decltype(Method)(ThunkAddress);
        return Method(Instance, args...);
    }
    else //Static Call.
    {
        result(*Method)(Args... args) = decltype(Method)(ThunkAddress);
        return Method(args...);
    }
}


/* ================================= ORIG HOOKED MONO FUNCS ============================================= */
extern int (*oOnPress)(MonoObject* Instance, MonoObject* element, MonoObject* e);
extern int (*oOnPreCreate)(MonoObject* Instance, MonoObject* element);
extern void (*oUserCustomElementReset)(MonoObject* Instance, MonoObject* item);
extern void (*oSettingPageStackOnPopping)(MonoObject* Instance,
                                          MonoObject* outgoing,
                                          MonoObject* incoming);
extern MonoString* (*CxmlUri)(MonoObject* obj,MonoString* uri);

extern bool (*boot_orig)(MonoString* uri, int opt, MonoString* titleIdForBootAction);
extern bool (*boot_orig_2)(MonoString* uri, int opt);
extern GamePadData (*GetData)(int deviceIndex);
extern MonoString *(*oGetString)(MonoObject *Instance, MonoString *str);
extern void (*createJson)(MonoObject*, MonoObject* array, MonoString* id, MonoString* label, MonoString* actionUrl, MonoString* actionId, MonoString* messageId, MonoObject* subMenu, bool enable);

extern int (*__sys_regmgr_call)(long, long, int*, int*, long);

/* ================================= HOOKED MONO FUNCS ============================================= */
#include "shellui_state.hpp"

extern  std::string cheats_xml;
extern  std::string UI3_dec;
extern  std::string legacy_dec;
extern  std::string appsystem_dll;
extern  std::string uilib;
extern  std::string Sysinfo;
extern  std::string display_info;
extern  std::string uilib_dll;

extern  std::string payloads_xml;
extern  std::string debug_settings_xml;
extern MonoImage* pui_img;
extern MonoImage* AppSystem_img;
extern MonoObject* Game;



MonoObject* CreateUIColor(float r, float g, float b, float a);
MonoObject* CreateUIFont(int size, int style, int weight);
MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a);
void Widget_Append_Child(MonoObject* widget, MonoObject* child);
MonoObject* New_Object(MonoClass* Klass);
MonoString *GetString_Hook(MonoObject *Instance, MonoString *str);
int OnPress_Hook(MonoObject* Instance, MonoObject* element, MonoObject* e);
int OnPreCreate_Hook(MonoObject* Instance, MonoObject* element);
void UserCustomElementReset_Hook(MonoObject* Instance, MonoObject* item);
void SettingPageStackOnPopping_Hook(MonoObject* Instance,
                                    MonoObject* outgoing,
                                    MonoObject* incoming);
MonoImage * getDLLimage(const char* dll_file);
MonoString* CxmlUri_Hook(MonoObject* obj, MonoString* uri);
MonoObject* InvokeByDesc(MonoClass* p_Class, const char* p_MethodDesc, void* p_Instance, void* p_Args);
void generate_plapps_xml(std::string& new_xml);
MonoString* GetString(MonoString* str);
int ItemzLaunchByUri(const char* uri);
void GoToHome();
void GoToURI(const char* uri);
bool Get_Running_App_TID(std::string& title_id, int& BigAppid);
void generate_cheats_xml(std::string &new_xml, std::string& not_open_tid, bool running_as_debug_settings, bool show_while_not_open);
#include <onion/proc_query.h>
#include <onion/platform.h>
int sceSystemServiceGetAppId(const char *tid);
/** USB mass-storage index, or -1 when none mounted. */
int usbpath();
extern "C" int sceUserServiceGetInitialUser(int* uid);
extern "C" int sceUserServiceGetForegroundUser(int* uid);
void ParseCheatID(const char* id, char* tid, int* cheat_id);
int Launch_FG_Game(const char *path, const char* title_id, const char* title);
bool uri_boot_hook(MonoString* uri, int opt, MonoString* titleIdForBootAction);
bool uri_boot_hook_2(MonoString* uri, int opt);
GamePadData GetData_hook(int deviceIndex);
void OnShareButton(MonoObject * data);
void CaptureScreen_old(MonoObject*  inst, int userId, long deviceId, int capType, MonoObject* capInfo);
void CaptureScreen_new(MonoObject*  inst, int userId, long deviceId, int capType,  MonoString* format, MonoObject* capInfo);
int ioctl_hook (int fd, unsigned long request, void *argp);
int LaunchApp(MonoString* titleId, uint64_t* args, int argsSize, LaunchAppParam *param);
int sceRegMgrGetInt_hook(long regid, int* out_val);
void createJson_hook(MonoObject* inst, MonoObject* array, MonoString* id, MonoString* label = nullptr, MonoString* actionUrl = nullptr, MonoString* actionId = nullptr, MonoString* messageId = nullptr, MonoObject* subMenu = nullptr, bool enable = true);
/* ================================= HOOKED MONO FUNCS ============================================= */
