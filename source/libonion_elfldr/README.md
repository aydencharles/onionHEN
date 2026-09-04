# libonion_elfldr

Single implementation of ptrace helpers (`pt_*`) and inject-path ELF loading
(`elfldr_load`, `elfldr_payload_args`, `elfldr_spawn`,
`elfldr_raise_privileges`).

## Authid policy

**Do not** flip ucred authid around every `ptrace` syscall.

Elevate **once** for the inject / attach window with:

```c
set_ucred_to_ptrace();  // → PTRACE_AUTHID 0x4800000000010003
```

That is Sony's SceTracer-style id. **`DEBUG_AUTHID` (`…0006`) is not accepted
for PT_*** — using it causes attach/`waitpid` failures (e.g. errno ECHILD).

Restore the previous authid after the inject window (see `inject_elf`).

## Remote calls

`pt_call` / `pt_syscall` must not single-step through libc. That is what used
to freeze `SceShellUI` for hundreds of milliseconds during Toolbox inject.

- `pt_syscall` sets RIP to `getpid+0xa` (`syscall`), steps **once**, then
  restores the interrupted registers. FreeBSD carry-flag errors become `-1`.
- `pt_call` plants a return address to a per-pid RWX `INT3` page (stack pages
  are NX) and `PT_CONTINUE`s until that trap.
- `pt_call2` is unchanged: the stager itself ends in `int3`.

Do not skip `elfldr_payload_args()` sockets/pipes on Toolbox inject.
ShellUI's CRT `payload_init` can stash `getpid+0xa` as a syscall gadget,
but later `kernel_copyin` / `dlsym("exit")` still need the ipv6/pipe
primitive. A kstuff-only stub ended at the CRT `ud2` after `exit` failed
to resolve (SIGILL, privileged instruction / `#UD`).

## Consumers

| Target | Uses |
|--------|------|
| libNineS | `pt_*`, `elfldr_load`, `elfldr_payload_args` |
| bootstrapper | `elfldr_raise_privileges` |
| onion_elfldr_server | `elfldr_spawn`, `elfldr_read` |
| util | `pt_attach` / `pt_mmap` after `set_ucred_to_ptrace()` |
| daemon | links via NineS |

Headers: `<onion/pt.h>`, `<onion/elfldr.h>`. Compatibility shims remain under
`libNineS/include/{pt,elfldr}.h` and `bootstrapper/include/`.
