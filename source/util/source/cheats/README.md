# OnionHEN Cheats (C++ orchestration)

## Architecture

See [docs/util_arch/cheats_cpp.md](../../../../docs/util_arch/cheats_cpp.md).

```text
IPC / main
  └─ onion::cheats::CheatService          # Facade + mutex + hot-reload state
       ├─ CheatRepository                 # path resolve, load, flatten
       │    └─ CheatParserFactory         # Strategy by extension
       │         ├─ JsonCheatParser
       │         ├─ XmlCheatParser (.shn)
       │         ├─ Mc4CheatParser
       │         └─ ShnExtCheatParser → C
       ├─ CheatApplier                    # apply patches
       │    └─ IMemoryBackend (Strategy)
       │         ├─ MdbgMemoryBackend
       │         └─ KdirectMemoryBackend  # MemoryBackendFactory
       └─ C helpers                       # utils / ShnExt crypto / flatten

IPC DOWNLOAD_CHEATS
  └─ onion::cheats::sync::CheatSyncService
       ├─ CheatCatalogRegistry / ICheatCatalog
       ├─ CheatMirrorFactory / ICheatMirror
       ├─ IHttpTransport + miniz archive extractor
       └─ CheatRepository::flattenInstallTree   # reuse
```

## Patterns

| Pattern | Class |
|---------|--------|
| Facade | `CheatService` |
| Singleton | `CheatService::instance()` |
| Strategy | `IMemoryBackend`, `ICheatParser`, `ICheatCatalog`, `ICheatMirror` |
| Factory | `MemoryBackendFactory`, `CheatParserFactory`, `CheatMirrorFactory` |
| Adapter | `ShnExtCheatParser` |
| RAII | `std::lock_guard` on service state |

## Formats / paths

```text
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].{json,shn,mc4,ShnExt}
```

`PROCESS` and 8-hex `SOURCE_ID` are optional. `SOURCE_ID` identifies a
physical source. Catalog install copies `json/`, `shn/`, and `mc4/` files
into this directory with their original names. The repository returns all
sources compatible with title/version/process; format priority only
determines deterministic display order.

## Memory

| FW major | Backend |
|---------|---------|
| `< 0x840` | mdbg |
| `>= 0x840` | kdirect |

Code cave: `pt_attach_proc` + `pt_mmap` + `kernel_mprotect`.

## Host tests (parser)

No PS5 SDK required. Modeled after kylin-core host cheat tests:

```bash
cd source/util/tests && make test
```

See `source/util/tests/README.md`. Covers JSON/SHN/MC4/ShnExt parse + utils + fixtures.
