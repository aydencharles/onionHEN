# Standalone FPS counter

One payload (`fps_counter.elf`) that samples game FPS and publishes it to
`/system_tmp/onionhen/fps_sample`. No daemon, util, kstuff, settings or ShellUI.

Build: `export PS5_PAYLOAD_SDK=...; ./build.sh` -> `build/bin/fps_counter.elf`
Run: send the ELF to an elfldr (9021) on a jailbroken console, then start a game.
Display: shows "FPS: NN" as a system toast every 5 s (change kToastEverySec in src/main.cpp).
Log: `/data/onion_fps.log` (+ klog).
Read: include `fps_sample.h` and call `onion_fps_read()` (stale after 2 s).

## Modes
- **Safe (default):** scanout counter from /dev/dce only. No kernel memory
  reads, no game patching, waits 15 s after a game starts.
- **Full:** create an empty file `/data/onion_fps_full` before loading. Adds
  render counters (PS5) and the PS4 BC hook. These read game memory through
  the kernel and patch game code, which is the likely cause of kernel panics
  on unsupported firmware.
