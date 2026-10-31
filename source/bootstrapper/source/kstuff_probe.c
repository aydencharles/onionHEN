/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "kstuff_probe.h"

#include <stddef.h>
#include <string.h>

int sceKernelMprotect(void *addr, size_t len, int prot);

bool kstuff_mprotect_available(void) {
  char probe[100];
  memset(probe, 0, sizeof(probe));
  return sceKernelMprotect(probe, sizeof(probe), 0x7) == 0;
}
