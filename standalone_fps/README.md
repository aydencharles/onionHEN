# Standalone FPS overlay

`fps_counter.elf` is the **only payload to load**. It samples the scanout
counter (/dev/dce, no game or kernel memory access), publishes it to
`/system_tmp/onionhen/fps_sample`, and injects an overlay-only ShellUI module
(built with `ONIONHEN_FPS_OVERLAY_ONLY` and embedded in `fps_counter.elf`) that
draws two plain white text rows, top-left, regular weight, no background panel:

    FPS: 59.94
    CPU: 43°C / GPU: 35°C

Build: `export PS5_PAYLOAD_SDK=...; ./standalone_fps/build.sh`

Output: `standalone_fps/build/bin/fps_counter.elf` (the only file in `build/bin/`).
The overlay module `shellui.elf` is an intermediate under `build/internal/`;
never load it separately (it is already embedded, and loading it on its own
fails).

Before it injects anything the payload checks that kstuff is running (it waits
up to 20 s, then exits with an error) and that SceShellUI does not already have
the overlay.

Log: `/data/onion_fps.log` - look for `kstuff detected`, then
`injecting fps overlay` followed by `fps overlay ready`.

Appearance is fixed in code: layout in `layout_bar_labels()`
(`source/shellui/src/prx_overlay.cpp`), font size in
`overlay::kStandaloneFontPt` (`source/shellui/include/overlay_layout.hpp`),
colour/weight in `source/shellui/src/shellui_overlay_widgets.cpp`, enabled
metrics/position in `fps_overlay_only_main()` (`source/shellui/src/prx.cpp`).

Stopping the payload: on a clean exit (SIGTERM/SIGINT) it publishes a shutdown
marker and the overlay removes all its widgets at once. If the payload is
killed outright, the overlay notices that the heartbeat (the payload publishes
at least once a second) stopped for 4 s and removes its widgets too. Starting
the payload again brings the overlay back without re-injecting.
