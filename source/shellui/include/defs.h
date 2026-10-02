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

#pragma once

#include <onion/version.h>

#define PUBLIC_TEST 0
#define PRE_RELEASE 0

/*
 * The ShellUI hook diagnostics are logged unconditionally. They used to sit
 * behind a SHELL_DEBUG macro that followed NDEBUG, which meant a release build
 * silently dropped the records that explain why a HomeUI profile did not match
 * or why a hook declined to fire — the exact questions a bug report asks.
 *
 * ShellUI is an injected system process, so the volume is real. Gate it at run
 * time instead of compile time: `[logging] level` in config.ini, or the
 * "Log output level" selector in the Toolbox. Most of these records are DEBUG,
 * so they need level=debug; the failure paths are WARN/ERROR and show at the
 * default level.
 */

#define libSceKernelHandle 0x2001
#define KERNEL_DLSYM(handle, sym) \
    (*(void**)&sym=(void*)kernel_dynlib_dlsym(-1, handle, #sym))


typedef void* ScePthread;
