/* kstuff presence probe for the standalone FPS payload.
 *
 * The daemon waits for the bootstrapper's "kstuff" ready marker; this payload
 * runs without a bootstrapper, so it probes the capability itself: with the
 * kstuff patches active, userland mprotect(RWX) succeeds (same test as
 * source/bootstrapper/source/kstuff_probe.c). */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

int sceKernelMprotect(void *addr, size_t len, int prot);

bool fps_kstuff_active(void) {
  char probe[100];
  memset(probe, 0, sizeof(probe));
  return sceKernelMprotect(probe, sizeof(probe), 0x7) == 0;
}
