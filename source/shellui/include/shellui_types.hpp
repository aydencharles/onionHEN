/* Copyright (C) 2025 OnionHEN / LightningMods
 * ShellUI domain types / enums / settings externs (no Mono hooks).
 */
#pragma once
#include "external_symbols.hpp"
#include "overlay_layout.hpp"
#include <string>
#include <vector>
#include <iostream>
#include "defs.h"
#include <onion/settings.hpp>

#define MAX_LINE 256
#define MAX_PAIRS 100

#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING 0x8094000c
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED 0x80940010
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED 0x80940011

#define SCE_REGMGR_ENT_KEY_DEVENV_TOOL_SHELLUI_disp_titleid 2013448470
#define SCE_REGMGR_INT_SIZE 4
#define SCE_REGMGR_ERROR_PRM_REGID 0x800D0203

typedef struct {
    char key[MAX_LINE];
    char value[MAX_LINE];
} KeyValue;

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

typedef struct IniParser_t {
    KeyValue pairs[MAX_PAIRS];
    int count = 0;
} IniParser;

enum RemoveWidget {
    REMOVE_GPU_OVERLAY,
    REMOVE_CPU_OVERLAY,
    REMOVE_RAM_OVERLAY,
    REMOVE_IP_OVERLAY,
    REMOVE_FPS_OVERLAY,
    REMOVE_ALL_OVERLAYS,
};

enum CreateWidget {
    CREATE_GPU_OVERLAY,
    CREATE_CPU_OVERLAY,
    CREATE_RAM_OVERLAY,
    CREATE_IP_OVERLAY,
    CREATE_FPS_OVERLAY,
    CREATE_ALL_OVERLAYS,
};

struct WidgetConfig {
    const char* id;
    float x, y;
    const char* text;
    int bold;
    float r, g, b, a;
};

void RemoveGameWidget(RemoveWidget widget);
bool CreateGameWidget(CreateWidget widget);

struct LaunchAppParam
{
  unsigned int size;
  int userId;
  int appAttr;
  int enableCrashReport;
  int checkFlag;
  unsigned long contextId;
  bool isSpeculativeLaunch;
};

/** List entry for a payload .elf in the toolbox. */
typedef struct {
    std::string path;
    std::string shellui_path;
    std::string tid;  /* stem key for PID / launch */
    std::string id;
    std::string name;
    std::string version;
} PayloadEntry;

typedef struct {
    std::string path;
    std::string shellui_path;
    std::string id;
    std::string name;
    std::string version;
} Payloads_Apps;

struct GameEntry {
    std::string tid;
    std::string title;
    std::string version;
    std::string path;
    std::string dir_name;
    std::string icon_path;
    std::string id;
};

// games_list: see shellui_state.hpp (ToolboxUiState)
// all_cpu_usage: overlay.cpu_usage_mode=per_core (config.ini)
enum Cheats_Shortcut{
    CHEATS_SC_OFF = 0,
    R3_L3,
    L2_TRIANGLE,
    LONG_OPTIONS,
    CHEATS_LONG_SHARE,
    CHEATS_SINGLE_SHARE,
 };

 enum Toolbox_Shortcut{
    TOOLBOX_SC_OFF = 0,
    L2_R3,
    TOOLBOX_LONG_SHARE,
    TOOLBOX_SINGLE_SHARE,
 };

 /**
  * Vertical edge for the Game++-style single-row monitor bar.
  * Values 1/3 keep the legacy TR/BR mapping → same as top/bottom.
  */
 enum overlay_positions{
    OVERLAY_POS_TOP_LEFT = 0,
    OVERLAY_POS_TOP_RIGHT,
    OVERLAY_POS_BOTTOM_LEFT,
    OVERLAY_POS_BOTTOM_RIGHT
 };

extern onion::Settings g_settings;

/**
 * Overlay layout: full-width edge strip + left-, center-, or right-packed metrics.
 * bar_x/bar_w are always 0 / screen width; bar_y is 0 or (H - bar_h).
 */
using OverlayLayout = onion::overlay::Layout;
extern OverlayLayout g_overlay_layout;
