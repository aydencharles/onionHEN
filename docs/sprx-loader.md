# OnionHEN SPRX Loader

`libonion_sprx` provides a reusable, synchronous use-case for loading a
`.sprx`/`.prx` module into an already-running PS5 process. It is intentionally
separate from the FPS sampler and from the ELF plugin manager.

## Design

The public `SprxLoader` owns validation and idempotency policy. It depends only
on `IRemoteSprxRuntime`, so host tests or another platform can provide a fake
runtime without linking ptrace or PS5 kernel code. `PtraceSprxRuntime` is the
production adapter and contains the PS5-specific details:

1. Elevate the loader process to `PTRACE_AUTHID` for one scoped operation.
2. Attach to the target and resolve `sceKernelLoadStartModule` and
   `pthread_create` by NID.
3. Allocate three temporary target pages for context, path, and thread data.
4. Start a short remote thread thunk. The thunk calls
   `sceKernelLoadStartModule(path, 0, 0, 0, 0, 0)` and records its return code.
5. Detach while the module initializes and poll the target's dynlib table.
6. Reattach only after the thunk completed, then unmap temporary pages.

The loader does not inject a persistent ELF payload and does not leave a
custom SPRX resident beyond the module requested by the caller.

## Usage

```cpp
#include <onion/sprx_loader.hpp>

onion::sprx::PtraceSprxRuntime runtime;
onion::sprx::NoopTargetAccessPolicy access;
onion::sprx::TitleIdAllowlist allowlist;
allowlist.add_exact("CUSA12345");
onion::sprx::SprxLoader loader(runtime, access, allowlist);

const auto result = loader.load({
    .pid = game_pid,
    .path = "/data/plugins/example.sprx",
    .options = {.timeout_ms = 5000, .poll_ms = 10},
});

if (!result.succeeded()) {
  // load_status_name(result.status), result.remote_result
}
```

The target must be able to resolve the two NIDs and access the requested path.
For a sandboxed game, the caller must apply the project's existing target
jailbreak/privilege policy before loading, or provide a runtime adapter that
does so. The framework deliberately does not silently jailbreak arbitrary
processes.

## Why this is not wired into FPS

FPS currently uses daemon-side `/dev/dce`, DMAP, and a BC GNM detour. This
loader is a general infrastructure component for future user-requested SPRX
plugins; adding it to the FPS path would reintroduce the old FPS injection
coupling.
