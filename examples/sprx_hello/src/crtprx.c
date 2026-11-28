/* Copyright (C) 2026 OnionHEN Contributors
 * Minimal PS5 PRX runtime entry and PT_SCE_MODULE_PARAM metadata.
 */

#include <stddef.h>
#include <stdint.h>

typedef void (*init_fini_fn)(void);

extern init_fini_fn __init_array_start[] __attribute__((visibility("hidden")));
extern init_fini_fn __init_array_end[] __attribute__((visibility("hidden")));
extern init_fini_fn __fini_array_start[] __attribute__((visibility("hidden")));
extern init_fini_fn __fini_array_end[] __attribute__((visibility("hidden")));

extern int module_start(size_t argc, const void *argp)
    __attribute__((visibility("hidden")));
extern int module_stop(size_t argc, const void *argp)
    __attribute__((visibility("hidden")));

struct SceModuleParamPs5 {
  uint64_t size;
  uint32_t magic;
  uint32_t entry_count;
  uint32_t sdk_ver;
  uint32_t ppr_sdk;
  uint32_t unk;
  uint32_t reserved;
};

/*
 * PS5 PT_SCE_MODULE_PARAM (0x61000002).
 * Default values match PS5 SDK 7.00 (0x07000038 / 0x10590001); elf2ps5prx.py
 * can override these at package time via --sdk-ver / --ppr-sdk / --sdk-unk.
 */
__attribute__((used, section(".data.sce_module_param"), aligned(8)))
const struct SceModuleParamPs5 _sceModuleParam = {
    .size = sizeof(struct SceModuleParamPs5),
    .magic = 0x3C13F4BFu,
    .entry_count = 3u,
    .sdk_ver = 0x10590001u,
    .ppr_sdk = 0x07000038u,
    .unk = 0x00000411u,
    .reserved = 0u,
};

/*
 * DT_INIT entry invoked by PS5 sceKernelLoadStartModule().
 * Runs static constructors and delegates to module_start(argc, argp).
 */
__attribute__((visibility("hidden")))
int _init(size_t argc, const void *argp) {
  for (init_fini_fn *fn = __init_array_start; fn < __init_array_end; ++fn) {
    if (*fn)
      (*fn)();
  }
  return module_start(argc, argp);
}

/*
 * DT_FINI entry invoked when the module is stopped/unloaded.
 * Delegates to module_stop(argc, argp) and runs destructors in reverse order.
 */
__attribute__((visibility("hidden")))
int _fini(size_t argc, const void *argp) {
  int rc = module_stop(argc, argp);
  for (init_fini_fn *fn = __fini_array_end; fn > __fini_array_start; --fn) {
    if (fn[-1])
      fn[-1]();
  }
  return rc;
}
