# Running ROMAW

Windows x64 is the supported host; see [the build guide](../README.windows-build)
for installation and diagnostics. Launching without operands opens the native
[Finder](../finder/README.md). An application operand starts a classic app;
quitting it exits that process. Finder launches apps in separate processes.
The removed `-browser`/`-nobrowser` switches and Startup/Browser preference keys
are unsupported.

The default reported Macintosh system version is 7.1.1. `-system` or an
application's `SystemVersion` setting can select another version; changing the
reported version does not implement its missing services. PPC-only flat `pwpc`
CFM apps use the PowerPC runtime and 32-bit addressing. Fat applications select
their supported PowerPC application fragment; applications without one retain
their 68K entry path.
See [synppc](../synppc/README.md) and [application notes](README.md#application-investigations)
for limits and required local libraries. Proprietary runtime files are not distributed.

## Window size and display scale

Window sizes are remembered in the application type/creator section of the
profile `ROMAW.prefs`, alongside the other application settings. Sizes describe
the inner/client area, excluding the Windows title bar and borders. Dragging the
host window border saves the new size immediately; restarting that app restores
it. Desktop resizing preserves the application's Macintosh window dimensions.
Full-screen dimensions are not saved as window preferences. Missing, malformed,
or out-of-range preferences are ignored; failure to write the file is nonfatal.

Override the saved size for a run:

    ROMAW.exe -window-size 800x600 "Apps\Coloring Book 3.2"

The existing -size option also works. Explicit sizes
override saved preferences and become the application's latest size. Supported
client sizes range from 512x342 to 4096x4096, subject to the display/memory mode.
If both are specified, -window-size takes precedence over -size.

Lock the client area to a specified size and disable border resizing and
maximizing for this run:

    ROMAW.exe -fixed-window-size 800x600 "Apps\Coloring Book 3.2"

This overrides saved dimensions
and the other size options, uses the same 512x342 minimum, and cannot be
combined with fullscreen. The dimensions are remembered in ROMAW.prefs;
the fixed/resizable setting is not persisted, so a subsequent normal launch
is resizable again. Macintosh application windows keep their normal behavior.

Run system/tests/check-ui.sh window-prefs from WSL to verify per-application storage,
reload, size bounds, and malformed preferences.
Run system/tests/check-ui.ps1 window-size-options in PowerShell to verify invalid
command-line dimensions fail before video initialization.

Scale the displayed pixels without changing the Macintosh resolution:

    ROMAW.exe -display-scale 150 "ROMAW:Kid Pix Studio Deluxe:Studio Stuff:Kid Pix"
    ROMAW.exe -display-scale 200 -window-size 800x600 "Apps\Coloring Book 3.2"

The percentage range is 25..1600: 100 is 1x, 150 is 1.5x, and 200 is 2x.
This overrides the saved DisplayScale at application launch for this process.
The switch itself does not save the scale; application preferences can still
change it during the run. A scaled surface must also fit the host size limits.
Run system/tests/check-ui.ps1 display-scaling and system/tests/check-ui.ps1 display-scale-options
to verify startup scaling, preference precedence and invalid options.

## Application architecture and system version

FAT applications prefer PowerPC by default. In Preferences → Advanced, select
**Prefer m68k for FAT apps** and click **Save** to use the 68K entry on the next
launch. The equivalent app preference is `PreferM68K=1`. A single-architecture
application still uses its available entry. The command-line `-ppc` option
takes precedence over the saved preference.

Finder releases 8.6 through 9.2.2 supply a known minimum OS contract through their
own version resource. ROMAW applies that requirement before Finder executes.
Other applications' release numbers are not treated as OS requirements.

During startup, an explicit Alert or StandardAlert message such as "requires
System 9.0.1" or "requires Mac OS 8.6" can select that required version in a fresh
process. ParamText substitutions are included. The supported automatic range is
System 6 through Mac OS 9.2.2; already sufficient versions are retained. Document
operands, mount modes and other launch options survive the restart. An application
already processing events keeps control of its own messages and is not replayed.
A plain Gestalt version query does not itself establish a minimum requirement.

Successful PPC runs save the selected version with their memory settings in the
application's existing preference section. Failed launches do not save a winning
profile. Run `synppc/tests/check-runtime.ps1 system-request` to check explicit
requests, Finder's known minimum and the event-loop guard.

## Memory

Choose **ROMAW Prefs** from the Apple menu of a running Mac app or press
Alt-Shift-5. Save writes the current application's section in
the profile `ROMAW.prefs`; MyABCD uses `[APPL.FP98]`.

The Memory page accepts bytes or k/m suffixes. Equivalent application preference keys are:

```ini
SysZone=8m
ApplZone=16m
```

These settings apply on the next application launch; active heaps are never
resized. A replacement ROMAW process starts with the requested sizes while
preserving command-line options and document operands. Explicit -syszone,
-applzone or -memory values take precedence. Blank/zero uses startup defaults.
On Windows, automatic defaults are 64 MB application/8 MB system for 32-bit
applications, and up to 8 MB application/2 MB system for 24-bit applications.
The 24-bit application default shrinks as needed to keep the actual display,
stack and reserved callback window below 16 MB. ROMAW
reads the application's SIZE flags before selecting the larger partition;
explicit addressing-mode options and application memory preferences still apply.
The minimum system heap is 128k; the minimum application heap is 512k. Values
are rounded up to 8192-byte boundaries and the total must fit the address space.

Focused checks: system/tests/check-runtime.sh ecf-unit,
system/tests/check-runtime.ps1 ecf and system/tests/check-runtime.sh sdl-preferences.
The SDL test is an isolated Linux harness using SDL 1.2 (or sdl12-compat),
pkg-config and dummy video.

## File dialogs and printing

Standard File open/save dialogs use ROMAW's classic Macintosh picker, including
custom controls, type/creator filters, guest callbacks, mounted volumes and
remembered directories. The Desktop button shows mounted volumes and the System
desktop folder when available. Resize using the bottom-right size box; position
and size are saved separately per app as `OpenDialogBounds` and `SaveDialogBounds`
(`widthxheight@x,y`), constrained to the current screen. Cancellation retains
geometry without creating a file. Finder's disk-image picker uses a host dialog.

Print and Page Setup use Windows common dialogs. QuickDraw renders monochrome
printer pages for native GDI spooling; Ghostscript is not required. Page size,
orientation, margins, ranges and driver copies follow the selected printer.
Cancellation preserves the print record and submits no job. PostScript-only
drawing comments are not interpreted by the raster path.

Relevant checks are `bash system/tests/check-ui.sh native-dialog` (intercepted
Windows APIs), `bash system/tests/check-runtime.sh robustness` (file replies
and print records).
These fixtures do not establish physical printer compatibility.

## Mouse input

The mouse wheel operates standard Macintosh scrollbars in the front window when
the pointer is inside its content area. Each notch performs one arrow action;
hold Shift to use a horizontal scrollbar. Custom-drawn scrolling controls need
application-specific support. Wheel releases and extra mouse buttons do not
become ordinary clicks. Wheel requests are bounded and dispatched at event-trap
boundaries, keeping guest callbacks out of SDL interrupt handling.

Right-click sends a Macintosh Control-click by default. Applications that support
that gesture can open their contextual menus; ROMAW does not invent menus for
older applications. An explicit RightButtonModifier preference still takes
precedence (0 restores ordinary-click behavior; 2048 selects Option-click).

Mouse geometry/sanitizer tests: bash system/tests/check-ui.sh mouse-input.
The Windows SDL-to-Toolbox probe and its Retro68 build instructions are in
system/tests/mouse-input/README.md. It checks callbacks, return-value tracking,
horizontal scrolling, cursor restoration, right-click/default and configured
modifiers, ignored auxiliary buttons and apps that never request mouse-up.

## File type mappings

The installed `ROMAW.ftm.master`, built from `system/ROMAW.ftm`, seeds
`ROMAW.ftm` in the user profile. Installation settings and master templates seed missing profile files.

```text
extension,,TYPE,CREA,flags,flags,description
```

The extension is case insensitive; an optional initial dot is accepted. Type
and creator are case-sensitive Macintosh four-character codes; shorter codes
are padded on the right with spaces. The remaining fields are retained for
compatibility with the starter database but do not perform format conversion.
Lines starting with # or ; are comments. Later duplicate extensions win.
Existing explicitly configured suffix mappings take precedence over this file.

Mappings infer metadata for files without Macintosh metadata, including legacy
empty TEXT/UNIX sidecars. Explicit nondefault Macintosh type/creator metadata
is preserved. Installed Macintosh applications under Apps are indexed by
creator, with FREF type fallback. ROMAW Finder uses that index to open
documents and obtain their associated document icons from BNDL resources.
Direct command-line document operands use the same application lookup.

## Editable System resources

PRAM and editable System resource overrides share the Macintosh file
System Folder:Preferences:ROMAW. Its data fork contains the classic parameter
RAM record; its resource fork contains System overrides. Its type is rsrc and
creator RSED, so ResEdit can edit the resources without changing PRAM. On host
filesystems the resource fork and Finder information use the adjacent ._ROMAW
sidecar. ROMAW's file operations preserve both forks.

The location follows the configured System Folder. Restart ROMAW after editing
resources. Unchanged embedded defaults and installed font suitcases are not copied
into this file. ExDl resource ID -32768 holds six-byte type/ID pairs for explicitly
deleted defaults; removing an ordinary override restores its embedded default.

Only this combined file is read and written. Older standalone parameter/resource
files and INI-encoded System resources are unsupported. Existing data and resource
forks are preserved independently; unreadable resource data is not overwritten.

## Application preferences

ROMAW.prefs.master is the checked-in source of first-start defaults.
The build stages it beside ROMAW.exe. ROMAW copies it to ROMAW.prefs
only when that file does not exist; existing user preferences are never replaced.
Maintain the master directly. Standalone ECF files, imports, numbered Config
records and legacy App/Creator headings are no longer supported. If the master
is absent, first startup uses built-in defaults.

Application sections use `[<type>.<creator>]`, for example `[APPL.8BIM]` or
`[APPL.ttxt]`. Each four-character component uses printable ASCII except brackets;
otherwise that component uses 0x followed by eight hexadecimal digits, for
example [APPL.0x005bff20]. Section names are case sensitive. Optional [Defaults]
settings are applied before the current application's section.

Settings are ordinary editable INI keys:

```ini
[APPL.8BIM]
Size=1162x858
Location=1485,47
Comments="Photoshop configuration"
BitsPerPixel=8
ScreenLocation=0,0
SystemVersion=7.0.0
BlitInOSEvent=1
SoundOff=1
PretendHelp=1
```

Option flags accept 0 or 1. The first flag in a section starts a fresh option
list; Options=0 explicitly clears it. Zero heap/depth values mean default and
are omitted when saving. Other explicit zeros are retained to override inherited
or nonzero defaults. Conditional CRC/version blocks and specialized expressions
use ordered Rule000000, Rule000001, etc. values with the existing expression
syntax. Gestalt overrides are direct statements, for example
Gestalt('fs  ')={0,1}; (the pair is the returned error and response). A selector
that cannot be written as four printable characters can use
Gestalt(0x00667320)={0,1};. Saving the configuration dialog preserves these
hand-written Gestalt entries. These remain in execution order with ordinary
assignments.

The configuration dialog's Save button writes these keys directly to the shared
application section while preserving window geometry and unrelated sections.
Command-line memory options continue to override saved heap sizes.

Run bash system/tests/check-runtime.sh config-prefs and
bash system/tests/check-runtime.sh ecf-unit in WSL, then
./system/tests/check-runtime.ps1 ecf in PowerShell for isolated runtime probes.

## Profile and host volumes

Mutable preferences, file type mappings, Apps and caches live in
`%USERPROFILE%/.romaw`. The profile Apps folder
is mounted as `ROMAW:` inside the emulator. Installed master prefs/mapping files
seed a new profile; populated profiles are preserved. Native Finder preferences
share ROMAW.prefs. Rebuildable BNDL ICNS files and icon_cache.idx live in cache/.
On first use, `.romaw` is created directly. Startup does not discover or import
other profile directories. Explicit absolute paths in settings remain unchanged.
See [the build guide](../README.windows-build) for installation assets and profile staging.

Windows host drives default to DOS volumes. On the first graphical startup,
when [HostDrives] SidecarDrives is absent or empty, a checkbox dialog selects
the drive letters that support Macintosh sidecars. Save records, for example,
SidecarDrives=M,S,Y in the profile ROMAW.prefs. Saving with no drives checked
records none, which avoids prompting again. Preferences > General > Drive
sidecars reopens the selection; clearing the value also requests setup again.
The same dialog's Add folder / Remove controls assign individual directories as
Mac compatible. They are stored as MacDirectoryCount and MacDirectory1,
MacDirectory2, etc. in [HostDrives]. Each configured directory and its children
support sidecars; the directory mounts as a separate volume at the next start.
Its containing drive remains DOS unless separately checked. Folder matching
uses path-component boundaries and resolves . and .. components.
Unselected and newly connected drive letters remain DOS volumes. UNC paths
without a selected mapped drive letter also default to DOS.

DOS volumes advertise no desktop items and cannot create, delete or rebuild
Finder Desktop, Desktop DB or Desktop DF files. Selected host volumes use the
Desktop Manager database; mounted HFS images retain their own desktop support.
The mapped profile Apps folder remains sidecar-capable even when its containing
Windows drive is DOS. Existing sidecars and directory conventions are detected
automatically; unmarked directories use OS X `._name` by default. `-netatalk`
or `-afpd` selects `.AppleDouble/name` as that fallback, `-basiliskii` selects
split `.finf/name` and `.rsrc/name` companions, and `-executor` selects `%name`.
These guest File Manager defaults do not override native Finder's destination
storage convention. See
[Macintosh sidecar files](../README.md#macintosh-sidecar-files) for the supported
layouts and command-line settings. Drive selection concerns host volumes;
it does not make ISO or compressed images writable.

Host drive and mapped-folder volume names can be changed in Finder. Names are
saved in [HostVolumeNames], keyed by the normalized host root (for example,
C:/=Macintosh HD). Special key characters are percent-escaped. The displayed
name is restored on subsequent mounts; the Windows volume label, folder name
and file contents are unchanged. Native C:/ paths, C:Macintosh:Paths and
historical C/: aliases continue to work after renaming. Image-volume renaming
continues through the image's own File Manager implementation.

Run bash system/tests/check-files.sh drive-prefs and
./system/tests/check-files.ps1 drive-volume for selection/persistence and staged
volume/desktop checks. The latter uses isolated temporary host volumes and
does not change the user's drive selection.
