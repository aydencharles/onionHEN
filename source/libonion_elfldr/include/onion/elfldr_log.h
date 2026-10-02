/* Copyright (C) 2024 John Törnblom

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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <onion/log.h>

#include <onion/pt.h>


/*
 * Route through the shared logger instead of printf + klog directly. libonion_elfldr
 * is -nostdlib, but it is always linked into a host that provides the platform
 * logger (daemon, bootstrapper, elfldr_server), so these records now reach the
 * file sink as well as klog and stdout — previously they bypassed it entirely.
 *
 * Every call site is a failure path, hence ERROR.
 */
#define LOG_PUTS(s) LOG_ERROR("%s", (s))

#define LOG_PRINTF(s, ...) LOG_INFO(s, __VA_ARGS__)

#define LOG_PERROR(s)							\
  LOG_ERROR("%s:%d:%s: %s", __FILE__, __LINE__, s, strerror(errno))

#define LOG_PT_PERROR(pid, s)						\
  LOG_ERROR("%s:%d:%s: %s", __FILE__, __LINE__, s, strerror(pt_errno(pid)))