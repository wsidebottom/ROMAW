# Shared classic UI rendering

The shared `classic-ui` static library contains the standard document WDEF frame, title bevel/rules and
MBDF menu-bar background, highlight and separator rules, plus the common
active/inactive/clicked scrollbar-arrow state selection. Its C API accepts native
integers, half-open rectangles and colour roles. It has no Windows, QuickDraw,
guest-memory, byte-order or CPU dependency.

`quickdraw_adapter.c` also exposes the versioned, host-order interface in
`include/classic-ui/host_graphics.h`. The native Finder uses real QuickDraw,
StandardWDEF, StandardCDEF and StandardMBDF for document frames, scrollbars,
grow boxes, menu-bar titles and desktop PixPat drawing. Windows retains the
outer application caption, HWND lifecycle, input and command dispatch.

The interface accepts ordinary integers, Pascal MacRoman titles and caller-owned
top-down RGB buffers. Packed Toolbox records, guest handles, palettes and byte
swapping stay inside the adapter. Calls require the initialized Toolbox UI
thread. Cached hidden Toolbox windows supply both drawing and WDEF hit testing;
they are released with their native counterparts. Wide surfaces are painted in
bands to respect the PixMap rowBytes limit.

No-application startup initializes the Toolbox before entering the built-in
native Finder; the SDL presentation window stays hidden. Explicit classic app
launches retain their existing entry paths. The built-in
Finder defaults to a 512 MiB application partition with 32-bit addressing;
explicit memory options retain their usual meaning.

MBDF supplies title geometry and pixels; the detached Windows menu model drives
commands and keyboard tracking. Ordinary popup rows use StandardMDEF measurement,
text, colours, selection, checkmarks, separators and submenu arrows. Literal
Ctrl/F-key captions come from the native command model; classic guest menus keep
their original command-key glyph path. Borrowed native application icons are
composited into the MDEF's reserved icon column without transferring ownership.
Windows still supplies popup borders, shadow and tracking. The custom Label
popup, item content and dialog controls retain native painting. The GDI fallback
remains available when no initialized renderer is attached. Compact/modal Toolbox
frame variants are not replaced by this module.

The portable implementation and canonical header now live in `classic-ui`.
ROMAW and Finder link the same archive; Finder no longer embeds a second copy.
The old ROMAW header path forwards to the package for source compatibility.

Run `bash system/tests/check-ui.sh classic-ui` for the portable C renderer
under Linux ASan/UBSan. Native Finder tests also compile the renderer test for
Windows. The menu-banner and window-resize PowerShell probes exercise real
classic applications through their existing debugger entry points.

After building, run `bash system/tests/check-graphics.sh quickdraw-host` for real
Toolbox rendering, guard/stride checks, wide offscreen surfaces, resource-pattern
comparison, WDEF hits, thread rejection and native presentation checks. Override
`ROMAW_TEST_OBJ` and `ROMAW_TEST_EXE` for an isolated build. The presentation
probe compares every scrollbar gutter pixel with the manager output and checks
that content pixels stay untouched. It also dispatches real WM_MEASUREITEM and
WM_DRAWITEM callbacks, compares complete menu rows with direct MDEF output, and
guards pixels outside the supplied row. The host probe checks selection
inversion, disabled-state handling, shortcut captions, marks and arrows.

The aggregate graphics checks use debugger-only DLL probes at the initialized
Finder entry point. They do not require a public native application SDK or a
product module-launch option.
