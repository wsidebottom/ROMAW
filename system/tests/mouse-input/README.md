# SDL mouse input integration probe

This original diagnostic creates real classic vertical/horizontal scrollbars.
Build from WSL using the local Retro68 toolchain:

```sh
cmake -S system/tests/mouse-input -B build/mouse-input-probe \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/retro68-toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
cmake --build build/mouse-input-probe -j10
```

Then run `./system/tests/check-ui.ps1 mouse-input` in PowerShell. Optional
`-ROMAW` and `-Gdb` arguments select absolute executable paths. It stages the
probe under `build/mouse-input-check`, runs a separate ROMAW under Windows GDB,
and injects SDL events only into that owned diagnostic process. It does not send
input to the user's desktop or running application. The process is terminated by
GDB after the guest explicitly signals success or failure.

Seven phases check default Control-click on press/release; exactly one scrollbar
callback; callback-free TrackControl return values; Shift-wheel horizontal
scrolling; pointer/button/modifier restoration and ignored auxiliary buttons; an
explicit Option-click modifier override; and repeated wheel input to an app that
requests only mouse-down. Normal ExitToShell is deliberately not accepted as a
successful test. The GDB log remains under `build/mouse-input-check`.

This verifies the standard Control Manager path in the Windows SDL build. It does
not establish compatibility with custom scrollbar implementations, contextual
menu extensions, or non-SDL frontends.
