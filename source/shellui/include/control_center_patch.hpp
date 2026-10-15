/* Copyright (C) 2025 OnionHEN / LightningMods */
#pragma once

#ifndef SHELLUI_CONTROL_CENTER_PATCH
#define SHELLUI_CONTROL_CENTER_PATCH 1
#endif

void patch_control_center(unsigned char *buffer, int *size_ptr,
                          int buffer_capacity);
