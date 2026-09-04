# ShellUI injection contract

This document defines the active ShellUI injection path, its synchronization
rules, and its failure behavior.

## Runtime flow

```text
daemon: cmd_enable_toolbox()          [daemon/source/daemon_inject.cpp]
  |- get_shellui_pid()                find SceShellUI
  `- Inject_Toolbox(pid, shellui.elf) [libNineS/src/main.c]
       `- inject_elf(proc, elf)       [libNineS/src/injector.c]
            |- set_ucred_to_ptrace() once for the inject window
            |- pt_attach(SceShellUI)
            |- init_remote_function_pointers()
            |- elfldr_load()
            |- elfldr_payload_args()
            |- map and copy the bootstrap stager
            |- map and copy the SCEFunctions block
            |- pt_call2(bootstrap) -> remote pthread_create(elf_main)
            `- pt_detach() and restore the caller authid
```

The remote stager terminates with `int3`. `pt_call2` waits for that stop before
restoring the interrupted ShellUI registers.

## Readiness identity

After every required hook is ready, `shellui.elf` writes its PID to:

```text
/system_tmp/onionhen/ready/toolbox
```

The daemon waits up to 45 seconds for the expected PID. The marker is an
instance identity, not a boolean flag:

- matching PID means the current ShellUI instance is initialized;
- missing or different PID requires injection into the current process;
- a PID change during the handshake invalidates the acknowledgement.

`ToolboxInject` serializes the identity check, injection, and readiness wait.
Cold start and util reinjection therefore cannot ptrace the same ShellUI
process concurrently.

After Rest Mode, daemon observes the new `NPXS40087` process through SceSysCore
`NOTE_EXEC`, waits for `libSceNpTrophy.sprx` and `libSceNpTrophy2.sprx`, and
then runs the same serialized injection flow.

## Injection invariants

| Area | Contract |
|------|----------|
| Authid | `inject_elf` enters one `PTRACE_AUTHID` window and restores the caller's authid on every exit path |
| ptrace | Calls execute inside that credential window without per-call authid changes |
| Remote calls | `pt_call`, `pt_call2`, and `pt_syscall` use an ABI-aligned private stack below the interrupted frame and outside the 128-byte red zone |
| Syscall completion | `pt_syscall` single-steps only the `syscall` opcode at `getpid+0xa` and maps FreeBSD CF to `-1`; it does not step the libc stub |
| Function completion | `pt_call` RETs into a process-lifetime RWX INT3 gadget, then `waitpid` for `SIGTRAP` |
| Register restore | `pt_call2` / `pt_call` wait for the INT3 stop before restoring saved registers |
| Mapping | Null, `MAP_FAILED`, mprotect, and copyin failures abort injection |
| Attach state | A successful detach clears the attached state; cleanup detaches whenever an attach completed |
| Return value | Any failed stage returns failure to the daemon and cannot publish Toolbox readiness |

The invariant is `rsp % 16 == 8` at the remote call boundary. Return traps
are never planted on the NX stack.

## Hook installation safety

ShellUI remains active while the payload thread installs hooks. Two mechanisms
keep callbacks coherent during that window:

- `TrampolineArena` uses non-fixed mmap hints and page-sized bump allocation.
  Every trampoline has a unique, non-overlapping address within rel32 reach of
  its target. Allocation fails closed when no valid mapping is available.
- Hook callbacks remain pass-through while lifecycle state is `Installing`.
  The payload publishes `Ready` only after all detours and shared dependencies
  are initialized.

Controller, navigation, render, registry, capture, and resource hooks call
their original functions until the lifecycle barrier reaches `Ready`.

## Failure behavior

An injection failure has these observable results:

1. The daemon receives a failed injection result.
2. The target is detached when attachment succeeded.
3. The daemon authid is restored.
4. The expected PID is not published as ready.
5. A later serialized request may retry the current ShellUI instance.

The injector does not kill or respawn ShellUI as part of this operation.

## Source map

| Responsibility | Source |
|----------------|--------|
| Request serialization and PID readiness | `source/daemon/source/daemon_inject.cpp` |
| Injection orchestration | `source/libNineS/src/main.c` |
| ELF mapping and stager launch | `source/libNineS/src/injector.c` |
| Remote ptrace calls | `source/libonion_elfldr/source/pt.c` |
| Trampoline allocation | `source/libonion_detour/source/trampoline_arena.cpp` |
| Hook lifecycle barrier | `source/shellui/src/hook_lifecycle.cpp` |
