# OnionHEN host unit tests

Host-side unit tests for shared libraries and util cheat **file parsing**
(no PS5 SDK). Modeled after `kylin-core/tests`.

## Run

```bash
cd source/util/tests
make test
```

Requirements:

- Host `clang` / `c++` (or set `HOST_CC` / `HOST_CXX`)
- Keystone: `/opt/homebrew` by default (`KEYSTONE_PREFIX=...` if elsewhere)

Optional:

```bash
ONION_TEST_VERBOSE=1 make test   # extra klog-style noise on stderr
make clean && make test
```

Binary: `source/util/build/host-tests/onion-host-tests`

## Coverage

| Suite | What it locks |
|-------|----------------|
| `test_cheat_utils` | hex decode, JSON extract, braces, replace_all, load buffer, ABI layout |
| `test_cheat_parsers` | JSON / SHN / MC4 / ShnExt via factory + real fixtures |
| `test_cheat_flatten` | extension match, TITLE/VERSION/PROCESS/SOURCE_ID parse, HENCC json/shn/mc4 copy with original names, version sanitize |
| `test_cheat_repository` | resolve every compatible TITLE_VERSION[_PROCESS][_SOURCE_ID] source, JSON/SHN/MC4 side-by-side, process vs generic |
| `test_git_mirror_factory` | archive URL factory; zh-Hans → cnb |
| `test_cheat_catalog` | registry + catalog layout/slug (no host URLs) |
| `test_cheat_sync` | mocked HTTPS ZIP download, miniz extraction, fallback, install and cleanup |
| `test_multipart` | DPI HTTP multipart upload: offset-based streaming, Content-Length validation, hex encoding |
| `test_payload` | `libonion_payload` ELF magic, package header, pid path/file, read_file |
| `test_base64` | encode / decode / round-trip (MC4 codec) |
| `test_aes_cbc` | AES-256-CBC encrypt/decrypt with MC4 key/IV |
| `test_hde64` | x86_64 length decode (nop/ret/mov/jmp) |
| `test_hotpatch` | aligned atomic entry-patch image, absolute jump encoding, preserved tail bytes, invalid input rejection |
| `test_trampoline_arena` | same-code-page allocations are unique, non-overlapping, and owned by one near arena |
| `test_hook_lifecycle` | Installing/Ready/Failed callback barrier state |
| `test_x64_relocator` | relocation-aware trampolines: RIP-relative memory/call, rel8/rel32 call/jmp/jcc, internal targets, safe rejection |
| `test_reg_entity` | registry entity-id formula (account slots) |
| `test_toolbox_helpers` | UI path rewrite + payload .elf basename filter |
| `test_overlay_layout` | monitor-bar packing: top/bottom × left/right, hidden metrics, per-core width |
| `test_settings` | semantic schema serialize/round-trip, partial INI defaults |
| `test_fps_formula` | broad PS4 BC classification: PPSA/PPSB/CUSA/PCAS/PCJS/PCKS/CUHJ prefix detection |
| `test_builtin_services` | builtin service key lookup, reserved-name protection, and pkgnet port allocation |
| `test_ready` | ready markers, PID-bound process instances, path builder, name rejection, **fps_overlay / util_booted** flags, toolbox runtime-root marker |
| `test_toolbox_injection` | same-PID skip, new-PID reinject, failure cleanup, concurrent request serialization |
| `test_platform_fs` | `if_exists` / `touch_file` / `rmtree` (libonion_platform) |
| `test_platform_log` | `onion_log_configure` + file sink |
| `test_platform_notify` | `onion_notify_format` prefix/truncate + send stub |
| `test_msg_protocol` | IPC paths, magic, command ordinals, `IPC_Ret`, message POD, reply JSON body |
| `test_util_language` | system-language cache/refresh + `util_webui_language_code` maps all 14 selectable locales to web-UI BCP-47 codes |
| `test_app_jailbreak_policy` | configurable app-jailbreak exact/prefix Title ID allowlist, including the Itemzflow compatibility ID `ITEM00001` |
| `test_ps5_settings_ui` | fluent XML builder + escaping |
| `test_toolbox_route` | resource → page routing + dynamic cheat enabled-state, ShadowMount+ and PkgNet descriptors |
| `test_toolbox_i18n` | locale-key parity for toolbox groups (zh-Hans, en) including shadowmount and pkgnet keys |
| `test_toolbox_pkg_xml` | PkgInstaller chooser XML, PkgNet toggle XML, shadowmount config XML, resource identifiers |
| `test_onpress_policy` | page-scoped OnPress ownership, stock-page pass-through, unrelated-resource stability |

## Intentionally not host-tested

| Area | Why |
|------|-----|
| `CheatApplier` / memory backends | needs target process / mdbg |
| `libonion_proc` allproc / ucred | kernel_copyout |
| ShellUI Mono invocation / OnPress handlers | SceShellUI; page ownership policy is host-tested |
| Full IPC server accept loop | device sockets + daemon world |
| libNineS inject | ptrace |

## Fixtures

`fixtures/cheats/` — small subset of cheat samples (json / shn / mc4 / ShnExt).
