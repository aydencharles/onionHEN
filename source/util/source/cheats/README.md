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
       │         ├─ PsGamePatchXmlParser (.xml)
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
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>.xml
```

`PROCESS` and 8-hex `SOURCE_ID` are optional. `SOURCE_ID` identifies a
physical source. Catalog install copies `json/`, `shn/`, and `mc4/` files
into this directory with their original names. The repository returns all
sources compatible with title/version/process; format priority only
determines deterministic display order.

Illusion PS-Game-Patch XML is read from a local `<TITLE_ID>_<VERSION>.xml`
file. The XML `TitleID` list and a metadata `AppVer` entry must match that
filename; comma-separated version lists and `AppVer="mask"` are accepted.
Each metadata block targets its `AppElf`; a direct address is converted to a
module-relative offset as `Address - ImageBase`, with `ImageBase` defaulting to
`0x00400000`. Direct types `byte`, `bytes16`, `bytes32`, `bytes64`, `bytes`,
`float32`, `float64`, `utf8`, and `utf16` are supported. `bytes` values may
contain up to 255 bytes. `mask` and `mask_jump32` signatures use two-digit hex
byte pairs. Use `?` or `??` for wildcards; whitespace between pairs is optional.
Both types accept a signed `Offset` relative to the first signature byte.
`mask` writes the hex
`Value` at the resolved address.
`mask_jump32` uses `Target` as an exact byte sequence to locate a code cave,
writes the hex `Value` there, and adds a return branch; optional `Size` (default
5, minimum 5) controls the source branch and NOP padding. `mask` signatures
must resolve uniquely within readable module ranges. A `mask_jump32` source and
target must resolve uniquely within executable module ranges, and both write
ranges must remain executable. A metadata block is skipped as a whole if any
line is unsupported or malformed. Disabling an XML patch restores the bytes
captured before it was enabled, provided the live bytes still match the applied
patch.
Integer values accept decimal or prefixed hexadecimal (`0x`, `#`, `$`) input
and must fit the selected type width. UTF string values support `\n`, `\t`,
`\r`, `\0`, `\\`, and `\xHH` escapes.

Example local file `/data/OnionHEN/cheats/CUSA00001_01.00.xml`:

```xml
<Patch>
  <TitleID><ID>CUSA00001</ID></TitleID>
  <Metadata Name="Example 60 FPS" AppVer="01.00" AppElf="eboot.bin">
    <PatchList>
      <Line Type="bytes32" Address="0x00412345" Value="0x00000001"/>
    </PatchList>
  </Metadata>
</Patch>
```

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
