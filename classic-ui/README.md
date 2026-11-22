# Shared classic drawing library

`libromaw-classic-ui.a` supplies the host-independent C geometry and drawing
rules used by both the Toolbox QuickDraw adapter and the native Finder. It has no
Windows, guest-memory, CPU-runtime or Toolbox initialization dependency.

Sources live in `src/`; the canonical public interface is
`include/classic-ui/classic_ui.h`. The former ROMAW header path remains a
compatibility include so existing consumers and targeted tests keep working.
The QuickDraw adapter lives in `windows/quickdraw_adapter.c` because it translates
Toolbox ports, records and guest resources rather than supplying portable drawing.

`include/classic-ui/host_graphics.h` exposes that adapter to native x64 Finder.
The API uses host-order requests and RGB buffers. Actual WDEF/CDEF/MBDF and Font
Manager drawing runs as native compiled C; guest handles and byte order stay
behind the adapter. Bounded drawing requests also supply QuickDraw shapes and
CopyBits artwork transfers. Windows supplies the outer frame, input/control
models, source bitmap/glyph decoding and completed-buffer presentation.

The aggregate `quickdraw-host` regression check covers both the Toolbox renderer
and native presentation in two modules loaded by the existing ROMAW. Use
`bash system/tests/check-graphics.sh quickdraw-host --build-only` to compile
them in WSL. When WSL process interop is unavailable, run these from the repository
root in PowerShell:

```powershell
./system/tests/check-graphics.ps1 quickdraw-host
./system/tests/check-graphics.ps1 quickdraw-host `
  -Module D:\Source\ROMAW\build\quickdraw-host-probe\quickdraw-finder-probe.dll `
  -Marker QUICKDRAW_FINDER_PROBE_OK
```

From the repository root in WSL, run `make library-classic-ui` to build only this
archive and its prerequisites, or `make libraries` to build and stage the complete
runtime library set. ROMAW builds it in the selected profile's `obj/classic-ui`; see
[build profiles](../README.windows-build#build-profiles-and-output).

For standalone Windows validation:

```sh
cmake -S classic-ui -B build/classic-ui-static \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DROMAW_CLASSIC_UI_BUILD_TESTS=ON
cmake --build build/classic-ui-static
./build/classic-ui-static/classic-ui-test.exe
```

`bash system/tests/check-ui.sh classic-ui` additionally checks the portable
renderer with Linux ASan/UBSan. Licensing follows the repository's `COPYING`;
original file-specific provenance and legal text remain in the moved files.
