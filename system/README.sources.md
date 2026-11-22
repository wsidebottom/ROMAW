# ROMAW source layout

C sources are grouped in library directories at the repository root. Public
Macintosh and ROMlib headers live in root `include/`; platform backends are in
`platform/` and `frontend-sdl/`. Automake mirrors the source subdirectories
beneath the selected profile's object directory, including dependency files.
Build from the workspace root with `make` in WSL; never configure in a source directory.

Build profiles, object locations and benchmark commands are documented in
[the build guide](../README.windows-build). Paths below using `system/obj`
refer to the debug profile; optimized output uses `build/romaw-optimized/obj`.
For archive ownership and targeted rebuilds, see [static libraries](../docs/static-libraries.md).

| Directory | Responsibility |
| --- | --- |
| `runtime/` | Startup, process/segment management, traps, CFM and Mixed Mode |
| `memory/` | Macintosh heaps, handles and temporary memory |
| `files/`, `hfs/`, `files/mkvol/` | Host files, forks, volumes, HFS and volume creation |
| `quickdraw/`, `display/` | Drawing, images, blitters and display refresh |
| `controls/`, `dialogs/`, `lists/`, `menus/`, `windows/` | Classic UI managers |
| `classic-ui/`, `windows/quickdraw_adapter.c` | Shared drawing primitives and QuickDraw adapter |
| `text/`, `textedit/` | Fonts, scripts, international text and TextEdit |
| `appleevents/`, `events/` | AppleEvents, Toolbox events and virtual interrupts |
| `sound/`, `printing/` | Sound Manager and printing |
| `numerics/` | Floating-point, decimal and Toolbox math |
| `devices/` | Device, serial and communications managers |
| `options/` | Options, numeric parsing and preferences |
| `ui/` | Application chrome, icons, appearance and help balloons |
| `finder/` | Native x64 Finder desktop, child windows and file operations |
| `services/` | Other Toolbox services, scrap, time, editions and packages |
| `host/` | Shared host/Unix support |
| `diagnostics/` | Errors, traces and diagnostic dumps |
| `tools/romaw/` | Build-time table generators and auxiliary checks |

The native Finder is built and linked by ROMAW's Windows wrapper. Its headers
live in `include/finder`, build tools in `tools/romaw/native_finder`, and tests in
`system/tests/native-finder`. Editable classic and native resources live in
`finder/resources`. Generated artwork, objects and test executables
belong in `system/obj/native-finder`. See `finder/README.md` for its build and
runtime checks.

Enlarged, framed SDL guest windows use optional Direct3D 11 presentation. The
Macintosh framebuffer and QuickDraw drawing stay in CPU memory; dirty packed
pixel bytes are uploaded at guest resolution, and a shader performs palette
lookup and exact nearest-neighbour scaling. Unscaled windows, fullscreen,
shaped accessories, preferences and loading overlays use software presentation.
Missing Direct3D/shader runtime components or device failures fall back to SDL.
Set `ROMAW_GPU=0` before launch to force software presentation. Diagnostics
identify the selected GPU path when ROMAW is launched with debug output.
Run `bash system/tests/check-graphics.sh sdl-gpu` for Windows GPU readback
comparisons, or append `--benchmark` to compare CPU scaling with steady-state
GPU upload/presentation call time. This benchmark does not measure whole-app
speed or GPU completion latency. Both use the current debug build settings.
`system/tests/sdl_gpu_probe.gdb` checks live overlay/hold transitions, simulated GPU
unavailability, full software recovery and 1x/2x mode changes. Set GDB's arguments
to `-syszone 2M -applzone 4M -display-scale 200` followed by a classic application
path before running the probe; the application must not already be running.

ICNS artwork and CUR cursors live in the workspace-root `bitmaps/` directory;
templates and generator scripts stay at the source root. Their output belongs
in `obj/generated`; generated C from `parse.y` belongs in `obj`.
Explicit generated-include prerequisites must target the consumer's new object
path (for example `menus/stdmbdf.o`). Native table generators use `tools/*.c`.

The interpreted PPC project lives at workspace-root `synppc/`; its Toolbox import
generator reads `runtime/emutraptables.c`. Its PEF loader and CFM bridge also
provide CFM-68K support. `Makefile.am` is the maintained source list; the obsolete
flat-directory `romaw.make` list has been removed. Native host architecture
support remains in `platform/arch/`.

Standalone SDL desk accessories retain their Macintosh title bars. On Windows
and X11 with the Shape extension, the host window follows the union of visible
Macintosh window structure regions, including modal dialogs. Outside regions
are transparent and do not receive clicks; control colours remain opaque.
`system/tests/check-graphics.sh desk-shape` checks Windows region holes and replacement.
Classic cdev control panels enter this same presentation after initialization.
Host close requests run the cdev close callback before releasing the dialog;
Macintosh title-bar dragging moves the host window. The caller's presentation
state is restored after either a panel or accessory returns.
`system/tests/check-ui.ps1 control-panel-host` checks the installed Sound and General
panels under Windows GDB, including cropping, shape, close and restoration.
Color's intermittent initialization crash predates this presentation change;
other cdev compatibility is not established by these host-window tests.

DRVR/Apple resource menus include the Apple Menu Items folder, with installed
Control Panels in a hierarchical submenu. ROMAW handles submenu selections
so older applications need not know its allocated menu ID. Available file icons
are selected through their FREF/BNDL mapping and drawn as small colour suites.
The menu cache stores copied icon bytes, with temporary suites owned by each
draw; menu disposal or reinitialization clears the cached entries. Resource-file
selection and pre-existing open maps are preserved during icon loading.
Extended LaunchApplication calls for `dfil` and `cdev` files use the same
separate-process path as Apple menu items, before replacing any caller state.
Opening an accessory or classic panel directly in ROMAW Finder therefore
keeps Finder running; ordinary APPL launches retain their single-guest behavior.
