# Startup display probe

This small 68K application reaches InitGraf without depending on a Finder
implementation. The startup-window and display-scaling tests stop there to
inspect the host surface.

Build from WSL before running those PowerShell tests.

```sh
cmake -S system/tests/startup-display -B system/obj/tests/startup-display \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/retro68-toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
cmake --build system/obj/tests/startup-display
```
