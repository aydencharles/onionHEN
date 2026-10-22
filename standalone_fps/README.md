# Standalone FPS overlay

`fps_counter.elf` samples the scanout counter (/dev/dce, no game or kernel
memory access), publishes it to `/system_tmp/onionhen/fps_sample`, and injects
an overlay-only ShellUI module (built with `ONIONHEN_FPS_OVERLAY_ONLY`) that
draws stacked rows, regular font, top-left:

    FPS  60
    CPU  54C  12%
    GPU  61C  40%

Build: `export PS5_PAYLOAD_SDK=...; ./standalone_fps/build.sh`
Load `build/bin/fps_counter.elf` via elfldr (9021) on a jailbroken console.
Log: `/data/onion_fps.log`. Position/alignment/metrics are fixed in
`fps_overlay_only_main()` in `source/shellui/src/prx.cpp`.
