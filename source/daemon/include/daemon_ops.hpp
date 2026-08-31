/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Daemon domain ops — settings, FS, inject. msg.cpp only owns IPC_loop.
 */
#pragma once

#include <atomic>
#include <string>
#include <sys/types.h>
#include <msg.hpp>
#include <onion/ipc_server.hpp>

// clientArgs alias
using clientArgs = onion::IpcClientArgs;

/** IPC accept/handle loops only — not util lifecycle. */
extern bool is_handler_enabled;

/**
 * Full stack teardown in progress (BREW_SHUTDOWN_STACK / TCP SHUTDOWN).
 * Runtime supervisor must not relaunch :9020 or util once this is true.
 * One-way: set true, never cleared (process exits).
 */
extern std::atomic_bool g_stack_shutting_down;

/**
 * Refresh g_settings from twin config paths when either is newer. Set force
 * for an explicit IPC reload so same-second writes bypass the mtime cache.
 * Returns true if store is usable (including defaults / skip-if-current).
 * Missing config is not an error.
 */
bool LoadSettings(bool force = false);

/** After settings_save from this process, refresh mtime gate so we don't thrash. */
void SettingsNoteDiskWritten();

void reply(int sender_socket, bool error, std::string out_var = "Nothing");
/** Last reply error for BREW_LAST_RET (0 = success, -1 = error). */
int daemon_last_ipc_error();
bool remount(const char *dev, const char *path, int mnt_flag);
int change_permissions_recursive(const char *path);
bool test_sb_file(const char *filename);
int get_shellui_pid();
int get_game_pid();
void ForceKillProc(int pid);
bool set_fan_threshold(int temp);
/** Write the stock 77 C threshold used when manual control is turned off. */
bool restore_automatic_fan();
/** Periodic /dev/icc_fan rewrite while manual threshold is enabled. */
void *fan_maintenance_thread(void *args) noexcept;
/** Render/skip-hook FPS sampler: DMAP counters, publishes the FPS sample. */
void *fps_sampler_thread(void *args) noexcept;
/** V-sync FPS sampler: feeds the FPS sampler when render counters are invalid. */
void *vsync_fps_sampler_thread(void *args) noexcept;
/** Live console-language poll: pushes language changes to util over IPC. */
void *system_language_poll_thread(void *args) noexcept;

/**
 * Tear down OnionHEN userland (does not return). Caller should reply to IPC first.
 *
 * Sets g_stack_shutting_down, then: stop util → restart SceShellUI → exit.
 * The private loader is left available as the runtime recovery endpoint.
 * Does NOT kill kstuff — hard-unloading kernel patches panics the console.
 */
[[noreturn]] void cmd_shutdown_onion_stack(void);

bool cmd_enable_toolbox();
/** Schedule injection for an identified SceShellUI EXEC without blocking kevent. */
void toolbox_on_new_shellui(pid_t pid);
/** Reconcile the current ShellUI after the WORKING power-state transition. */
void toolbox_on_resume();

void *IPC_loop(void *args);
/** LAN TCP :9048 — PC can trigger BREW_SHUTDOWN_STACK without Unix socket. */
void *control_tcp_loop(void *args);
/** Re-bind the TCP :9048 listener after resume or accept failure. */
void control_tcp_restart();
/** True while the :9048 accept loop holds a live listen fd. */
bool control_tcp_is_listening();
/** Re-bind the crit Unix IPC listener after resume or accept failure. */
void restart_crit_ipc_server();
/** True while the crit Unix IPC accept loop holds a live listen fd. */
bool crit_ipc_is_listening();
void handleIPC(clientArgs *client, std::string &inputStr, DaemonCommands command);

/* ---- shared helpers (daemon_utils.cpp) ---- */
bool GetFileContents(const char *path, char **buffer);
/* Console IP: onion_net_get_ip_address() from <onion/net.h>. */
bool Get_Running_App_TID(std::string &title_id, int &BigAppid);
bool Open_Utility_Elf(const char *path, uint8_t **buffer);

/* ---- background threads ---- */
/** Update the app-jailbreak gate and wake the event listener to rebuild. */
void app_jailbreak_set_enabled(bool enabled);
void *fifo_and_dumper_thread(void *args) noexcept; // daemon_jailbreak.cpp
void *runtime_supervisor_thread(void *args) noexcept;
/** PID owned by the current util readiness marker, or -1. */
pid_t runtime_owned_util_pid();
/** PID owned by the private-loader state marker, or -1. */
pid_t runtime_owned_private_loader_pid();
void *resume_recovery_thread(void *args) noexcept;
