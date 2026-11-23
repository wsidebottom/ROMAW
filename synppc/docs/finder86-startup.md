# Finder 8.6 startup

The user's `Apps/Desktop Folder/Finder 8.6` contains a classic `CODE` startup
stub and three PowerPC fragments: `Finder`, `FinderLib`, and
`SOMCustomMemoryMgr`. It has no CFM-68K application fragment. The default classic
startup reaches Code Fragment Manager selectors `0xffff` and `0x0044`, then
`SysError` before PowerPC preference was enabled by default. ROMAW's legacy system-error dialog displays the ARDI copyright
message here; that display does not establish successful Finder initialization.

ROMAW now prefers a supported PowerPC application fragment in every fat
application, including Finder 8.6, without a command-line override. Applications
without a PowerPC application fragment retain their 68K path. `-ppc` remains an
accepted explicit selection. The option's
consumer existed but its command-line declaration was missing, causing the
parser to reject it. The declaration is now restored. A diagnostic Windows
launch of the original application is:

```powershell
& D:/Source/ROMAW/windows-binaries/bin/ROMAW.exe -debug trace -system 8.6 -syszone 16m -applzone 64m "C:/Users/William/.romaw/Apps/Desktop Folder/Finder 8.6"
```

Run from a disposable diagnostic directory because tracing writes `stdout.txt`
and `stderr.txt`. These heap sizes are diagnostic settings, not established
minimum requirements or a saved compatibility profile.

The native [Memory Manager, SOM allocation and CFM notification services](native-runtime-libraries.md)
now provide the relevant runtime contracts without embedding Apple executable
bytes. Finder passes `FinderLib` initialization and reaches its preference
resource handling. The existing native `PtrToXHand` implementation is now
reachable through InterfaceLib, allowing preference resources to be resized and
written. `AppearanceLib:GetViewsFont` now resolves the configured script/font
settings through the existing native theme-font service. Finder proceeds through
font setup and offscreen GWorld creation. Native Appearance drawing-state
services now preserve actual QuickDraw color/pen state. With the additional
Toolbox queue bindings and native Help Manager error return, Finder enters menu
construction. Native MenusLib submenu state and menu visibility/enable bindings
allow it to proceed through special-folder discovery. The native icon-name
adapter allows registry and system-icon lookups. The Classic composition gateway
now reports invalid missing icons or an unavailable compositor, while retaining
a genuine original implementation when its required dispatch is installed. It
does not fabricate successful composition. Finder handles the missing badge
errors and continues through script setup, process discovery and desktop construction.
The private `KeyScriptEnable` selector (`0x8006ffae`) now validates its actual
short/pointer ABI and records the process's input-script restrictions, retaining
Roman as a fallback. `OSEventAvail`, timer callbacks, desktop-layer creation,
icon-device selection and system-heap queries use the existing Toolbox services.

Native Object Support initializes actual application/system accessor tables,
retains callback UPPs and refcons, and resolves nested object specifiers through
application callbacks. Application tables reset with the AppleEvent manager.
Whose/range expansion remains unsupported and returns an error. Optional volume
notifications, extended private memory accounting, native WindowShade, draggable
title proxies and missing theme sounds return explicit unavailable statuses.
They do not become successful initializers or fabricated services.

Fresh Windows fibers no longer inherit dynamic interpreter/JIT CPU scopes from
the launching fiber. Each suspended fiber restores its own scopes on resumption.
A deterministic Windows regression resumes a worker after the launching CPU has
been destroyed; it checks both interpreted and precompiled native execution.

Folder construction includes a native chasing-arrows control with real drawing,
idle animation and the one-byte `anim` property. Native window definitions report
their capabilities. Zoom transitions use the rectangle animator and actual
Window Manager visibility. Header drawing uses native QuickDraw state.

Windows validation reached the populated desktop, opened the ROMAW disk and
browsed System Folder contents. Closing the host window completed the PPC
fragment and exited with status zero. Normal launches also reached the desktop
with default settings. These checks do not establish complete Finder compatibility.

The launcher also reads Finder's own version resource and automatically applies
its known minimum system version before guest execution. It does not infer an
ordinary application's minimum OS from that application's release number.

## Reference dependency investigation

The following reference experiment established the original dependency gap,
before the native Memory Manager implementation was added.

An isolated copy under `build/finder86-startup/` was also tested with the user's
local System reference and `Y:/System 9 Folder/Extensions/SOMobjects™ for Mac OS`,
including their AppleDouble companions. The System reference identifies itself
as version 9.0; this is a dependency investigation, not a matching 8.6 system.
The original application, System reference, SOM runtime and installed extension
directory were not modified.

The System reference supplies a substantive `Memory Manager` implementation,
including `MMNewHeap`, in a nonzero `cfrg` resource. Automatic extension discovery
currently registers resource zero and the explicitly selected CarbonLib
DataBrowserLib member from resource one. It does not discover every System-file
fragment. For this isolated experiment only, an unmodified Memory Manager member
descriptor was exposed through resource zero of the copied System file.

The actual Memory Manager imports SOM allocator data exports, so supplying only
that fragment fails at `SOM:SOMFree`. Adding the supplied SOM runtime gets through
linking and into SOM's initializer. That initializer stops at the unsupported
`PrivateInterfaceLib:FragAddNotifyProc` service. Its observed arguments include a
callback transition vector and a second guest pointer. Fragment lifecycle
notification needs its real registration, invocation and ownership contract;
an unconditional success stub would not implement it.

Registering all System-file fragment descriptors instead also exposes dependencies
on boot-resident state, including `NQDResidentCursor:gNotifyRectList`. Importing
an entire System file therefore does not by itself supply a working runtime.

No initializer was skipped and no private boot structures were fabricated.
Copying and application launching have not been validated. Native service tests establish their listed contracts, not full
Finder compatibility.

## Validation

Build from the repository root with the optimized Windows profile and matching
CPU archives. After building, run:

```powershell
./synppc/tests/check-runtime.ps1 finder-services -ROMAW D:/Source/ROMAW/windows-binaries/bin/ROMAW.exe
./synppc/tests/run-romaw-tests.ps1 -Interpreter -Group "PPC Native Libraries Probe"
./synppc/tests/run-romaw-tests.ps1 -Jit -Group "PPC Native Libraries Probe"
```

The focused ScriptUtil GDB fixture, process-client checks and Windows-fiber scope
regression cover ABI, process isolation and CPU lifetime. The grouped Finder
fixture exercises actual native callbacks, descriptors, timer/event state,
chasing-arrows controls, window transitions, headers and optional errors in both
engines. The generated-file and formatting checks cover build freshness and
maintained source layout.

Manual Windows validation must separately exercise normal absolute-path and
PATH launches, desktop interaction, disk/folder browsing and normal host-window
close. Trace logs must be checked for `win_exception` and PPC launch failures.
Focused probes do not establish complete Finder, Carbon, physical CPU or MMU
compatibility.
