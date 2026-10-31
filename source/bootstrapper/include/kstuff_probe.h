/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Detect whether the kstuff capability is already active.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * True when userland mprotect accepts RWX. This is a capability signal and
 * does not identify which payload or system component enabled it.
 */
bool kstuff_mprotect_available(void);

#ifdef __cplusplus
}
#endif
