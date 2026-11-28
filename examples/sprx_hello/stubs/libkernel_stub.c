/* Copyright (C) 2026 OnionHEN Contributors
 * Minimal link-time stub for libkernel.prx so ld.lld records DT_NEEDED
 * and PLT/GOT imports without requiring proprietary SDK libraries.
 */

#include <stddef.h>
#include <stdint.h>

#define STUB(name) void name(void) {}

STUB(getpid)
STUB(sceKernelSendNotificationRequest)
STUB(sceKernelDebugOutText)
STUB(sceKernelUsleep)
STUB(sceKernelGetProcessTime)
STUB(sceKernelOpen)
STUB(sceKernelWrite)
STUB(sceKernelClose)
STUB(scePthreadCreate)
STUB(scePthreadJoin)
