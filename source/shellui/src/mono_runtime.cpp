/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Extracted from mono_utils.cpp for module locality.
 */

#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "defs.h"
#include "ipc.hpp" // shellui_log
#include <atomic>
#include <string>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace {

std::atomic<bool> g_home_reload_pending{false};

} // namespace

MonoImage * getDLLimage(const char* dll_file){
  
  std::string dll_path = "/system_ex/common_ex/lib/" + std::string(dll_file);
  MonoAssembly * Assembly = mono_domain_assembly_open(Root_Domain, dll_path.c_str());
  if (!Assembly) {
    LOG_ERROR("Failed to open assembly %s.", dll_path.c_str());
    return nullptr;
  }

  MonoImage * img = mono_assembly_get_image(Assembly);
  if (!img) {
    LOG_ERROR("Failed to get image %s.", dll_path.c_str());
    return nullptr;
  }
  return img;
}


MonoObject* New_Object(MonoClass* Klass)
{
    if (Klass == nullptr)
    {
        return nullptr;
    }

    return mono_object_new(Root_Domain, Klass);
}

MonoObject* CreateUIColor(float r, float g, float b, float a)
{
    MonoClass* uIColor = mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UIColor");

    // Allocates memory for our new instance of a class.
    MonoObject* uIColorInstance = New_Object(uIColor);
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIColorInstance);

    Invoke<void>(pui_img, uIColor, realInstance, ".ctor", r, g, b, a);

    return realInstance;
}

MonoObject* CreateUIFont(int size, int style, int weight)
{
    MonoClass* uIFont = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "UIFont");

    // Allocates memory for our new instance of a class.
    MonoObject* uIFontInstance = New_Object(uIFont);
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIFontInstance);

    Invoke<void>(pui_img, uIFont, realInstance, ".ctor", size, style, weight);

    return realInstance;
}

MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a)
{
    MonoClass* labelClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");

    // Allocates memory for our new instance of a class.
    MonoObject* labelInstance = New_Object(labelClass);

    // Call Constructor.
    mono_runtime_object_init(labelInstance);

    Set_Property(labelClass, labelInstance, "Name", mono_string_new(Root_Domain, name));
    Set_Property(labelClass, labelInstance, "PositionType", 1);
    Set_Property(labelClass, labelInstance, "MarginLeft", x);
    Set_Property(labelClass, labelInstance, "MarginTop", y);
    Set_Property(labelClass, labelInstance, "Text", mono_string_new(Root_Domain, text));
    Set_Property_Invoke(labelClass, labelInstance, "Font", font);
    Set_Property(labelClass, labelInstance, "HorizontalAlignment", horzAlign);
    Set_Property(labelClass, labelInstance, "VerticalAlignment", vertAlign);
    Set_Property_Invoke(labelClass, labelInstance, "TextColor", CreateUIColor(r, g, b, a));

    Set_Property(labelClass, labelInstance, "FitWidthToText", false);
    Set_Property(labelClass, labelInstance, "FitHeightToText", true);
    Set_Property(labelClass, labelInstance, "NumberOfLines", 1);
    /* Readable HUD text on dark bar. */
    Set_Property(labelClass, labelInstance, "EnableThemedTextShadow", true);

    return labelInstance;
}

void Widget_Append_Child(MonoObject* widget, MonoObject* child)
{
    MonoClass* widgetClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
    MonoMethod* appendChild = mono_class_get_method_from_name(widgetClass, "AppendChild", 1);

    void* args[1];
    args[0] = child;

    mono_runtime_invoke(appendChild, widget, args, nullptr);
}

/*
 * ShellUI methods are AOT. Prefer that image so we patch the code the UI
 * thread is already running. compile_method from the inject worker can take
 * the JIT lock and emit a second copy — then the detour misses live calls.
 */
static uint64_t address_of_compiled_method(MonoMethod *method) {
  if (!method)
    return 0;

  MonoDomain *domain = Root_Domain;
  if (!domain && mono_get_root_domain)
    domain = mono_get_root_domain();

  uint64_t addr = 0;
  if (mono_aot_get_method && domain)
    addr = mono_aot_get_method(domain, method);
  if (!addr && mono_compile_method)
    addr = mono_compile_method(method);
  return addr;
}

uint64_t Get_Address_of_Method(MonoImage *Assembly_Image, const char *Name_Space, const char *Class_Name, const char *Method_Name, int Param_Count)
{
  MonoClass *klass = mono_class_from_name(Assembly_Image, Name_Space, Class_Name);
  if (!klass)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Get_Address_of_Method: failed to open class \"%s\" in namespace \"%s\"", Class_Name, Name_Space);
#endif
    return 0;
  }

  MonoMethod *Method = mono_class_get_method_from_name(klass, Method_Name, Param_Count);
  if (!Method)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Get_Address_of_Method: failed to find method \"%s\" in class \"%s\"", Method_Name, Class_Name);
#endif
    return 0;
  }

  return address_of_compiled_method(Method);
}

uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, MonoClass* klass, const char* Method_Name, int Param_Count)
{
	if (!klass)
	{
		return 0;
	}

	MonoMethod* Method = mono_class_get_method_from_name(klass, Method_Name, Param_Count);

	if (!Method)
	{
		return 0;
	}

  return address_of_compiled_method(Method);
}

MonoObject *Get_Instance(MonoClass *klass, const char *Instance)
{

  MonoProperty *inst_prop = mono_class_get_property_from_name(klass, Instance);
  if (!inst_prop)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Failed to find Instance property \"%s\" in Class \"%s\".", Instance, klass->name);
#endif
    return nullptr;
  }

  MonoMethod *inst_get_method = mono_property_get_get_method(inst_prop);
  if (!inst_get_method)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Failed to find get method for \"%s\" in Class \"%s\".", Instance, klass->name);
#endif
    return nullptr;
  }

  MonoObject *inst = mono_runtime_invoke(inst_get_method, 0, 0, 0);
  if (!inst)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Failed to find get Instance \"%s\" in Class \"%s\".", Instance, klass->name);
#endif
    return nullptr;
  }

  return inst;
}

std::string Mono_to_String(MonoString *str)
{
  if (!str)
  {
    return "";
  }

  if (!mono_string_to_utf8)
    return "";

  const char *c_str = mono_string_to_utf8(str);
  if (!c_str)
    return "";

  std::string ret(c_str);
  if (mono_free)
    mono_free((void *)c_str);
  return ret;
}

std::string GetPropertyValue(MonoObject *element, const char *propertyName)
{
  std::string ret_val;
  MonoClass *elementClass = element->vtable->klass;
  MonoProperty *property = mono_class_get_property_from_name(elementClass, propertyName);
  if (!property)
  {
    //  LOG_ERROR("[LM HOOK] OnPress_Hook: Property %s not found", propertyName);
    return std::string();
  }

  MonoMethod *getter = mono_property_get_get_method(property);
  if (!getter)
  {
    // LOG_ERROR("[LM HOOK] OnPress_Hook: Getter for property %s not found", propertyName);
    return std::string();
  }

  MonoObject *result = mono_runtime_invoke(getter, element, nullptr, nullptr);
  if (!result)
  {
    // LOG_DEBUG("[LM HOOK] OnPress_Hook: Getter for property %s returned nullptr", propertyName);
    return std::string();
  }
  return Mono_to_String((MonoString *)result);
}

MonoObject *InvokeByDesc(MonoClass *p_Class, const char *p_MethodDesc, void *p_Instance, void *p_Args)
{
  MonoMethodDesc *s_MethodDesc = mono_method_desc_new(p_MethodDesc, 1);
  auto s_ClassMethod = mono_method_desc_search_in_class(s_MethodDesc, p_Class);
  mono_method_desc_free(s_MethodDesc);
  if (s_ClassMethod == nullptr)
    return nullptr;

  return mono_runtime_invoke(s_ClassMethod, p_Instance, (void **)p_Args, nullptr);
}

/* Keep a few recent streams rooted because Settings can consume returned
 * streams after rapid resource re-entry. Old slots are released on reuse. */
static std::mutex g_xml_stream_gchandle_lock;
static constexpr size_t kXmlStreamHandleSlots = 4;
static uint32_t g_xml_stream_gchandles[kXmlStreamHandleSlots] = {};
static size_t g_xml_stream_gchandle_cursor = 0;

static uint32_t root_xml_stream(MonoObject *stream)
{
  if (!stream || !mono_gchandle_new)
    return 0;

  std::lock_guard<std::mutex> lock(g_xml_stream_gchandle_lock);
  uint32_t old_handle = g_xml_stream_gchandles[g_xml_stream_gchandle_cursor];
  if (old_handle != 0 && mono_gchandle_free)
    mono_gchandle_free(old_handle);

  uint32_t handle = mono_gchandle_new(stream, /*pinned=*/1);
  g_xml_stream_gchandles[g_xml_stream_gchandle_cursor] = handle;
  g_xml_stream_gchandle_cursor =
      (g_xml_stream_gchandle_cursor + 1) % kXmlStreamHandleSlots;
  return handle;
}

MonoObject *New_Mono_XML_From_String(std::string xml_doc, MonoDomain *domain)
{
  if (!domain)
    domain = (mono_domain_get ? mono_domain_get() : nullptr);
  if (!domain)
    domain = Root_Domain;

  LOG_DEBUG("[GMRS] New_Mono_XML_From_String: xml_size=%zu domain=%p Root_Domain=%p MemoryStream_IO=%p",
              xml_doc.size(), (void *)domain, (void *)Root_Domain,
              (void *)MemoryStream_IO);

  if (xml_doc.empty()) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: empty xml_doc");
    return nullptr;
  }
  if (!domain) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: domain is null");
    return nullptr;
  }
  if (!MemoryStream_IO) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: MemoryStream_IO is null");
    return nullptr;
  }

  MonoArray *Array = mono_array_new(domain, mono_get_byte_class(), xml_doc.size());
  if (!Array)
  {
    LOG_ERROR("[GMRS] New_Mono_XML_From_String: Failed to create byte[] array (size=%zu)", xml_doc.size());
    return nullptr;
  }

  char *Array_addr = mono_array_addr_with_size(Array, sizeof(char), 0);
  if (!Array_addr) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: mono_array_addr_with_size returned null");
    return nullptr;
  }
  /* Do NOT mprotect mono heap pages. Array_addr is usually mid-page; mprotect
   * rounds to page bounds and can change permissions on neighboring GC
   * objects — intermittent SIGSEGV when opening DebugSettings quickly or from
   * a notification (Hermes/RN still busy). Mono byte[] is already writable. */
  memcpy(Array_addr, xml_doc.data(), xml_doc.size());

  MonoObject *stream = mono_object_new(domain, MemoryStream_IO);
  if (!stream)
  {
    MemoryStream_IO = nullptr;
    LOG_ERROR("[GMRS] New_Mono_XML_From_String: Failed to create MemoryStream_Instance");
    return nullptr;
  }
  void *args[] = {Array};
  InvokeByDesc(MemoryStream_IO, ":.ctor(byte[])", stream, args);

  uint32_t gchandle = root_xml_stream(stream);

  LOG_DEBUG("[GMRS] New_Mono_XML_From_String: ok instance=%p gchandle=%u",
              (void *)stream, gchandle);
  return stream;
}

bool SetVersionString(const char *str)
{
  MonoAssembly *Assembly = mono_domain_assembly_open(Root_Domain, uilib_dll.c_str());
  if (!Assembly)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("SetVersionString: Failed to open assembly.");
#endif
    return false;
  }
  MonoClass *SystemSoftwareVersionInfo = mono_class_from_name(mono_assembly_get_image(Assembly), uilib.c_str(), Sysinfo.c_str());
  if (!SystemSoftwareVersionInfo)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("SetVersionString: Failed to open class.");
#endif
    return false;
  }

  MonoObject *SystemSoftwareVersionInfo_Instance = Get_Instance(SystemSoftwareVersionInfo, "Instance");
  if (!SystemSoftwareVersionInfo_Instance)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("SetVersionString: Failed to open Instance.");
#endif
    return false;
  }

  MonoMethod *Set_Method = mono_class_get_method_from_name(SystemSoftwareVersionInfo, display_info.c_str(), 1);
  if (Set_Method == nullptr)
  {
#if SHELL_DEBUG == 1
    LOG_DEBUG("SetVersionString: Could not find set method.");
#endif
    return false;
  }

  MonoObject *exception = nullptr;
  void *args[] = {mono_string_new(Root_Domain, str)};
  //    MonoObject* result = mono_runtime_invoke(send_by_id_method, nullptr, args, &exception);
  mono_runtime_invoke(Set_Method, SystemSoftwareVersionInfo_Instance, args, &exception);
  if (exception)
  {
    MonoString *exc_string = mono_object_to_string(exception, nullptr);
    const char *exc_chars = mono_string_to_utf8(exc_string);
#if SHELL_DEBUG == 1
    LOG_DEBUG("Exception: %s", exc_chars);
#endif
    mono_free((void *)exc_chars);
    return false;
  }
  return true;
}

void ReloadRNPSApp(const char* title_id){
    void (*ReloadApp)(MonoString* tid) = (void(*)(MonoString*))Get_Address_of_Method(react_common_img, "ReactNative.Vsh.Common", "ReactApplicationSceneManager", "ReloadApp", 1);
    if (ReloadApp) {
        LOG_DEBUG("Reloading %s scenes", title_id);
        ReloadApp(mono_string_new(Root_Domain, title_id));
    } else {
        LOG_ERROR("Failed to find reload method, not reloading scene");
    }
}

void shellui_request_home_reload(void) {
  if (g_home_reload_pending.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  LOG_DEBUG("queued NPXS40002 home reload for next UI tick");
}

void shellui_poll_home_reload(void) {
  if (!g_home_reload_pending.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  LOG_DEBUG("applying NPXS40002 home reload on UI thread");
  ReloadRNPSApp("NPXS40002");
}
