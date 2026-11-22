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
errors and proceeds to a private ScriptUtil call (`0x8006ffae`, with a short and
a pointer), which currently opens the fatal unknown-selector dialog. It has not
entered a validated desktop event loop. Some failure-unwind runs also expose a host heap error during PEF cleanup;
that failure-path issue remains unresolved.

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
A working desktop, file browsing, copying and application launching have not
been validated. Native service tests establish their listed contracts, not full
Finder compatibility.

## Validation

The root Windows cross-build completed and staged the optimized executable and
matching dependencies. The command-line help regression passed all three forms:

```powershell
powershell.exe -NoProfile -File system/tests/check-runtime.ps1 help -ROMAW D:/Source/ROMAW/windows-binaries/bin/ROMAW.exe
```

The changed command-line table passed clang-format 18.1.8's read-only check.
Actual Windows launches accepted `-ppc` and reproduced the failures above.
The generated-file suite subsequently passed with the selected optimized object
directory. Native Memory Manager and SOM/CFM reporting fixtures passed on Windows
in both interpreter and JIT modes. Handle-copy checks, including locked
`PtrToXHand` shrink/grow operations, also passed in both modes. The Appearance
font fixture verifies default and explicit scripts, live font-size changes,
big-endian unaligned output and missing-output guards in both modes.

The adapter validation build freezes System resource inputs and their matching
generators at committed revision `22fbf3d9`. Concurrent resource conversions in the
shared tree otherwise prevented generation or triggered the existing numeric
compatibility-revision check before PPC execution. Those unrelated edits are
preserved. These service checks do not establish a working Finder desktop.
