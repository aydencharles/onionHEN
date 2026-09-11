/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Remote flip detour for PS4 backwards-compatibility titles. The daemon
 * resolves sceGnmSubmitAndFlipCommandBuffers inside the running BC process
 * with libhijacker and patches a short trampoline that bumps a frame counter
 * before jumping back. Everything is written through the kernel DMAP window
 * (onion_proc_copyin) because mdbg writes are unreliable on BC processes.
 */
#include <onion/fps_bc.hpp>

#include <onion/hde64.h>
#include <onion/log.h>
#include <onion/proc_dmap.h>

#include <hijacker.hpp>
#include <ps5/kernel.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace onion {
namespace fps {

namespace {

/* Smallest prologue etaHEN-style detours accept. */
constexpr uint32_t kHookMin = 14;
/* Bytes of the entry point we read to locate an instruction boundary. */
constexpr size_t kPrologueRead = 32;
constexpr size_t kCounterBytes = 8;
constexpr size_t kIncBytes = 6;  /* ff 05 disp32 */
constexpr size_t kJmpBytes = 14; /* ff 25 00000000 + qword target */

/* PS4 GNM submission entry points, in order of preference. */
constexpr const char *kNidNames[] = {
    "Ga6r7H6Y0RI", /* sceGnmSubmitAndFlipCommandBuffersForWorkload */
    "xbxNatawohc", /* sceGnmSubmitAndFlipCommandBuffers */
    "jRcI8VcgTz4", /* sceGnmSubmitCommandBuffersForWorkload */
    "zwY0YV91TTI", /* sceGnmSubmitCommandBuffers */
};

/* The BC shim annotates exports as "<nid>#F#A": an 11-char NID followed by a
 * type tag separated with '#'. Only the NID part identifies the function. */
bool nid_matches(const StringView &name, const char *nid) {
  const size_t want = __builtin_strlen(nid);
  if (name.length() < want)
    return false;
  if (__builtin_memcmp(name.c_str(), nid, want) != 0)
    return false;
  return name.length() == want || name[want] == '#';
}

void set_status(BcHookStatus *out, BcHookStatus status) {
  if (out)
    *out = status;
}

/* True when the target VA is backed by a page table entry (allocator tails
 * can land past the last mapped page of the eboot sections). */
bool va_mapped(pid_t pid, uint64_t va) {
  struct onion_proc_dmap_ctx ctx {};
  if (onion_proc_dmap_init(pid, &ctx) != 0)
    return false;
  uint64_t phys = 0;
  return onion_proc_translate(&ctx, va, &phys, nullptr) == 0;
}

bool contains_ci(const StringView &name, const char *needle) {
  const size_t len = name.length();
  const size_t want = __builtin_strlen(needle);
  if (len == 0 || want > len)
    return false;
  const char *s = name.c_str();
  for (size_t i = 0; i + want <= len; ++i) {
    size_t j = 0;
    for (; j < want; ++j) {
      char a = s[i + j];
      char b = needle[j];
      if (a >= 'A' && a <= 'Z')
        a = static_cast<char>(a + ('a' - 'A'));
      if (b >= 'A' && b <= 'Z')
        b = static_cast<char>(b + ('a' - 'A'));
      if (a != b)
        break;
    }
    if (j == want)
      return true;
  }
  return false;
}

/* Walk the runtime symbol table of one module looking for any GNM flip NID.
 * On success the NID name is copied to `found` and the VA is returned. */
uint64_t resolve_rtld(SharedLib *lib, char *found, size_t found_cap) {
  RtldMeta *meta = lib->getMetaData();
  if (!meta) {
    LOG_WARN("fps-bc: no rtld meta for module");
    return 0;
  }
  const auto &syms = meta->getSymbolTable();
  const size_t n = syms.length();
  LOG_INFO("fps-bc: rtld syms=%zu symtab=0x%llx strtab=0x%llx", n,
           static_cast<unsigned long long>(meta->symtab()),
           static_cast<unsigned long long>(meta->strtab()));
  for (const char *nid : kNidNames) {
    for (size_t i = 0; i < n; ++i) {
      const auto sym = syms[i];
      if (!sym.exported())
        continue;
      if (nid_matches(sym.name(), nid)) {
        std::snprintf(found, found_cap, "%s", nid);
        return sym.vaddr();
      }
    }
  }
  return 0;
}

void dump_symbols(SharedLib *lib) {
  RtldMeta *meta = lib->getMetaData();
  if (!meta)
    return;
  const auto &syms = meta->getSymbolTable();
  const size_t n = syms.length();
  size_t exported = 0;
  size_t logged = 0;
  constexpr size_t kMaxDump = 40;
  for (size_t i = 0; i < n; ++i) {
    const auto sym = syms[i];
    if (!sym.exported())
      continue;
    ++exported;
    const StringView name = sym.name();
    const size_t print_len = name.length() > 160 ? 160 : name.length();
    if (contains_ci(name, "submit") || contains_ci(name, "flip") ||
        contains_ci(name, "gnm") || logged < 10) {
      LOG_INFO("fps-bc: sym[%zu] %.*s vaddr=0x%llx", i,
               static_cast<int>(print_len), name.c_str(),
               static_cast<unsigned long long>(sym.vaddr()));
      ++logged;
      if (logged >= kMaxDump)
        break;
    }
  }
  LOG_INFO("fps-bc: exported symbols total=%zu (dumped %zu)", exported, logged);
}

} // namespace

struct BcGnmHook::Impl {
  UniquePtr<Hijacker> hijacker;
  UniquePtr<SharedLib> gnm;
  bool logged_dump_ = false;
};

const char *bc_hook_status_name(BcHookStatus status) {
  switch (status) {
  case BcHookStatus::NotAttempted:
    return "not-attempted";
  case BcHookStatus::Ok:
    return "ok";
  case BcHookStatus::ProcessGone:
    return "process-gone";
  case BcHookStatus::HijackFailed:
    return "hijack-failed";
  case BcHookStatus::GnmDriverNotFound:
    return "gnm-driver-not-found";
  case BcHookStatus::SymbolNotFound:
    return "symbol-not-found";
  case BcHookStatus::DisasmFailed:
    return "disasm-failed";
  case BcHookStatus::AllocFailed:
    return "alloc-failed";
  case BcHookStatus::MprotectFailed:
    return "mprotect-failed";
  case BcHookStatus::WriteFailed:
    return "write-failed";
  case BcHookStatus::ReadFailed:
    return "read-failed";
  }
  return "unknown";
}

BcGnmHook::~BcGnmHook() { reset(); }

void BcGnmHook::reset() {
  delete impl_;
  impl_ = nullptr;
  pid_ = -1;
  target_va_ = 0;
  counter_va_ = 0;
  hook_len_ = 0;
  installed_ = false;
}

BcHookStatus BcGnmHook::install(pid_t pid) {
  reset();
  if (pid <= 0)
    return BcHookStatus::ProcessGone;

  impl_ = new Impl();
  Impl &im = *impl_;

  im.hijacker = Hijacker::getHijacker(pid);
  if (im.hijacker == nullptr) {
    LOG_WARN("fps-bc: getHijacker(%d) failed", static_cast<int>(pid));
    reset();
    return BcHookStatus::HijackFailed;
  }

  /* Locate libSceGnmDriver and resolve the flip export by NID. */
  uint64_t fn = 0;
  char found_name[96] = {};
  bool any_candidate = false;
  for (auto lib : im.hijacker->getLibs()) {
    StringView path = lib->getPath();
    if (!path.contains("GnmDriver") && !path.contains("gnmdriver"))
      continue;
    any_candidate = true;
    LOG_INFO("fps-bc: gnm candidate %s imagebase=0x%llx", path.c_str(),
             static_cast<unsigned long long>(lib->imagebase()));
    fn = resolve_rtld(lib.get(), found_name, sizeof(found_name));
    if (fn != 0) {
      im.gnm = lib.release();
      break;
    }
    if (!im.logged_dump_) {
      dump_symbols(lib.get());
      im.logged_dump_ = true;
    }
  }
  if (fn == 0) {
    if (any_candidate)
      LOG_WARN("fps-bc: GNM flip symbol not found in pid %d",
               static_cast<int>(pid));
    else
      LOG_WARN("fps-bc: libSceGnmDriver not found in pid %d",
               static_cast<int>(pid));
    reset();
    return any_candidate ? BcHookStatus::SymbolNotFound
                         : BcHookStatus::GnmDriverNotFound;
  }
  LOG_INFO("fps-bc: resolved %s -> 0x%llx", found_name,
           static_cast<unsigned long long>(fn));

  /* Disassemble the entry prologue to a clean instruction boundary. */
  uint8_t code[kPrologueRead] {};
  if (!im.hijacker->read(fn, code, sizeof(code))) {
    LOG_WARN("fps-bc: prologue read failed @0x%llx",
             static_cast<unsigned long long>(fn));
    reset();
    return BcHookStatus::ReadFailed;
  }

  uint32_t hook_len = 0;
  while (hook_len < kHookMin) {
    hde64s hs {};
    const uint32_t n = hde64_disasm(code + hook_len, &hs);
    if (n == 0 || (hs.flags & F_ERROR)) {
      LOG_WARN("fps-bc: disasm failed @0x%llx+%u",
               static_cast<unsigned long long>(fn),
               static_cast<unsigned>(hook_len));
      reset();
      return BcHookStatus::DisasmFailed;
    }
    hook_len += n;
    if (hook_len > kPrologueRead) {
      LOG_WARN("fps-bc: prologue exceeds read window @0x%llx",
               static_cast<unsigned long long>(fn));
      reset();
      return BcHookStatus::DisasmFailed;
    }
  }

  /* Carve the trampoline and its counter out of the eboot section tails. */
  ProcessMemoryAllocator text_alloc = im.hijacker->getTextAllocator();
  ProcessMemoryAllocator data_alloc = im.hijacker->getDataAllocator();

  uint64_t tramp_code = text_alloc.allocate(
      static_cast<size_t>(hook_len) + kIncBytes + kJmpBytes);
  uint64_t counter_va = data_alloc.allocate(kCounterBytes);

  if (!va_mapped(pid, tramp_code) || !va_mapped(pid, counter_va)) {
    /* The eboot tail is not mapped; fall back to the GnmDriver sections. */
    LOG_INFO("fps-bc: eboot alloc tail unmapped; using GnmDriver sections");
    const SharedLibSection *ts = im.gnm ? im.gnm->getTextSection() : nullptr;
    const SharedLibSection *ds = im.gnm ? im.gnm->getDataSection() : nullptr;
    if (!ts || !ds) {
      LOG_WARN("fps-bc: GnmDriver sections unavailable");
      reset();
      return BcHookStatus::AllocFailed;
    }
    text_alloc = ProcessMemoryAllocator(ts);
    data_alloc = ProcessMemoryAllocator(ds);
    tramp_code = text_alloc.allocate(
        static_cast<size_t>(hook_len) + kIncBytes + kJmpBytes);
    counter_va = data_alloc.allocate(kCounterBytes);
    if (!va_mapped(pid, tramp_code) || !va_mapped(pid, counter_va)) {
      LOG_WARN("fps-bc: GnmDriver alloc tail also unmapped");
      reset();
      return BcHookStatus::AllocFailed;
    }
  }

  /* trampoline = prologue | inc qword [counter] | jmp back */
  uint8_t block[64] {};
  uint8_t *p = block;
  std::memcpy(p, code, hook_len);
  p += hook_len;
  *p++ = 0xFF;
  *p++ = 0x05;
  const int64_t disp64 = static_cast<int64_t>(counter_va) -
                         static_cast<int64_t>(tramp_code + hook_len + kIncBytes);
  if (disp64 < INT32_MIN || disp64 > INT32_MAX) {
    LOG_WARN("fps-bc: counter out of disp32 range (tramp=0x%llx "
             "counter=0x%llx)",
             static_cast<unsigned long long>(tramp_code),
             static_cast<unsigned long long>(counter_va));
    reset();
    return BcHookStatus::AllocFailed;
  }
  const int32_t disp = static_cast<int32_t>(disp64);
  std::memcpy(p, &disp, sizeof(disp));
  p += sizeof(disp);
  *p++ = 0xFF;
  *p++ = 0x25;
  const uint32_t zero = 0;
  std::memcpy(p, &zero, sizeof(zero));
  p += sizeof(zero);
  const uint64_t back = fn + hook_len;
  std::memcpy(p, &back, sizeof(back));
  p += sizeof(back);
  const size_t block_bytes = static_cast<size_t>(p - block);

  constexpr int kRwx = PROT_READ | PROT_WRITE | PROT_EXEC;
  constexpr int kRx = PROT_READ | PROT_EXEC;
  const intptr_t tramp_page = static_cast<intptr_t>(tramp_code) & ~0xFFFL;
  const intptr_t fn_page = static_cast<intptr_t>(fn) & ~0xFFFL;

  if (kernel_mprotect(pid, tramp_page, 0x1000, kRwx) != 0) {
    LOG_WARN("fps-bc: mprotect(RWX) failed @0x%llx errno=%d",
             static_cast<unsigned long long>(tramp_page), errno);
    reset();
    return BcHookStatus::MprotectFailed;
  }
  if (kernel_mprotect(pid, fn_page, 0x1000, kRwx) != 0) {
    LOG_WARN("fps-bc: mprotect(RWX) failed @0x%llx errno=%d",
             static_cast<unsigned long long>(fn_page), errno);
    reset();
    return BcHookStatus::MprotectFailed;
  }

  const uint64_t zero64 = 0;
  if (onion_proc_copyin(pid, counter_va, &zero64, sizeof(zero64)) != 0) {
    LOG_WARN("fps-bc: counter zero write failed @0x%llx",
             static_cast<unsigned long long>(counter_va));
    reset();
    return BcHookStatus::WriteFailed;
  }

  if (onion_proc_copyin(pid, tramp_code, block, block_bytes) != 0) {
    LOG_WARN("fps-bc: trampoline write failed @0x%llx",
             static_cast<unsigned long long>(tramp_code));
    reset();
    return BcHookStatus::WriteFailed;
  }

  uint8_t patch[kJmpBytes] = {0xFF, 0x25, 0, 0, 0, 0};
  std::memcpy(patch + 6, &tramp_code, sizeof(tramp_code));
  if (onion_proc_copyin(pid, fn, patch, sizeof(patch)) != 0) {
    LOG_WARN("fps-bc: entry patch failed @0x%llx",
             static_cast<unsigned long long>(fn));
    reset();
    return BcHookStatus::WriteFailed;
  }

  if (kernel_mprotect(pid, fn_page, 0x1000, kRx) != 0)
    LOG_WARN("fps-bc: mprotect(RX) restore failed @0x%llx errno=%d",
             static_cast<unsigned long long>(fn_page), errno);

  pid_ = pid;
  target_va_ = fn;
  counter_va_ = counter_va;
  hook_len_ = hook_len;
  installed_ = true;
  LOG_INFO("fps-bc: hook installed pid=%d fn=0x%llx tramp=0x%llx counter=0x%llx "
           "len=%u",
           static_cast<int>(pid), static_cast<unsigned long long>(fn),
           static_cast<unsigned long long>(tramp_code),
           static_cast<unsigned long long>(counter_va),
           static_cast<unsigned>(hook_len));
  return BcHookStatus::Ok;
}

bool BcGnmHook::sample(uint64_t *count, BcHookStatus *status) {
  if (!count) {
    set_status(status, BcHookStatus::NotAttempted);
    return false;
  }
  if (!installed_ || impl_ == nullptr || impl_->hijacker == nullptr) {
    set_status(status, BcHookStatus::NotAttempted);
    return false;
  }
  uint64_t value = 0;
  if (onion_proc_copyout(pid_, counter_va_, &value, sizeof(value)) != 0) {
    set_status(status, BcHookStatus::ReadFailed);
    return false;
  }
  *count = value;
  set_status(status, BcHookStatus::Ok);
  return true;
}

} // namespace fps
} // namespace onion
