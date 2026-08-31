/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include "overlay_text_metrics.hpp"
#include <onion/fps_sample.h>
#include <onion/net.h>
#include <onion/settings.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern MonoObject* rootWidget;
extern MonoObject* font;
extern OverlayLayout g_overlay_layout;
extern onion::Settings g_settings;
#include "shellui_state.hpp"
void RemoveGameWidget(RemoveWidget widget);
bool CreateGameWidget(CreateWidget widget);
MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* fontObj, int horzAlign, int vertAlign, float r, float g, float b, float a);
void Widget_Append_Child(MonoObject* widget, MonoObject* child);
MonoObject* CreateUIFont(int size, int style, int weight);

struct OrbisKernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct Proc_Stats
{
    int32_t lo_data;								//0x00
    uint32_t td_tid;						//0x04
    OrbisKernelTimespec user_cpu_usage_time;	//0x08
    OrbisKernelTimespec system_cpu_usage_time;  //0x18
}; //0x28

extern "C" {
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int get_page_table_stats(int vm, int type, int* total, int* free);
    int sceKernelGetCpuUsage(struct Proc_Stats* out, int32_t* size);
    int sceKernelGetThreadName(uint32_t id, char* out);
	int sceKernelGetCpuTemperature(int* cputemp);
    int sceKernelClockGettime(int clockId, OrbisKernelTimespec* tp);
}

struct Memory
{
    int Used;
    int Free;
    int Total;
    float Percentage;
};

struct thread_usages
{
    OrbisKernelTimespec current_time;	//0x00
    int Thread_Count;					//0x10
    char padding0[0x4];					//0x14
    Proc_Stats Threads[3072];			//0x18
};

int Thread_Count = 0;
float Usage[8] = { 0 };
float Average_Usage;
Memory RAM;
Memory VRAM;

Proc_Stats Stat_Data[3072];
thread_usages gThread_Data[2];


extern "C" int sceLncUtilKillAppWithReason(int appId, int reason);

int KillAppWithReason_Hook(int appId, int reason)
{
    return sceLncUtilKillAppWithReason(appId, reason);
}

void Get_Page_Table_Stats(int vm, int type, int* Used, int* Free, int* Total)
{
    int _Total = 0, _Free = 0;

    if (get_page_table_stats(vm, type, &_Total, &_Free) == -1) {
        LOG_ERROR("get_page_table_stats() Failed.");
        return;
    }

    if (Used)
        *Used = (_Total - _Free);

    if (Free)
        *Free = _Free;

    if (Total)
        *Total = _Total;
}

void calc_usage(unsigned int idle_tid[8], thread_usages* cur, thread_usages* prev, float usage_out[8])
{
    if (cur->Thread_Count <= 0 || prev->Thread_Count <= 0) //Make sure our banks have threads
        return;

    //Calculate the Current time difference from the last bank to the current bank.
    float Current_Time_Total = ((prev->current_time.tv_sec + (prev->current_time.tv_nsec / 1000000000.0f)) - (cur->current_time.tv_sec + (cur->current_time.tv_nsec / 1000000000.0f)));

    //Here this could use to be improved but essetially what its doing is finding the thread information for the idle threads using their thread Index stored from before.
    struct Data_s
    {
        Proc_Stats* Cur;
        Proc_Stats* Prev;
    }Data[8];

    for (int i = 0; i < cur->Thread_Count; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (idle_tid[j] == cur->Threads[i].td_tid)
                Data[j].Cur = &cur->Threads[i];
        }
    }

    for (int i = 0; i < prev->Thread_Count; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (idle_tid[j] == prev->Threads[i].td_tid)
                Data[j].Prev = &prev->Threads[i];
        }
    }

    //Here we loop through each core to calculate the total usage time as its split into user/sustem
    for (int i = 0; i < 8; i++)
    {
        float Prev_Usage_Time = (Data[i].Prev->system_cpu_usage_time.tv_sec + (Data[i].Prev->system_cpu_usage_time.tv_nsec / 1000000.0f));
        Prev_Usage_Time += (Data[i].Prev->user_cpu_usage_time.tv_sec + (Data[i].Prev->user_cpu_usage_time.tv_nsec / 1000000.0f));

        float Cur_Usage_Time = (Data[i].Cur->system_cpu_usage_time.tv_sec + (Data[i].Cur->system_cpu_usage_time.tv_nsec / 1000000.0f));
        Cur_Usage_Time += (Data[i].Cur->user_cpu_usage_time.tv_sec + (Data[i].Cur->user_cpu_usage_time.tv_nsec / 1000000.0f));

        //We calculate the usage using usage time difference between the two samples divided by the current time difference.
        float Idle_Usage = ((Prev_Usage_Time - Cur_Usage_Time) / Current_Time_Total);

        if (Idle_Usage > 1.0f)
            Idle_Usage = 1.0f;

        if (Idle_Usage < 0.0f)
            Idle_Usage = 0.0f;

        //Get inverse of idle percentage and express in percent.
        usage_out[i] = (1.0f - Idle_Usage) * 100.0f;
    }
}

namespace {

constexpr int kMaxProcThreads = 3072;
constexpr int kCpuCores = 8;
constexpr int kOverlayInitDelayFrames = 30;
constexpr int kOverlayUpdateIntervalFrames = 60;
constexpr int kFpsUpdateIntervalFrames = 2;
constexpr int kOverlayFontSize = 18;
constexpr int kClockIdRealtime = 4;
constexpr int kVmSystem = 1;
constexpr int kPageTableRam = 1;
constexpr int kPageTableVram = 2;

MonoObject *find_label(const char *widget_name) {
  MonoObject *root = Get_Property<MonoObject *>(
      pui_img, "Sce.PlayStation.PUI.UI2", "Scene", Game, "RootWidget");
  MonoClass *widget_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  return Invoke<MonoObject *>(pui_img, widget_cls, root, "FindWidgetByName",
                              mono_string_new(Root_Domain, widget_name));
}

void set_label_text(const char *widget_name, const char *text) {
  MonoClass *label_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
  MonoObject *label = find_label(widget_name);
  if (label)
    Set_Property(label_cls, label, "Text", mono_string_new(Root_Domain, text));
}

void set_label_layout(const char *widget_name, float margin_left,
                      float margin_top, float width) {
  MonoClass *label_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
  MonoClass *panel_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
  MonoObject *label = find_label(widget_name);
  if (!label)
    return;

  char cell_name[96];
  std::snprintf(cell_name, sizeof(cell_name), "%s_cell", widget_name);
  MonoObject *cell = find_label(cell_name);
  if (cell) {
    Set_Property(panel_cls, cell, "X", margin_left);
    Set_Property(panel_cls, cell, "Y", g_overlay_layout.bar_y);
    Set_Property(panel_cls, cell, "Width", width);
    Set_Property(panel_cls, cell, "Height", g_overlay_layout.bar_h);
  }

  Set_Property(label_cls, label, "PositionType", 1);
  Set_Property(label_cls, label, "MarginLeft", 0.0f);
  Set_Property(label_cls, label, "MarginTop", margin_top);
  Set_Property(label_cls, label, "Width", width);
  Set_Property(label_cls, label, "HorizontalAlignment", 0);
  Set_Property(label_cls, label, "VerticalAlignment", 0);
  Set_Property(label_cls, label, "FitWidthToText", false);
  Set_Property(label_cls, label, "FitHeightToText", true);
  Set_Property(label_cls, label, "NumberOfLines", 1);
}

/**
 * Lay out metrics as slots packed left/center/right on the full-width bar.
 * Labels use PositionType + margins layout.
 * Horizontal: label + value(s) + " | " between items (not after the last).
 *
 * Width is measured from the live text so groups (esp. RAM / IP / all-CPU)
 * expand instead of wrapping when every metric is enabled.
 */
void layout_bar_labels(const char *fps_str, const char *cpu_temp,
                       const char *cpu_usage, const char *gpu_temp,
                       const char *gpu_usage, const char *ram_str,
                       const char *ip_str, const char *fan_str) {
  constexpr float kPairGap = 8.0f;   /* label → first value */
  constexpr float kValGap = 10.0f;   /* value → value (temp / usage) */
  constexpr float kSepGap = 10.0f;   /* last value → "|" */
  constexpr float kAfterSep = 16.0f; /* "|" → next group label */
  constexpr float kOff = -4096.0f;

  struct Piece {
    const char *id;
    const char *text;
    float width;
    bool is_label; /* group title (CPU/GPU/…) */
    bool is_sep;   /* trailing pipe after an item */
  };
  std::vector<Piece> pieces;
  pieces.reserve(24);

  struct GroupSpec {
    const char *id_l;
    const char *lab;
    const char *id_v0;
    const char *v0;
    const char *id_v1;
    const char *v1;
    const char *id_sep;
  };
  GroupSpec groups[6];
  int ng = 0;

  if (g_settings.overlay_fps && fps_str)
    groups[ng++] = {"id_fps_label", "FPS", "id_fps_value", fps_str, nullptr,
                    nullptr, "id_fps_sep"};
  if ((g_settings.overlay_cpu || g_settings.all_cpu_usage) && cpu_temp)
    groups[ng++] = {"id_cpu_label",
                    "CPU",
                    "id_cpu_temp_value",
                    cpu_temp,
                    "id_cpu_usage_value",
                    cpu_usage,
                    "id_cpu_sep"};
  if (g_settings.overlay_gpu && gpu_temp)
    groups[ng++] = {"id_gpu_label",
                    "GPU",
                    "id_gpu_temp_value",
                    gpu_temp,
                    "id_gpu_usage_value",
                    gpu_usage,
                    "id_gpu_sep"};
  if (g_settings.overlay_ram && ram_str)
    groups[ng++] = {"id_ram_label", "RAM", "id_ram_value", ram_str, nullptr,
                    nullptr, "id_ram_sep"};
  if (g_settings.overlay_ip && ip_str)
    groups[ng++] = {"id_ip_label", "IP", "id_ip_value", ip_str, nullptr,
                    nullptr, "id_ip_sep"};
  if (g_settings.overlay_fan && fan_str)
    groups[ng++] = {"id_fan_label", "FAN", "id_fan_value", fan_str, nullptr,
                    nullptr, "id_fan_sep"};

  if (ng == 0)
    return;

  for (int g = 0; g < ng; ++g) {
    const GroupSpec &gs = groups[g];
    if (!gs.v0 || !gs.v0[0])
      continue;
    pieces.push_back({gs.id_l, gs.lab,
                      onion::overlay::estimate_text_width(gs.lab), true, false});
    pieces.push_back({gs.id_v0, gs.v0,
                      onion::overlay::estimate_text_width(gs.v0), false, false});
    if (gs.id_v1 && gs.v1 && gs.v1[0])
      pieces.push_back({gs.id_v1, gs.v1,
                        onion::overlay::estimate_text_width(gs.v1), false, false});
    /* Pipe after every item except the last. */
    if (g + 1 < ng)
      pieces.push_back({gs.id_sep, "|",
                        onion::overlay::estimate_text_width("|"), false, true});
  }

  if (pieces.empty())
    return;

  auto gap_after = [&](size_t i) -> float {
    if (i + 1 >= pieces.size())
      return 0.f;
    if (pieces[i].is_label)
      return kPairGap;
    if (pieces[i].is_sep)
      return kAfterSep;
    if (pieces[i + 1].is_sep)
      return kSepGap;
    if (pieces[i + 1].is_label)
      return kAfterSep;
    return kValGap;
  };

  float total = 0.f;
  for (size_t i = 0; i < pieces.size(); ++i) {
    total += pieces[i].width;
    total += gap_after(i);
  }

  const float screen_w = g_overlay_layout.bar_w;
  if (screen_w <= 1.0f)
    return;
  float x = onion::overlay::pack_origin(
      screen_w, total,
      onion::overlay::bar_align_from_value(g_settings.overlay_align));
  const float margin_top = g_overlay_layout.label_margin_top;

  static const char *kAll[] = {
      "id_fps_label",        "id_fps_value",       "id_fps_sep",
      "id_cpu_label",        "id_cpu_temp_value",  "id_cpu_usage_value",
      "id_cpu_sep",          "id_gpu_label",       "id_gpu_temp_value",
      "id_gpu_usage_value",  "id_gpu_sep",         "id_ram_label",
      "id_ram_value",        "id_ram_sep",         "id_ip_label",
      "id_ip_value",         "id_ip_sep",          "id_fan_label",
      "id_fan_value",        "id_fan_sep",
  };
  for (const char *id : kAll)
    set_label_layout(id, kOff, margin_top, 0.0f);

  for (size_t i = 0; i < pieces.size(); ++i) {
    set_label_layout(pieces[i].id, x, margin_top, pieces[i].width);
    set_label_text(pieces[i].id, pieces[i].text);
    x += pieces[i].width + gap_after(i);
  }
}

void discover_idle_thread_ids(unsigned int idle_tid[kCpuCores]) {
  int count = kMaxProcThreads;
  if (sceKernelGetCpuUsage(reinterpret_cast<Proc_Stats*>(&Stat_Data), &count) ||
      count <= 0)
    return;

  char thread_name[0x40];
  int core = 0;
  for (int i = 0; i < count; i++) {
    if (!sceKernelGetThreadName(Stat_Data[i].td_tid, thread_name) &&
        sscanf(thread_name, "SceIdleCpu%d", &core) == 1 && core >= 0 &&
        core < kCpuCores) {
      idle_tid[core] = Stat_Data[i].td_tid;
    }
  }
}

bool init_overlay_once(unsigned int idle_tid[kCpuCores]) {
  rootWidget = Get_Property<MonoObject*>(pui_img, "Sce.PlayStation.PUI.UI2", "Scene",
                                         Game, "RootWidget");
  if (!rootWidget)
    return false;

  float screen_w = 0.0f;
  float screen_h = 0.0f;
  if (!resolve_root_dimensions(rootWidget, &screen_w, &screen_h))
    return false;

  apply_overlay_layout(screen_w, screen_h);
  discover_idle_thread_ids(idle_tid);
  if (!g_settings.overlay_enabled)
    return true;

  /* style=Bold(1), weight=900 */
  font = CreateUIFont(kOverlayFontSize, 1, 900);
  if (!font)
    return false;

  RemoveGameWidget(REMOVE_ALL_OVERLAYS);
  bool widgets_ready = true;
  if (g_settings.overlay_fps) {
    if (!CreateGameWidget(CREATE_FPS_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_cpu || g_settings.all_cpu_usage) {
    if (!CreateGameWidget(CREATE_CPU_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_gpu) {
    if (!CreateGameWidget(CREATE_GPU_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_ram) {
    if (!CreateGameWidget(CREATE_RAM_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_ip) {
    if (!CreateGameWidget(CREATE_IP_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_fan) {
    if (!CreateGameWidget(CREATE_FAN_OVERLAY))
      widgets_ready = false;
  }
  if (!widgets_ready) {
    RemoveGameWidget(REMOVE_ALL_OVERLAYS);
    return false;
  }

  /* First paint: center placeholders on the full-width bar. */
  layout_bar_labels(
      g_settings.overlay_fps ? "--" : nullptr,
      (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? "--C" : nullptr,
      (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? "--%" : nullptr,
      g_settings.overlay_gpu ? "--C" : nullptr,
      g_settings.overlay_gpu ? "--%" : nullptr,
      g_settings.overlay_ram ? "---- MB" : nullptr,
      g_settings.overlay_ip ? "---.---.---.---" : nullptr,
      g_settings.overlay_fan ? "--%" : nullptr);
  return true;
}

/** Sample CPU into Usage[]; formats CPU_USAGE. Returns false if sampling skipped/failed. */
bool sample_cpu_usage(unsigned int idle_tid[kCpuCores], int& current_bank,
                      char* cpu_usage, size_t cpu_usage_sz) {
  if (!g_settings.overlay_cpu && !g_settings.all_cpu_usage)
    return false;

  /*
   * Dual-bank sample: first successful GetCpuUsage seeds the other bank; the
   * next one can compute a delta. Bound retries so a stuck/failing CPU probe
   * cannot block RAM/GPU/IP updates on the same render tick.
   */
  constexpr int kMaxAttempts = 4;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    gThread_Data[current_bank].Thread_Count = kMaxProcThreads;
    if (sceKernelGetCpuUsage(
            reinterpret_cast<Proc_Stats*>(&gThread_Data[current_bank].Threads),
            &gThread_Data[current_bank].Thread_Count))
      continue;

    Thread_Count = gThread_Data[current_bank].Thread_Count;
    sceKernelClockGettime(kClockIdRealtime, &gThread_Data[current_bank].current_time);
    current_bank = !current_bank;

    if (gThread_Data[current_bank].Thread_Count <= 0)
      continue;

    calc_usage(idle_tid, &gThread_Data[!current_bank], &gThread_Data[current_bank],
               Usage);

    if (g_settings.all_cpu_usage) {
      snprintf(cpu_usage, cpu_usage_sz,
               "%2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%%",
               Usage[0], Usage[1], Usage[2], Usage[3], Usage[4], Usage[5], Usage[6],
               Usage[7]);
    } else {
      float avg = 0.f;
      for (int i = 0; i < kCpuCores; i++)
        avg += Usage[i];
      avg /= static_cast<float>(kCpuCores);
      snprintf(cpu_usage, cpu_usage_sz, "%.0f%%", avg);
    }
    return true;
  }
  snprintf(cpu_usage, cpu_usage_sz, "--%%");
  return false;
}

void format_fps_text(char *buf, size_t buf_sz) {
  if (!buf || buf_sz == 0)
    return;
  OnionFpsSample sample {};
  if (onion_fps_read(&sample) == 0 && sample.valid && sample.fps >= 1.f) {
    if (sample.fps >= 100.f)
      snprintf(buf, buf_sz, "%.0f", sample.fps);
    else
      snprintf(buf, buf_sz, "%.1f", sample.fps);
  } else {
    snprintf(buf, buf_sz, "--");
  }
}

void update_overlay_metrics(unsigned int idle_tid[kCpuCores], int& current_bank,
                            char *cpu_temp, size_t cpu_temp_sz, char *cpu_usage,
                            size_t cpu_usage_sz, char *gpu_temp, size_t gpu_temp_sz,
                            char *gpu_usage, size_t gpu_usage_sz, char *ram_str,
                            size_t ram_sz, char *ip_address, size_t ip_sz,
                            char *fan_str, size_t fan_sz) {
  if (!g_settings.overlay_enabled)
    return;

  sample_cpu_usage(idle_tid, current_bank, cpu_usage, cpu_usage_sz);

  if (g_settings.overlay_ram) {
    Get_Page_Table_Stats(kVmSystem, kPageTableRam, &RAM.Used, &RAM.Free, &RAM.Total);
    snprintf(ram_str, ram_sz, "%u MB", RAM.Used);
  }

  if (g_settings.overlay_gpu) {
    int soc_temp = 0;
    sceKernelGetSocSensorTemperature(0, &soc_temp);
    snprintf(gpu_temp, gpu_temp_sz, "%dC", soc_temp);
    Get_Page_Table_Stats(kVmSystem, kPageTableVram, &VRAM.Used, &VRAM.Free,
                         &VRAM.Total);
    VRAM.Percentage =
        (VRAM.Total > 0) ? ((float)VRAM.Used / (float)VRAM.Total) * 100.0f : 0.f;
    snprintf(gpu_usage, gpu_usage_sz, "%.0f%%", VRAM.Percentage);
  }

  if (g_settings.overlay_ip)
    (void)onion_net_get_ip_address(ip_address, ip_sz);

  if (g_settings.overlay_cpu || g_settings.all_cpu_usage) {
    int cpu_t = 0;
    sceKernelGetCpuTemperature(&cpu_t);
    snprintf(cpu_temp, cpu_temp_sz, "%dC", cpu_t);
  }

  if (g_settings.overlay_fan) {
    uint16_t duty = 0;
    uint64_t chassis = 0;
    if (sceKernelGetCurrentFanDuty &&
        sceKernelGetCurrentFanDuty(&duty, &chassis) == 0) {
      snprintf(fan_str, fan_sz, "%.0f%%",
               (double)duty * 100.0 / 1024.0);
    } else {
      snprintf(fan_str, fan_sz, "--%%");
    }
  }
}

} // namespace

void OnRender_Hook(MonoObject* instance) {
  if (!shellui_hooks_are_ready()) {
    if (OnRender_orig)
      OnRender_orig(instance);
    return;
  }

  /* UI thread: one NPXS40002 reload covers display TIDs and top-nav patch. */
  shellui_poll_home_reload();

  static bool inited = false;
  static unsigned int idle_thread_id[kCpuCores] = {};
  static int current_bank = 0;
  static int frames_until_update = 0;
  static int frames_until_fps = 0;
  static int frames_until_init = kOverlayInitDelayFrames;
  static char gpu_temp[32] = {};
  static char gpu_usage[32] = {};
  static char cpu_temp[32] = {};
  static char cpu_usage[120] = {};
  static char ram_str[32] = {};
  static char ip_address[64] = {};
  static char fan_str[32] = "--%";
  static char fps_str[16] = "--";

  if (!inited) {
    if (frames_until_init > 0) {
      --frames_until_init;
    } else if (init_overlay_once(idle_thread_id)) {
      inited = true;
    }
    if (!inited) {
      OnRender_orig(instance);
      return;
    }
  }

  if (frames_until_update <= 0) {
    update_overlay_metrics(idle_thread_id, current_bank, cpu_temp,
                           sizeof(cpu_temp), cpu_usage, sizeof(cpu_usage),
                           gpu_temp, sizeof(gpu_temp), gpu_usage,
                           sizeof(gpu_usage), ram_str, sizeof(ram_str),
                           ip_address, sizeof(ip_address), fan_str,
                           sizeof(fan_str));
    frames_until_update = kOverlayUpdateIntervalFrames;
  } else {
    frames_until_update--;
  }

  if (frames_until_fps <= 0) {
    if (g_settings.overlay_fps)
      format_fps_text(fps_str, sizeof(fps_str));
    else
      fps_str[0] = '\0';
    layout_bar_labels(
        g_settings.overlay_fps ? fps_str : nullptr,
        (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? cpu_temp
                                                             : nullptr,
        (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? cpu_usage
                                                             : nullptr,
        g_settings.overlay_gpu ? gpu_temp : nullptr,
        g_settings.overlay_gpu ? gpu_usage : nullptr,
        g_settings.overlay_ram ? ram_str : nullptr,
        g_settings.overlay_ip ? ip_address : nullptr,
        g_settings.overlay_fan ? fan_str : nullptr);
    frames_until_fps = kFpsUpdateIntervalFrames;
  } else {
    frames_until_fps--;
  }

  OnRender_orig(instance);

  /*
   * Legacy's own UpdateProgressPanel lifecycle runs inside OnRender_orig and
   * can restore its default Progress=1.0. Apply our value after that update so
   * the final value committed for this frame is the daemon state.
   */
  shellui_poll_cheat_progress();
}
