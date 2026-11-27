#include "test_harness.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cheats/cheat_applier.hpp"
#include "cheats/cheat_code_cave.hpp"
#include "cheats/cheat_engine.h"
#include "cheats/i_cheat_runtime.hpp"

namespace onion::cheats {

// Host test fallback definition so cheat_applier.cpp links without PS5 SDK.
namespace {
class NullCheatRuntime final : public ICheatRuntime {
public:
  uint32_t firmwareMajor() const override { return 0; }
  std::unique_ptr<IMemoryBackend> createBackend(uint32_t) const override {
    return nullptr;
  }
  int findModule(pid_t, const char *, util_module_info_t *) const override {
    return -1;
  }
  int findModuleInApp(int32_t, const char *, pid_t *,
                      util_module_info_t *) const override {
    return -1;
  }
};
} // namespace

const ICheatRuntime &defaultCheatRuntime() {
  static const NullCheatRuntime kNull;
  return kNull;
}

} // namespace onion::cheats

namespace {

using namespace onion::cheats;

struct FakeMemorySpace {
  struct Page {
    std::vector<uint8_t> data =
        std::vector<uint8_t>(static_cast<size_t>(kCodeCavePageSize), 0);
    bool present = true;
    bool writable = true;
  };

  std::map<std::pair<pid_t, uint64_t>, Page> pages;
  int map_calls = 0;
  std::vector<uint64_t> mapped_addrs;
  bool fail_next_map = false;
  bool map_leaves_unfaulted = false;

  static uint64_t alignPage(uint64_t addr) {
    return addr & ~(kCodeCavePageSize - 1ULL);
  }

  void mapRange(pid_t pid, uint64_t addr, const std::vector<uint8_t> &bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
      const uint64_t a = addr + i;
      const uint64_t pg = alignPage(a);
      Page &p = pages[{pid, pg}];
      p.present = true;
      p.writable = true;
      p.data[static_cast<size_t>(a - pg)] = bytes[i];
    }
  }

  int read(pid_t pid, uint64_t addr, void *buf, size_t len) const {
    auto *out = static_cast<uint8_t *>(buf);
    for (size_t i = 0; i < len; ++i) {
      const uint64_t a = addr + i;
      const uint64_t pg = alignPage(a);
      const auto it = pages.find({pid, pg});
      if (it == pages.end() || !it->second.present) {
        return -1;
      }
      out[i] = it->second.data[static_cast<size_t>(a - pg)];
    }
    return 0;
  }

  int write(pid_t pid, uint64_t addr, const void *buf, size_t len) {
    const auto *in = static_cast<const uint8_t *>(buf);
    for (size_t i = 0; i < len; ++i) {
      const uint64_t a = addr + i;
      const uint64_t pg = alignPage(a);
      const auto it = pages.find({pid, pg});
      if (it == pages.end() || !it->second.present || !it->second.writable) {
        return -1;
      }
    }
    for (size_t i = 0; i < len; ++i) {
      const uint64_t a = addr + i;
      const uint64_t pg = alignPage(a);
      pages[{pid, pg}].data[static_cast<size_t>(a - pg)] = in[i];
    }
    return 0;
  }

  int mapCodeCave(pid_t pid, uint64_t addr, size_t len) {
    ++map_calls;
    mapped_addrs.push_back(addr);
    if (fail_next_map) {
      fail_next_map = false;
      return -1;
    }
    CodeCavePageRange range{};
    if (!computeCodeCavePageRange(addr, len, range)) {
      return -1;
    }
    for (uint64_t pg = range.start; pg < range.start + range.length;
         pg += kCodeCavePageSize) {
      Page &p = pages[{pid, pg}];
      p.data.assign(static_cast<size_t>(kCodeCavePageSize), 0);
      p.present = !map_leaves_unfaulted;
      p.writable = true;
    }
    return 0;
  }
};

class FakeMemoryBackend final : public IMemoryBackend {
public:
  explicit FakeMemoryBackend(FakeMemorySpace *space) : space_(space) {}

  int read(pid_t pid, uint64_t addr, void *buf, size_t len) override {
    return space_->read(pid, addr, buf, len);
  }
  int write(pid_t pid, uint64_t addr, const void *buf, size_t len) override {
    return space_->write(pid, addr, buf, len);
  }
  int mapCodeCave(pid_t pid, uint64_t addr, size_t len) override {
    return space_->mapCodeCave(pid, addr, len);
  }

private:
  FakeMemorySpace *space_;
};

class FakeCheatRuntime final : public ICheatRuntime {
public:
  FakeMemorySpace memory;
  uint32_t fw = 0x1001;
  std::map<std::pair<pid_t, std::string>, util_module_info_t> modules;

  void addModule(pid_t pid, const char *name, uint64_t base, uint64_t size) {
    util_module_info_t info{};
    info.sections[0].vaddr = base;
    info.sections[0].size = size;
    info.sections[0].prot = 5;
    modules[{pid, name}] = info;
  }

  uint32_t firmwareMajor() const override { return fw; }

  std::unique_ptr<IMemoryBackend> createBackend(uint32_t) const override {
    return std::make_unique<FakeMemoryBackend>(
        const_cast<FakeMemorySpace *>(&memory));
  }

  int findModule(pid_t pid, const char *name,
                 util_module_info_t *out) const override {
    if (name == nullptr || out == nullptr) {
      return -1;
    }
    const auto it = modules.find({pid, name});
    if (it == modules.end()) {
      return -1;
    }
    *out = it->second;
    return 0;
  }

  int findModuleInApp(int32_t, const char *, pid_t *,
                      util_module_info_t *) const override {
    return -1;
  }
};

void initPatch(onion_patch_t &patch, uint64_t offset,
               const std::vector<uint8_t> &on,
               const std::vector<uint8_t> &off) {
  std::memset(&patch, 0, sizeof(patch));
  patch.kind = ONION_PATCH_DIRECT;
  patch.offset = offset;
  patch.on_len = on.size();
  patch.off_len = off.size();
  if (!on.empty()) {
    std::memcpy(patch.on, on.data(), on.size());
  }
  if (!off.empty()) {
    std::memcpy(patch.off, off.data(), off.size());
  }
}

void addCheat(onion_cheat_file_t &file, const char *name, const char *module,
              const std::vector<onion_patch_t> &patches) {
  onion_cheat_file_ensure_cheat(&file);
  auto &entry = file.cheats[file.cheat_count++];
  std::memset(&entry, 0, sizeof(entry));
  std::snprintf(entry.name, sizeof(entry.name), "%s", name);
  std::snprintf(entry.module_name, sizeof(entry.module_name), "%s", module);
  for (const auto &p : patches) {
    onion_cheat_entry_ensure_patch(&entry);
    entry.patches[entry.patch_count++] = p;
  }
}

game_context_t makeGame(pid_t pid, const char *process = "eboot.bin") {
  game_context_t game{};
  game.pid = pid;
  game.appid = 1;
  std::snprintf(game.title_id, sizeof(game.title_id), "PPSA04264");
  std::snprintf(game.version, sizeof(game.version), "01.005.000");
  std::snprintf(game.process_name, sizeof(game.process_name), "%s", process);
  return game;
}

/* --- CodeCaveMapOps unit tests --- */

struct MockCaveOpsState {
  int attach_calls = 0;
  int detach_calls = 0;
  int mmap_calls = 0;
  int mprotect_calls = 0;
  int munmap_calls = 0;
  int fail_attach = 0;
  intptr_t mmap_override = 0;
  bool use_mmap_override = false;
  int fail_copyin_at = -1;
  int fail_mprotect = 0;
  std::vector<std::string> sequence;
  std::vector<uint64_t> faulted_addrs;
  std::vector<uint64_t> unmapped_addrs;
};

int mockAttach(pid_t, void *ctx) {
  auto *s = static_cast<MockCaveOpsState *>(ctx);
  ++s->attach_calls;
  s->sequence.push_back("attach");
  return s->fail_attach ? -1 : 0;
}

intptr_t mockMmap(pid_t, intptr_t addr, size_t, void *ctx) {
  auto *s = static_cast<MockCaveOpsState *>(ctx);
  ++s->mmap_calls;
  s->sequence.push_back("mmap");
  return s->use_mmap_override ? s->mmap_override : addr;
}

int mockCopyin(pid_t, const void *buf, intptr_t addr, size_t len, void *ctx) {
  auto *s = static_cast<MockCaveOpsState *>(ctx);
  const int call_index = static_cast<int>(s->faulted_addrs.size());
  s->sequence.push_back("copyin");
  if (len != 1 || *static_cast<const uint8_t *>(buf) != 0) {
    return -1;
  }
  s->faulted_addrs.push_back(static_cast<uint64_t>(addr));
  if (s->fail_copyin_at == call_index) {
    return -1;
  }
  return 0;
}

int mockMprotect(pid_t, uint64_t, size_t, void *ctx) {
  auto *s = static_cast<MockCaveOpsState *>(ctx);
  ++s->mprotect_calls;
  s->sequence.push_back("mprotect");
  return s->fail_mprotect ? -1 : 0;
}

int mockMunmap(pid_t, intptr_t addr, size_t, void *ctx) {
  auto *s = static_cast<MockCaveOpsState *>(ctx);
  ++s->munmap_calls;
  s->sequence.push_back("munmap");
  s->unmapped_addrs.push_back(static_cast<uint64_t>(addr));
  return 0;
}

int mockDetach(pid_t, void *ctx) {
  auto *s = static_cast<MockCaveOpsState *>(ctx);
  ++s->detach_calls;
  s->sequence.push_back("detach");
  return 0;
}

CodeCaveMapOps makeMockOps(MockCaveOpsState &state) {
  CodeCaveMapOps ops{};
  ops.attach = mockAttach;
  ops.mmap = mockMmap;
  ops.copyin = mockCopyin;
  ops.mprotect_rwx = mockMprotect;
  ops.munmap = mockMunmap;
  ops.detach = mockDetach;
  ops.ctx = &state;
  return ops;
}

int test_code_cave_page_range_validation() {
  CodeCavePageRange r{};
  TEST_ASSERT_TRUE(computeCodeCavePageRange(0x405A69EC0ULL, 20, r));
  TEST_ASSERT_EQ_U64(0x405A68000ULL, r.start);
  TEST_ASSERT_EQ_U64(0x4000ULL, r.length);

  // Straddling a 16 KiB boundary spans two 16 KiB pages.
  TEST_ASSERT_TRUE(computeCodeCavePageRange(0x405A6BFF8ULL, 16, r));
  TEST_ASSERT_EQ_U64(0x405A68000ULL, r.start);
  TEST_ASSERT_EQ_U64(0x8000ULL, r.length);

  // Invalid zero length and overflow cases.
  TEST_ASSERT_TRUE(!computeCodeCavePageRange(0x405A69EC0ULL, 0, r));
  TEST_ASSERT_TRUE(
      !computeCodeCavePageRange(UINT64_MAX - 8ULL, 16, r));
  TEST_ASSERT_TRUE(
      !computeCodeCavePageRange(UINT64_MAX - 0x100ULL, 16, r));
  return 0;
}

int test_code_cave_mapper_faults_every_4kb_pte_before_rwx() {
  MockCaveOpsState state;
  const CodeCaveMapOps ops = makeMockOps(state);
  uint64_t page = 0;

  // Single 16 KiB page -> 4 x 4 KiB PTE fault-ins before mprotect.
  const CodeCaveMapStatus rc =
      mapCodeCaveWithOps(100, 0x405A69EC0ULL, 20, ops, &page);
  TEST_ASSERT_EQ_INT(static_cast<int>(CodeCaveMapStatus::Ok),
                     static_cast<int>(rc));
  TEST_ASSERT_EQ_U64(0x405A68000ULL, page);
  TEST_ASSERT_EQ_INT(1, state.attach_calls);
  TEST_ASSERT_EQ_INT(1, state.mmap_calls);
  TEST_ASSERT_EQ_INT(1, state.mprotect_calls);
  TEST_ASSERT_EQ_INT(0, state.munmap_calls);
  TEST_ASSERT_EQ_INT(1, state.detach_calls);
  TEST_ASSERT_EQ_INT(4, static_cast<int>(state.faulted_addrs.size()));
  TEST_ASSERT_EQ_U64(0x405A68000ULL, state.faulted_addrs[0]);
  TEST_ASSERT_EQ_U64(0x405A69000ULL, state.faulted_addrs[1]);
  TEST_ASSERT_EQ_U64(0x405A6A000ULL, state.faulted_addrs[2]);
  TEST_ASSERT_EQ_U64(0x405A6B000ULL, state.faulted_addrs[3]);
  TEST_ASSERT_STREQ("attach", state.sequence[0].c_str());
  TEST_ASSERT_STREQ("mmap", state.sequence[1].c_str());
  TEST_ASSERT_STREQ("copyin", state.sequence[2].c_str());
  TEST_ASSERT_STREQ("copyin", state.sequence[5].c_str());
  TEST_ASSERT_STREQ("mprotect", state.sequence[6].c_str());
  TEST_ASSERT_STREQ("detach", state.sequence[7].c_str());

  // Two 16 KiB pages -> 8 x 4 KiB PTE fault-ins.
  MockCaveOpsState span;
  const CodeCaveMapOps span_ops = makeMockOps(span);
  TEST_ASSERT_EQ_INT(
      static_cast<int>(CodeCaveMapStatus::Ok),
      static_cast<int>(mapCodeCaveWithOps(100, 0x405A6BFF8ULL, 16, span_ops)));
  TEST_ASSERT_EQ_INT(8, static_cast<int>(span.faulted_addrs.size()));
  TEST_ASSERT_EQ_U64(0x405A6F000ULL, span.faulted_addrs[7]);
  return 0;
}

int test_code_cave_mapper_cleans_up_on_errors() {
  // 1. mmap returns a different positive address -> must munmap the stray VMA.
  {
    MockCaveOpsState state;
    state.use_mmap_override = true;
    state.mmap_override = static_cast<intptr_t>(0x700000000ULL);
    const CodeCaveMapOps ops = makeMockOps(state);
    TEST_ASSERT_EQ_INT(
        static_cast<int>(CodeCaveMapStatus::MmapFailed),
        static_cast<int>(mapCodeCaveWithOps(100, 0x405A69EC0ULL, 20, ops)));
    TEST_ASSERT_EQ_INT(1, state.munmap_calls);
    TEST_ASSERT_EQ_U64(0x700000000ULL, state.unmapped_addrs[0]);
    TEST_ASSERT_EQ_INT(1, state.detach_calls);
  }
  // 2. mmap returns -1 -> must NOT munmap, must still detach.
  {
    MockCaveOpsState state;
    state.use_mmap_override = true;
    state.mmap_override = -1;
    const CodeCaveMapOps ops = makeMockOps(state);
    TEST_ASSERT_EQ_INT(
        static_cast<int>(CodeCaveMapStatus::MmapFailed),
        static_cast<int>(mapCodeCaveWithOps(100, 0x405A69EC0ULL, 20, ops)));
    TEST_ASSERT_EQ_INT(0, state.munmap_calls);
    TEST_ASSERT_EQ_INT(1, state.detach_calls);
  }
  // 3. copyin fault-in fails -> must munmap and detach.
  {
    MockCaveOpsState state;
    state.fail_copyin_at = 2;
    const CodeCaveMapOps ops = makeMockOps(state);
    TEST_ASSERT_EQ_INT(
        static_cast<int>(CodeCaveMapStatus::FaultInFailed),
        static_cast<int>(mapCodeCaveWithOps(100, 0x405A69EC0ULL, 20, ops)));
    TEST_ASSERT_EQ_INT(1, state.munmap_calls);
    TEST_ASSERT_EQ_U64(0x405A68000ULL, state.unmapped_addrs[0]);
    TEST_ASSERT_EQ_INT(0, state.mprotect_calls);
    TEST_ASSERT_EQ_INT(1, state.detach_calls);
  }
  // 4. mprotect fails -> must munmap and detach.
  {
    MockCaveOpsState state;
    state.fail_mprotect = 1;
    const CodeCaveMapOps ops = makeMockOps(state);
    TEST_ASSERT_EQ_INT(
        static_cast<int>(CodeCaveMapStatus::MprotectFailed),
        static_cast<int>(mapCodeCaveWithOps(100, 0x405A69EC0ULL, 20, ops)));
    TEST_ASSERT_EQ_INT(1, state.munmap_calls);
    TEST_ASSERT_EQ_INT(1, state.detach_calls);
  }
  // 5. attach fails -> must NOT detach.
  {
    MockCaveOpsState state;
    state.fail_attach = 1;
    const CodeCaveMapOps ops = makeMockOps(state);
    TEST_ASSERT_EQ_INT(
        static_cast<int>(CodeCaveMapStatus::AttachFailed),
        static_cast<int>(mapCodeCaveWithOps(100, 0x405A69EC0ULL, 20, ops)));
    TEST_ASSERT_EQ_INT(0, state.detach_calls);
  }
  return 0;
}

/* --- CheatApplier end-to-end tests --- */

int test_applier_maps_unmapped_code_cave_gta_v_maze_bank() {
  // Exact layout from issue #82 (PPSA04264_01.005.000_33feea8f.mc4):
  // Patch 0: code cave at unmapped base + 0x5A69EC0 (20 zero OFF bytes).
  // Patch 1: jump hook at mapped base + 0x2D2E641.
  FakeCheatRuntime runtime;
  constexpr uint64_t kBase = 0x400000000ULL;
  runtime.addModule(42, "eboot.bin", kBase, 0x3100000ULL);
  const std::vector<uint8_t> hook_orig = {0x48, 0x89, 0x34, 0xC8,
                                          0x41, 0x0F, 0xB6, 0x07};
  const std::vector<uint8_t> hook_on = {0xE9, 0x7A, 0xB8, 0xD3,
                                        0x02, 0x90, 0x90, 0x90};
  const std::vector<uint8_t> cave_on = {
      0x48, 0xC7, 0xC6, 0x40, 0x54, 0x89, 0x00, 0x48, 0x89, 0x34,
      0xC8, 0x41, 0x0F, 0xB6, 0x07, 0xE9, 0x75, 0x47, 0x2C, 0xFD};
  const std::vector<uint8_t> cave_off(cave_on.size(), 0x00);
  runtime.memory.mapRange(42, kBase + 0x2D2E641ULL, hook_orig);

  onion_cheat_file_t file{};
  file.master_code_id = -1;
  onion_patch_t p0{};
  onion_patch_t p1{};
  initPatch(p0, 0x5A69EC0ULL, cave_on, cave_off);
  initPatch(p1, 0x2D2E641ULL, hook_on, hook_orig);
  addCheat(file, "Lot Of Money (Go To MazeBank)", "eboot.bin", {p0, p1});

  CheatApplier applier(&runtime);
  const game_context_t game = makeGame(42);
  std::string status;

  // Enable: lazily maps the unmapped cave, writes both patches, verifies.
  TEST_ASSERT_EQ_INT(0, applier.toggle(game, file, 0, status, "gta.mc4"));
  TEST_ASSERT_TRUE(file.cheats[0].enabled);
  TEST_ASSERT_TRUE(file.cheats[0].patches[0].code_cave_reloc);
  TEST_ASSERT_EQ_INT(1, runtime.memory.map_calls);
  TEST_ASSERT_EQ_U64(kBase + 0x5A69EC0ULL, runtime.memory.mapped_addrs[0]);

  std::vector<uint8_t> live_cave(cave_on.size());
  std::vector<uint8_t> live_hook(hook_on.size());
  TEST_ASSERT_EQ_INT(
      0, runtime.memory.read(42, kBase + 0x5A69EC0ULL, live_cave.data(),
                             live_cave.size()));
  TEST_ASSERT_EQ_INT(
      0, runtime.memory.read(42, kBase + 0x2D2E641ULL, live_hook.data(),
                             live_hook.size()));
  TEST_ASSERT_TRUE(live_cave == cave_on);
  TEST_ASSERT_TRUE(live_hook == hook_on);

  // Disable: restores zeros in cave and original bytes at hook without remapping.
  TEST_ASSERT_EQ_INT(0, applier.toggle(game, file, 0, status, "gta.mc4"));
  TEST_ASSERT_TRUE(!file.cheats[0].enabled);
  TEST_ASSERT_EQ_INT(1, runtime.memory.map_calls);
  TEST_ASSERT_EQ_INT(
      0, runtime.memory.read(42, kBase + 0x5A69EC0ULL, live_cave.data(),
                             live_cave.size()));
  TEST_ASSERT_EQ_INT(
      0, runtime.memory.read(42, kBase + 0x2D2E641ULL, live_hook.data(),
                             live_hook.size()));
  TEST_ASSERT_TRUE(live_cave == cave_off);
  TEST_ASSERT_TRUE(live_hook == hook_orig);

  // Re-enable: page is already mapped, no extra mapCodeCave call.
  TEST_ASSERT_EQ_INT(0, applier.toggle(game, file, 0, status, "gta.mc4"));
  TEST_ASSERT_TRUE(file.cheats[0].enabled);
  TEST_ASSERT_EQ_INT(1, runtime.memory.map_calls);

  onion_cheat_file_clear(&file);
  return 0;
}

int test_applier_reuses_mapped_page_across_adjacent_cave_segments() {
  FakeCheatRuntime runtime;
  constexpr uint64_t kBase = 0x400000000ULL;
  runtime.addModule(42, "Il2CppUserAssemblies.prx", kBase, 0x4700000ULL);
  const std::vector<uint8_t> hook_orig = {0x55, 0x48, 0x89, 0xE5};
  const std::vector<uint8_t> hook_on = {0x90, 0x90, 0x90, 0x90};
  runtime.memory.mapRange(42, kBase + 0x1000ULL, hook_orig);

  onion_cheat_file_t file{};
  file.master_code_id = -1;
  onion_patch_t cave_a{};
  onion_patch_t cave_b{};
  onion_patch_t hook{};
  initPatch(cave_a, 0x4792000ULL, {0x57, 0x56, 0x52, 0xC3}, {0, 0, 0, 0});
  initPatch(cave_b, 0x4792078ULL, {0x48, 0x89, 0xE5, 0xC3}, {0, 0, 0, 0});
  initPatch(hook, 0x1000ULL, hook_on, hook_orig);
  addCheat(file, "Multi-chunk cave", "Il2CppUserAssemblies.prx",
           {cave_a, cave_b, hook});

  CheatApplier applier(&runtime);
  std::string status;
  TEST_ASSERT_EQ_INT(0, applier.toggle(makeGame(42), file, 0, status, "g.json"));
  TEST_ASSERT_EQ_INT(1, runtime.memory.map_calls);
  TEST_ASSERT_TRUE(file.cheats[0].patches[0].code_cave_reloc);
  TEST_ASSERT_TRUE(file.cheats[0].patches[1].code_cave_reloc);

  onion_cheat_file_clear(&file);
  return 0;
}

int test_applier_refuses_cave_map_when_regular_patch_unreadable() {
  FakeCheatRuntime runtime;
  constexpr uint64_t kBase = 0x400000000ULL;
  runtime.addModule(42, "eboot.bin", kBase, 0x100000ULL);
  // Note: neither the cave (0x5A69EC0) nor the regular patch (0x2D2E641) is
  // mapped (e.g. wrong game build / wrong module).

  onion_cheat_file_t file{};
  file.master_code_id = -1;
  onion_patch_t cave{};
  onion_patch_t regular{};
  initPatch(cave, 0x5A69EC0ULL, {0x90, 0x90, 0x90, 0xC3}, {0, 0, 0, 0});
  initPatch(regular, 0x2D2E641ULL, {0x90, 0x90}, {0x48, 0x89});
  addCheat(file, "Wrong build cheat", "eboot.bin", {cave, regular});

  CheatApplier applier(&runtime);
  std::string status;
  TEST_ASSERT_EQ_INT(-1, applier.toggle(makeGame(42), file, 0, status, "b.mc4"));
  TEST_ASSERT_EQ_INT(0, runtime.memory.map_calls);
  TEST_ASSERT_TRUE(!file.cheats[0].enabled);

  onion_cheat_file_clear(&file);
  return 0;
}

int test_applier_rejects_non_zero_off_or_restore_original_cave_map() {
  FakeCheatRuntime runtime;
  constexpr uint64_t kBase = 0x400000000ULL;
  runtime.addModule(42, "eboot.bin", kBase, 0x100000ULL);

  onion_cheat_file_t file{};
  file.master_code_id = -1;
  onion_patch_t non_zero{};
  initPatch(non_zero, 0x5A69EC0ULL, {0x90, 0x90}, {0x75, 0x10});
  addCheat(file, "Non-zero off", "eboot.bin", {non_zero});

  onion_patch_t restore_orig{};
  initPatch(restore_orig, 0x5A69ED0ULL, {0x90, 0x90}, {});
  restore_orig.restore_original = true;
  addCheat(file, "Restore original", "eboot.bin", {restore_orig});

  CheatApplier applier(&runtime);
  std::string status;
  TEST_ASSERT_EQ_INT(-1, applier.toggle(makeGame(42), file, 0, status, "t.mc4"));
  TEST_ASSERT_EQ_INT(-1, applier.toggle(makeGame(42), file, 1, status, "t.mc4"));
  TEST_ASSERT_EQ_INT(0, runtime.memory.map_calls);

  onion_cheat_file_clear(&file);
  return 0;
}

int test_applier_transient_map_failure_allows_retry_and_pid_reset() {
  FakeCheatRuntime runtime;
  constexpr uint64_t kBase = 0x400000000ULL;
  runtime.addModule(42, "eboot.bin", kBase, 0x100000ULL);
  runtime.addModule(99, "eboot.bin", kBase, 0x100000ULL);

  onion_cheat_file_t file{};
  file.master_code_id = -1;
  onion_patch_t cave{};
  initPatch(cave, 0x5A69EC0ULL, {0x48, 0x31, 0xC0, 0xC3}, {0, 0, 0, 0});
  addCheat(file, "Cave retry", "eboot.bin", {cave});

  CheatApplier applier(&runtime);
  std::string status;

  // Transient mapCodeCave failure does not permanently set code_cave_reloc.
  runtime.memory.fail_next_map = true;
  TEST_ASSERT_EQ_INT(-1, applier.toggle(makeGame(42), file, 0, status, "r.mc4"));
  TEST_ASSERT_TRUE(!file.cheats[0].patches[0].code_cave_reloc);
  TEST_ASSERT_EQ_INT(1, runtime.memory.map_calls);

  // Subsequent attempt succeeds.
  TEST_ASSERT_EQ_INT(0, applier.toggle(makeGame(42), file, 0, status, "r.mc4"));
  TEST_ASSERT_TRUE(file.cheats[0].enabled);
  TEST_ASSERT_TRUE(file.cheats[0].patches[0].code_cave_reloc);
  TEST_ASSERT_EQ_INT(2, runtime.memory.map_calls);

  // Relaunching the game with a new PID resets code_cave_reloc so the cave
  // maps cleanly in the new address space.
  TEST_ASSERT_EQ_INT(0, applier.toggle(makeGame(99), file, 0, status, "r.mc4"));
  TEST_ASSERT_TRUE(file.cheats[0].enabled);
  TEST_ASSERT_TRUE(file.cheats[0].patches[0].code_cave_reloc);
  TEST_ASSERT_EQ_INT(3, runtime.memory.map_calls);

  onion_cheat_file_clear(&file);
  return 0;
}

int test_applier_fails_if_mapped_cave_remains_unreadable() {
  FakeCheatRuntime runtime;
  constexpr uint64_t kBase = 0x400000000ULL;
  runtime.addModule(42, "eboot.bin", kBase, 0x100000ULL);
  runtime.memory.map_leaves_unfaulted = true;

  onion_cheat_file_t file{};
  file.master_code_id = -1;
  onion_patch_t cave{};
  initPatch(cave, 0x5A69EC0ULL, {0x48, 0x31, 0xC0, 0xC3}, {0, 0, 0, 0});
  addCheat(file, "Unfaulted cave", "eboot.bin", {cave});

  CheatApplier applier(&runtime);
  std::string status;
  TEST_ASSERT_EQ_INT(-1, applier.toggle(makeGame(42), file, 0, status, "u.mc4"));
  TEST_ASSERT_EQ_INT(1, runtime.memory.map_calls);
  TEST_ASSERT_TRUE(!file.cheats[0].patches[0].code_cave_reloc);
  TEST_ASSERT_TRUE(!file.cheats[0].enabled);

  onion_cheat_file_clear(&file);
  return 0;
}

} // namespace

extern "C" int test_cheat_applier_suite(void) {
  int failures = 0;
  failures += onion_test_run("code cave page range validation",
                             test_code_cave_page_range_validation);
  failures += onion_test_run("code cave mapper faults every 4kb pte before rwx",
                             test_code_cave_mapper_faults_every_4kb_pte_before_rwx);
  failures += onion_test_run("code cave mapper cleans up on errors",
                             test_code_cave_mapper_cleans_up_on_errors);
  failures += onion_test_run("applier maps unmapped code cave (GTA V #82)",
                             test_applier_maps_unmapped_code_cave_gta_v_maze_bank);
  failures += onion_test_run("applier reuses mapped page across adjacent caves",
                             test_applier_reuses_mapped_page_across_adjacent_cave_segments);
  failures += onion_test_run("applier refuses cave map when regular patch unreadable",
                             test_applier_refuses_cave_map_when_regular_patch_unreadable);
  failures += onion_test_run("applier rejects non-zero off or restore_original cave map",
                             test_applier_rejects_non_zero_off_or_restore_original_cave_map);
  failures += onion_test_run("applier transient map failure retry and pid reset",
                             test_applier_transient_map_failure_allows_retry_and_pid_reset);
  failures += onion_test_run("applier fails if mapped cave remains unreadable",
                             test_applier_fails_if_mapped_cave_remains_unreadable);
  return failures;
}
