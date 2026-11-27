#include <onion/log.h>
#include <onion/proc_dmap.h>
#include "cheats/cheat_code_cave.hpp"
#include "cheats/i_cheat_runtime.hpp"
#include "cheats/i_memory_backend.hpp"

#include <cerrno>
#include <cstring>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include <ps5/mdbg.h>

#include "pt.h"
#include "util_platform.h"


namespace onion::cheats {
namespace {

int ps5Attach(pid_t pid, void *) { return pt_attach_proc(pid); }

intptr_t ps5Mmap(pid_t pid, intptr_t addr, size_t len, void *) {
  return pt_mmap(pid, addr, len, PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
}

int ps5Copyin(pid_t pid, const void *buf, intptr_t addr, size_t len, void *) {
  return pt_copyin(pid, buf, addr, len);
}

int ps5MprotectRwx(pid_t pid, uint64_t addr, size_t len, void *) {
  return kernel_mprotect(pid, addr, len, PROT_READ | PROT_WRITE | PROT_EXEC);
}

int ps5Munmap(pid_t pid, intptr_t addr, size_t len, void *) {
  return pt_munmap(pid, addr, len);
}

int ps5Detach(pid_t pid, void *) { return pt_detach_proc(pid, 0); }

int mapCodeCaveCommon(pid_t pid, uint64_t addr, size_t len) {
  CodeCaveMapOps ops{};
  ops.attach = ps5Attach;
  ops.mmap = ps5Mmap;
  ops.copyin = ps5Copyin;
  ops.mprotect_rwx = ps5MprotectRwx;
  ops.munmap = ps5Munmap;
  ops.detach = ps5Detach;

  uint64_t mapped_page = 0;
  intptr_t mmap_ret = 0;
  const CodeCaveMapStatus rc =
      mapCodeCaveWithOps(pid, addr, len, ops, &mapped_page, &mmap_ret);
  switch (rc) {
  case CodeCaveMapStatus::Ok:
    return 0;
  case CodeCaveMapStatus::InvalidRange:
    LOG_ERROR("[Cheat] code cave invalid range addr=0x%llx len=%zu",
              static_cast<unsigned long long>(addr), len);
    return -1;
  case CodeCaveMapStatus::AttachFailed:
    LOG_ERROR("[Cheat] code cave attach failed pid=%d errno=%d", pid, errno);
    return -1;
  case CodeCaveMapStatus::MmapFailed:
    LOG_ERROR("[Cheat] code cave mmap failed page=0x%llx ret=0x%llx",
              static_cast<unsigned long long>(mapped_page),
              static_cast<unsigned long long>(mmap_ret));
    return -1;
  case CodeCaveMapStatus::FaultInFailed:
    LOG_ERROR("[Cheat] code cave fault-in failed page=0x%llx errno=%d",
              static_cast<unsigned long long>(mapped_page), errno);
    return -1;
  case CodeCaveMapStatus::MprotectFailed:
    LOG_ERROR("[Cheat] code cave mprotect failed page=0x%llx",
              static_cast<unsigned long long>(mapped_page));
    return -1;
  }
  return -1;
}

class MdbgBackend final : public IMemoryBackend {
public:
  int read(pid_t pid, uint64_t addr, void *buf, size_t len) override {
    return mdbg_copyout(pid, static_cast<intptr_t>(addr), buf, len);
  }
  int write(pid_t pid, uint64_t addr, const void *buf, size_t len) override {
    return mdbg_copyin(pid, buf, static_cast<intptr_t>(addr), len);
  }
  int mapCodeCave(pid_t pid, uint64_t addr, size_t len) override {
    return mapCodeCaveCommon(pid, addr, len);
  }
};

class KdirectBackend final : public IMemoryBackend {
public:
  int read(pid_t pid, uint64_t addr, void *buf, size_t len) override {
    return onion_proc_copyout(pid, addr, buf, len);
  }
  int write(pid_t pid, uint64_t addr, const void *buf, size_t len) override {
    return onion_proc_copyin(pid, addr, buf, len);
  }
  int mapCodeCave(pid_t pid, uint64_t addr, size_t len) override {
    return mapCodeCaveCommon(pid, addr, len);
  }
};

class Ps5CheatRuntime final : public ICheatRuntime {
public:
  uint32_t firmwareMajor() const override { return util_system_fw_major(); }
  std::unique_ptr<IMemoryBackend> createBackend(
      uint32_t fw_major) const override {
    return MemoryBackendFactory::create(fw_major);
  }
  int findModule(pid_t pid, const char *name,
                 util_module_info_t *out) const override {
    return util_find_module(pid, name, out);
  }
  int findModuleInApp(int32_t appid, const char *name, pid_t *pid_out,
                      util_module_info_t *out) const override {
    return util_find_module_in_app(appid, name, pid_out, out);
  }
};

} // namespace

std::unique_ptr<IMemoryBackend> MemoryBackendFactory::create(uint32_t fw_major) {
  if (fw_major >= 0x840) {
    return std::make_unique<KdirectBackend>();
  }
  return std::make_unique<MdbgBackend>();
}

const ICheatRuntime &defaultCheatRuntime() {
  static const Ps5CheatRuntime kRuntime;
  return kRuntime;
}

} // namespace onion::cheats
