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

/*
 * HomeUI RNPS/Hermes top-nav patch:
 * inserts the OnionHEN icon-button slot between Search and Settings by reusing
 * a hidden system entry.
 *
 * Disable for A/B crash isolation (must recompile shellui):
 *   -D SHELLUI_HOMEUI_TOP_NAV_PATCH=0
 * or CMake: -DONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH=OFF
 *
 * The SetIconSource focus-icon mirror lives in onion_icon_source_hook.hpp now:
 * the Control Center host needs it too, and neither A/B switch may blank the
 * other feature's focused icon.
 */
#ifndef SHELLUI_HOMEUI_TOP_NAV_PATCH
#define SHELLUI_HOMEUI_TOP_NAV_PATCH 1
#endif

void patch_homeui_top_nav(unsigned char *buffer, int *size_ptr,
                          int buffer_capacity);
