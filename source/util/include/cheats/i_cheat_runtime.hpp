#pragma once

#include <cstdint>
#include <memory>
#include <sys/types.h>

#include "cheats/i_memory_backend.hpp"
#include "util_platform.h"

namespace onion::cheats {

/**
 * Runtime seam for process/module lookup and memory backend creation.
 * Production PS5 builds bind defaultCheatRuntime(); host tests inject a fake.
 */
class ICheatRuntime {
public:
  virtual ~ICheatRuntime() = default;

  virtual uint32_t firmwareMajor() const = 0;
  virtual std::unique_ptr<IMemoryBackend> createBackend(
      uint32_t fw_major) const = 0;
  virtual int findModule(pid_t pid, const char *name,
                         util_module_info_t *out) const = 0;
  virtual int findModuleInApp(int32_t appid, const char *name, pid_t *pid_out,
                              util_module_info_t *out) const = 0;
};

/** Default PS5 runtime (defined in memory_backends.cpp). */
const ICheatRuntime &defaultCheatRuntime();

} // namespace onion::cheats
