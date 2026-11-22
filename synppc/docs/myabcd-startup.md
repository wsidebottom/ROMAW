# MyABCD / CarbonLib startup investigation

Observed with the local CarbonLib 1.4 and Mac OS 9.2.1 libraries, using the staged
Windows ROMAW with `-system 9.2 -applzone 16m`. These are ignored local runtime
assets; this project does not distribute them.

## Current boundary

The current build completes Carbon initialization, reads its embedded SWF,
creates and shows a color movie window, hides the menu bar for fullscreen mode
and services the application's timer callbacks. The registered application
renders its main screen, loads external activity movies and responds to
navigation. Manual checks include alphabet and number animations, a balloon
game, string instruments and Help. The user confirmed improved animation speed.
These checks do not establish every activity or complete MyABCD compatibility.

The embedded movie has ten frames at ten frames per second. Its first frame
runs fullscreen/menu setup and loads initialization variables. A later frame
requests loader.swf or register.swf. Tracing has seen user.ini, register.swf and music/123theme2.swf opened.
An earlier captured failure called a NULL weak font lookup import from the
application at LR 0x6243C8 (FMGetFontFamilyFromName). The native lookup now
uses installed resource fonts, reports missing families as invalid, and preserves
ResLoad. GetAppFont now forwards to the existing application font setting.
The original Appearance Probe checks these paths.

ATSUnicodeLib then required ATSSupport:identityMatrix. The matching ATSSupport
fragment was extracted from the user-provided Mac OS 9.2.1 System cfrg and
installed with an AppleDouble resource fork under local Extensions. It remains
an ignored runtime asset. Preserve the application's original
registration/startup logic throughout compatibility work.

## Earlier selector 0x005A checkpoint

The following private-selector findings describe an earlier, resolved startup
blocker. Later timer, file-loading and drawing follow-ups are recorded below.

The call uses ProcInfo `0x03F79FBE`: stack-dispatched Pascal, a 16-bit selector,
32-bit result and nine arguments with widths `4,4,1,2,4,1,4,4,4`. In the observed
call those arguments are a 20-by-20 rectangle, empty Pascal title, false, zero,
minus one, false, zero, a routine descriptor and zero. The final argument is a region handle (verified below). The native routine returns
the created window pointer, or zero when its floating layer cannot be created.

The routine descriptor has ProcInfo `0x00003BE0`: a 16-bit result and arguments
with widths `4,4,2,4`. Its PPC code examines an EventRecord and writes through the
fourth argument. An event callback is a working interpretation, not an established
API declaration. The actual Process Manager callback site now confirms the
`0x00003BE0` ProcInfo and parameter order. It differs from the classic WDEF ABI
(`0x00003BB0`). An ordinary
NewWindow implementation that discards this callback is therefore insufficient.

## Reference findings

The user's MacOS921.cdr System file has cfrg entries for WindowsLib (data-fork
extent `0x221710`, length `0x26434`) and ProcessMgrLib (`0x5373F0`, length
`0x25070`). WindowsLib exports CreateNewWindow, window notification registration
and property APIs, among others. These names alone do not identify selector
`0x005A`; no name or implementation has been assumed from that similarity.
Reference extraction stays under the ignored build directory. No Apple code is
copied into synppc or installed as part of this investigation.

## Reproduce in Windows GDB

Load `windows-binaries/bin/ROMAW.exe`, set a working directory under `build`,
and use the following breakpoint before running the application:

```gdb
set substitute-path /mnt/d/Source/ROMAW D:/Source/ROMAW
break ppc_mixed_service if r->gpr[4] == 0x03f79fbe
run -system 9.2 -applzone 16m -debug trace "D:/Source/ROMAW/windows-binaries/bin/Apps/MyABCD/MyABCD"
p/x *r
x/8bx ROMlib_offset+r->gpr[6]
x/32bx ROMlib_offset+r->gpr[7]
x/12wx ROMlib_offset+r->gpr[1]+48
```

Here r5 holds the selector, r6 the rectangle and r7 the title. Remaining arguments
span the PPC registers and parameter area; inspect them according to ProcInfo
rather than treating each as a host pointer. Guest locations change with the
loaded library set and allocator state. Use guest-to-host translation only after
checking the guest extent.

## Project-split validation

Standalone CPU/PEF/formatted-I/O checks and all five staged ROMAW integration
probes passed after moving the runtime to synppc. The ROMAW build, generated
file tests and Windows help checks passed. Touching the public PPC header rebuilt
both the core and bridge and relinked/staged ROMAW. Core dependency files do
not reference ROMAW headers. MacWrite, Coloring Book and Browser reached their
event loops under Windows GDB; these are startup checks, not complete workflows.
MyABCD reproduced the controlled selector failure after the move.

## Reproducible diagnostic boundary

The unsupported-selector diagnostic now includes ProcInfo and the PPC return
address. Its trace records the decoded argument count and values without
reading through guest argument pointers. With the local MyABCD library set, the
observed caller is `0x0080989C` and all nine values match the earlier GDB capture.
This confirms transport through the Mixed Mode bridge; it does not implement
the private window service or establish its ownership/callback contract.

Run `synppc/tests/run-diagnostic-tests.ps1` in PowerShell after building. It uses
Windows GDB (override `-Gdb` if needed) and a generated original-code PPC fragment
with the same ProcInfo/selector and distinct register/stack argument values. The
fixture checks all nine decoded arguments, the caller/ABI diagnostic, absence of
Windows exceptions and controlled exit status 2. GDB suppresses the expected
native message box for this test only. No Apple libraries are required by the
probe. The five normal application integration probes also pass with this change.

The actual Mac OS 9.2.1 System resource catalogue and ProcessMgrLib fragment were
inspected further. ProcessMgrLib has no exported names to identify the selector,
and the inspected resources supplied no direct `0x005A` OSDispatch call stub.
The later data/transition-vector analysis below identifies the implementation
without relying on an exported API name. MyABCD remains unsupported at this boundary.

## Native system-window implementation identified

The unpacked Mac OS 9.2.1 ProcessMgrLib data contains a RoutineRecord at offset
`0x2274`: ProcInfo `0x00FDE7F0`, ISA 1, flags `0x000C`, transition vector `0xAE0`
and selector `0x005A`. The vector targets code offset `0x68F0` with TOC `0x8EC`.
The record's selector is its final field, not the word preceding ProcInfo.
The record flags select native execution and omit the selector from the target
arguments. The nine-argument target ProcInfo agrees with CarbonLib's dispatched
`0x03F79FBE` signature after removing its selector field.

Relocating imports with synthetic addresses (without executing Apple code)
identifies the creation routine's calls to NewFloatLayer, SwapCurLayer, NewPtr,
NewCWindow, SetCurLayer and HiliteWindow. It allocates `0xC6` bytes of window
storage, passes that storage and the seven classic window arguments to NewCWindow,
sets windowKind to 5, and records owning-process and emulated A5 context.
Additional fields include the event callback at `+0xB2`, saved A5 at `+0xB6`,
and the ninth argument at `+0xBA`. Two further private fields are zeroed.

The event-callback call site at code offset `0x19DB4` loads `+0xB2` and invokes
CallUniversalProc with ProcInfo `0x3BE0`, passing the window, event pointer,
16-bit part value and fourth callback argument. It restores the owner's emulated
A5 before calling. A different-owner path marshals an interprocess invocation.
Code at `0x1B020` loads `+0xBA` and supplies it to PtInRgn, identifying the ninth
creation argument as a region handle. No clone or disposal of that handle was
observed in the creation/destruction routines inspected.

Selector `0x005B` maps to transition vector `0xAE8`, code offset `0x6A8C`. It
calls DisposeWindow, then disposes the shared floating layer when it becomes empty.
These operations explain why returning a plain application window and forgetting
the callback/owner/layer would be incomplete. The public/private API name is still
unconfirmed. No guessed Process Manager structures or replacement Apple code have
been introduced; the existing controlled failure remains in place.

## Dispatched-descriptor routing fix

Inspection exposed a bridge bug independent of the missing window service:
CallUniversalProc previously selected the first PPC entry and passed its selector
to the target as an ordinary argument. It now selects an exact or default native
record in a bounded non-indexable descriptor and honors kDontPassSelector and
relative entry points. The supported records use prepared Pascal signatures;
signature mismatches, unprepared fragments, indexed layouts, excessive record
counts and invalid entry vectors produce controlled diagnostics.

The original-code PPC Dispatch Probe reproduces the old failure (the first entry
returned 11 when the selected entry should return 42), then verifies stack/D0/D1
selection, default selection, relative vectors, signed narrow values and all nine
register/stack arguments. All six integration probes and seven controlled-failure
cases pass. Standalone CPU/PEF and Windows help checks pass. MyABCD still reports
selector `0x005A` at caller `0x0080989C`; it has not reached a usable window.

The descriptor flag meanings follow Apple's [Mixed Mode Manager reference](https://leopard-adc.pepas.com/documentation/mac/PPCSoftware/PPCSoftware-39.html).

## Current checkpoint (2026-10-19)

The selector-0x005A/0x005B system-window service now creates, dispatches callbacks
for and disposes the extended kind-5 window in a shared floating layer. Layers
retain distinct window and auxiliary lists. The owning process supplies its real
classic identity prefix; the rest of the private Process Manager ABI remains
incomplete. Component registration and instance lifetime, explicit toolbox
notifications, TSM process registration, MoveHLow, guest C allocations and current
CFM dependency-closure metadata have original-code regression fixtures. The nine
staged PPC probes passed before formatting.

The local runtime additionally contains the original ToolboxUtils and Collections
fragments extracted from the user's Mac OS 9.2.1 System file. They remain ignored
local assets. CollectionsLib forwarders resolve to the actual Collections code.
CFM prerequisites without pending INIT resources initialize before extension
INITs. Direct 68K AAFE entry now invokes prepared PPC descriptors, including the
real ToolboxUtils AA77 notification dispatcher installed during initialization.

MyABCD still does not run successfully: CarbonLib's initializer returns -2821
after its 68K INIT executes through that dispatcher. The ToolboxUtils context and
bootstrap interaction needs further investigation. No initializer is bypassed,
and no replacement private state is supplied to turn that failure into success.
The earlier selector-0x005A failure descriptions above record prior checkpoints.

## ExpandMem and event initialization follow-up (2026-10-19)

A hardware watchpoint traced error -2821 to CarbonLib's check of
ExpandMem.emProcessMgrExists, at CarbonLib code offset 0x60D70. The flag was
initially present, and the ToolboxUtils clqu context version was already 8.
ROMAW had represented the record header as zero-filled padding, including
emSize. When the real library extended ExpandMem it therefore copied zero bytes,
losing that flag and the AppleEvent tables. Publishing the allocation's actual
596-byte extent fixes the copy. The partial record keeps emVersion zero. The
layout follows the user's System 7.1 Internal/C/ExpandMemPriv.h: emSize at 2,
emProcessMgrExists at 0x128, and AppleEvent tables at 340 and 380.

CarbonLib next required GetSystemClientProcess and Begin/EndSystemMode. These
now use the actual process serial number and a per-process nesting counter,
following ProcessMgr/OSDispatch.c in the user's source. A CFM initializer can
find its own already linked connection; a separate initializing state prevents
recursive initialization while preserving failed initializer errors.

The next null call was the weak InterfaceLib UpTime import reached from
GetCurrentEventTime. UpTime, AbsoluteToNanoseconds and Microseconds now share the
interpreter's monotonic 25 MHz time-base clock. The classic PPC ABI returns
UnsignedWide structures through a hidden output pointer, as confirmed by the
actual caller. The original-code probe checks conversion across a 32-bit boundary
and monotonic readings. LMGetKeyMap returns the actual low-memory key bitmap.
The front-process query uses the existing single-application process identity.
WakeUpProcess now records the real process wake request, and native
WaitNextEvent consumes it before sleeping. The probe checks a long sleep exits
promptly after a wake request.

With no open TSM documents, TSMEvent leaves the event unconsumed and
TSMGetActiveDocument returns NULL. The former matches the invalid-document path
in the user's TSMFunctions.c; the latter matches Apple's TextServices.h contract.
The Component Probe exercises their actual 68K stack result layouts.

The real TextServices trap fragment identifies selector 0x001D as
GetUseInputWindowState. For NULL documents it returns the actual process
registration state: registered applications default to inline input, unregistered
ones to a floating input window. Selector 0x0020 is GetDefaultInputMethodInfo;
the original implementation clears two four-byte outputs before querying an
input method. The supported Roman-script case has none and returns false.
Other scripts/documents remain unsupported.

CarbonLib initialization now completes. MyABCD enters its own menu/event setup.
Consumed high-level messages remain owned while Carbon exchanges the current
message context through OSDispatch 0x007C; peeking never disposes queued messages.
The Linux HLE lifetime fixture checks these paths under ASan/UBSan.

The current application thread supports critical sections, current-thread queries
and yields; creating worker threads is not implemented. MathLib exception flags
use the emulated FPSCR. Nested callback errors leave native event waits promptly.
AppleEvent imports use existing handlers; removal and PurgeSpace outputs now
preserve guest byte order. Menu icon-handle queries distinguish attached handles
from classic resource IDs, and theme metrics preserve the current graphics port.

MyABCD's resource MDEF is an unprepared PowerPC fragment. The bridge now prepares
its bounded resource PEF, links dependencies, runs its initializer and retains the
prepared vector for subsequent calls. The original-code Resource Probe verifies
initialization and repeated calls, and actual DeferUserFn callbacks execute with
the documented A0 argument in this resident-memory runtime.

The projector's startup trailer read exposed a separate file I/O ABI bug:
FSRead/FSWrite and scalar native helper outputs use host byte order, while PPC
callers supply guest-endian memory. The bridge now uses encoded parameter blocks
and local outputs for FSOpen, read/write counts, GetEOF and GetFPos. Transfers
validate buffer extents and reject negative counts. MyABCD now reads its embedded
startup SWF and creates a window, but that window remains hidden; startup timer
and event delivery still need investigation.

GetAuxMenuItem reports the unsupported auxiliary-record query with unimpErr,
without modifying the output. The original MDEF retains its classic fields on
that error. Classic modifier, key-glyph, font and text-encoding queries report
no extended overrides. No private record is fabricated.

Fourteen staged PPC probes passed, including exact and partial EOF reads, guarded
buffers, write/read-back, scalar byte order and errors. Eleven diagnostic cases
passed, including a resource PEF extent outside its allocation. This remains an
unfinished compatibility investigation, not a working MyABCD application.

## Event, drawing and input follow-up (2026-10-19)

Native PPC event bindings now honor installed 68K patches for WaitNextEvent,
GetNextEvent and EventAvail. CarbonLib's own wait wrapper is retained. Bypassing
these patches prevented the event integration from running. An original Carbon
Wait Probe verifies wrapper forwarding and explicit 68K patch output markers.

CarbonLib NewWindow produces a CGrafPort as required by the color window-port
accessors. Returning a monochrome port made the projector dereference screen
pixels as a PixMapHandle and pass NULL to CopyBits. The classic InterfaceLib
entry is unchanged. Menu visibility changes update actual desktop/window region
geometry and preserve the prior height; cleanup restores the geometry.

Classic theme menu drawing invokes content callbacks, preserves clip/pen/colors
and paints selected backgrounds and separators. Keyboard focus queries report no
focused classic control. FindControlUnderMouse handles the runtime's flat control
list, retaining a disabled control's handle with part zero. Extended focus and
embedded controls are not implemented. The arrow theme cursor is implemented;
other theme cursor images remain unsupported.

MacRoman lowercasing covers all byte values without the host locale. KeyTranslate
adapts the native dead-key state to guest byte order. Roman character byte types
and the no-current-document Unicode query are bridged. UpTime uses an elapsed
clock relative to interpreter startup.

The supplied Appearance SDK's MenuDrawing.c confirms callback arguments; the
CarbonLib 1.6GM SDK supplies Controls.h, Appearance.h and MacWindows.h signatures.
These are references only, not newly installed runtime replacements.

Sixteen staged PPC probes pass, including real menu pixels, callback state,
control hits, color window ports, menu geometry, all MacRoman values, dead-key
sequences and patch-aware event forwarding. A black movie window remains a
partial result, not successful application playback.

The font lookup contract is described in the supplied SDK Fonts.h/ATSTypes.h
and [Apple's Font Manager reference](https://leopard-adc.pepas.com/documentation/Carbon/Reference/Font_Manager/DeprecationAppendix/AppendixADeprecatedAPI.html#//apple_ref/c/func/FMGetFontFamilyFromName).

## Timer, PCM and input follow-up (2026-10-20)

Native WaitNextEvent now invokes an installed EventAvail hook while polling.
CarbonLib services its event-loop timers through this hook; bypassing it left
the projector's timers installed but unfired. The original Carbon Wait Probe
checks the hook's side effect. Failing PPC callbacks unwind immediately from
either event hook. A local diagnostic using the actual installed CarbonLib now
receives three repeating timer callbacks and exits normally.

GetOverrideMap follows the actual resource-map override runs, including the
NULL-input error. GetPageableSystemZone uses the system heap, matching the
original MemoryMgr fallback when no separate pageable heap is configured.
RegisterAppearanceClient and UnregisterAppearanceClient are paired in the
classic backend, which owns no theme resources. Original probes cover these
contracts. The GlyphsCache fragment from the same local Mac OS 9.2.1 System
image is installed with its fork as an ignored runtime asset.

MyABCD streams 16-bit stereo extended PCM buffers. Previously they were dropped,
and callbacks repeatedly queued replacements without advancing audio time.
Extended 8/16-bit mono/stereo PCM now converts to the existing mono 8-bit mixer.
The original PPC Sound Probe checks real queue callbacks and playback duration;
bounded conversion checks run with ASan/UBSan.

Hidden system windows must not consume the application's mouse input. Mouse
callbacks now target the hit system window and receive the actual part code.
ShowHide selects the window's owning layer for auxiliary-window lookup, then
restores the caller's layer. The Layer Probe covers hidden/visible mouse
delivery, part values and layer restoration.

The trial screen, intro, main activity screen and alphabet activity have now
rendered. Trial text still has dark colors. Ordinary Start and alphabet-tile
clicks advance; automated clicks immediately after pointer movement can initially
focus the target.
The user identified the separate KCHR assertion as a volume-key press. SDL
unmapped/media keys now bypass guest translation, and native keyboard resource
loading bypasses guest Resource Manager patches. These paths have focused
regression fixtures. This remains an unfinished compatibility investigation;
no completion commit has been made.

Explicit PPC UniversalProc callbacks now reserve an aligned dispatcher frame on
the caller's stack. Native/68K callbacks reuse independent 256 KB system-heap
stacks, with bounded recursion and controlled allocation failures. Original
fixtures cover application-heap pressure, backchains, frame guards and native
file completions; invalid-stack and recursion fixtures check controlled exits.
The later allocation failure was a fourth callback stack exhausting the default
2 MB system heap. Use `-syszone 8m -applzone 16m -system 9.2` for this diagnostic
application and its matching local Mac OS 9.2.1 libraries.

An independent offscreen QuickDraw defect is fixed: 16/32-bit port foreground and
background pixels follow the port depth when SetPort leaves an indexed GDevice
selected. GetCPixel also supports direct pixels. The Appearance Probe draws and
reads yellow text on white in screen and 8/16/32-bit offscreen ports. MyABCD's
remaining dark text uses an 8-bit port, so this is not a demonstrated fix for it.
An experimental SDL 32-bit display path produced a black application window and
was removed; the maintained SDL display backend remains indexed.

The staged build passes all seventeen positive PPC probes and fourteen controlled
diagnostic cases after these changes. Both formatter checks and git diff --check
pass. Debug processes were closed. These checks do not establish full MyABCD
compatibility. The user authorized a checkpoint commit before continuing this
investigation; the completion commit remains pending.

## Colored text follow-up

The trial-text problem is now fixed in the Windows 8-bit blitter. It previously
mapped a source mask to the foreground palette index before applying bitwise OR
to the destination index: yellow index 5 over blue index 212 became dark index
213. The corrected path uses the existing portable blitter's mask truth tables,
so glyph pixels become foreground index 5 and untouched pixels retain their
background. XOR ignores the drawing colors, and BIC applies the background
through the source mask. This also matches the color transfer semantics in
[Inside Macintosh's CopyBits reference](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-166.html).

GetCPixel had a second problem: it copied through CopyBits using the current
drawing colors, which could recolor the sample. It now reads the stored pixel
without drawing or changing the port. Original native fixtures check the eight
source modes, clipped spans, origins, packed pixel positions, direct byte order,
bounds and non-device palette values. The original PPC Appearance Probe checks
yellow glyphs on white and blue in the screen window and 8/16/32-bit worlds.

MyABCD's trial title, body and Start label now have their intended visible colors.
Manual checks reached the game list, popped a letter balloon, returned through
the navigation controls and opened the violin/viola/cello activity. The diagnostic
process exited normally afterward. These are representative interaction checks;
they do not establish every activity, audio output or complete compatibility.

Validation: WSL build/staging, all seventeen PPC probes, the complete robustness
suite including the two new native fixtures, both formatter checks and
git diff --check passed.

## Playback performance follow-up

Sampling the running Windows process found most samples in PPC dispatch and
memory checks. The interpreter now builds with -O2, retaining debug symbols and
disabling strict-aliasing assumptions. A bounded ordinary-RAM span supplies
direct access without callback dispatch. The low-memory vector overlays and
reserved host-alias window remain on the checked path. Nested executions restore
the previous environment, including after a fault.

CPU/PEF tests, seventeen staged PPC probes and fourteen controlled diagnostics
pass. CPU coverage includes both memory paths, partial-span reads, invalid spans
and nested success/failure. A local memory-loop benchmark of eight million guest
instructions improved from about 50-53 ms to 32-33 ms. The user registered MyABCD
and confirmed better animation speed in the staged build; this does not claim
a measured movie frame rate. Enlarged bitmap text and live resizing are the
next display issues being investigated.

## Enlarged text follow-up

Windows draws uniformly enlarged Times, Helvetica and Courier runs using native
outline masks at the target pixel size. QuickDraw still determines character
origins, baseline, colors, clipping and pen advance. Exact-size bitmap fonts and
unsupported cases keep their existing renderer. The masks remain monochrome;
this avoids magnifying coarse bitmap strokes but does not add antialiasing.
The native font cache has twelve entries and is released during Font Manager
shutdown. Native tests check MacRoman glyphs, spacing, padding, rejected inputs
and GDI handle cleanup. The Appearance Probe checks enlarged yellow Times over
white/blue in screen and 8/16/32-bit offscreen worlds.

The saved large window renders the main-screen and Help text cleanly. Number
movies continued through several scenes and the Home control returned to the
main menu. Native maximization worked while playback continued: the Macintosh
canvas retained its size as required by ROMAW's desktop-resize behavior.
Live movie reflow is not implemented. WSL build/staging, seventeen PPC probes,
robustness, both formatter checks and git diff --check passed.

## x64 startup and translation overhead follow-up

The x64 GetPort bridge wrote a native eight-byte GrafPtr into a four-byte
guest output. MyABCD stores its saved clipping region immediately afterward;
GetPort cleared that handle and the following GetClip crashed. GetPort now uses
the existing packed guest-pointer implementation. The Windows Probe checks the
returned pointer and guards on both sides. It fails on the old build and passes
with the fix. A normal launch reached the alphabet activities again.

Sampling the slow JIT-enabled application found repeated compilation and
VirtualAlloc/VirtualProtect/VirtualFree calls. Filling the bounded block cache
previously discarded every warmed block. The cache now retains existing entries
and interprets untracked addresses once full. A CPU regression warms a block,
visits 512 cold blocks, then verifies immediate native reuse and the same guest
result as interpretation.

The user confirmed normal speed after disabling the JIT in the running process,
while noting that further acceleration is desirable. PPC interpretation is now
the normal launch default; -ppc-jit explicitly enables the experimental backend
and -no-ppc-jit takes precedence. Integration probes explicitly select JIT when
testing native execution.

## Persistent translation follow-up

The x64 backend now passes the current CPU and status scratch to native blocks,
removing embedded temporary CPU addresses. A launch-owned cache retains code
across callbacks, including nested timer callbacks. Each native entry has private
status scratch; code and compile-time metadata remain shared. Source changes at
the same guest PC invalidate the old block before entry.

`-ppc-aot` pretranslates supported runs from linked PEF code sections and saves
bounded per-application native artifacts in the user profile. A separate-process
fixture verifies that a second launch loads the saved code, prepares no new
application blocks and executes with zero runtime compilations. Header/build/ABI
checks, corruption/truncation fixtures and source-word checks cover rejection
and fallback. Regular interpretation remains the default, with `-no-ppc-jit`
overriding both native modes.

This remains partial AOT over the existing native instruction subset. Short or
unprepared entry points and unsupported instructions retain lazy translation or
interpretation. MyABCD reached its registered main screen and alphabet animation
with AOT enabled. A later process loaded 4,096 saved blocks. These checks do not
establish a playback speedup or every activity's correctness.

Validation: the full `synppc/tests/check-core.sh core` CPU/PEF/cache suite passed,
including 312,800 independent fused-FPSCR cases. All 29 integration probes passed
in each of AOT, JIT and interpreter modes. The cold/warm AOT frontend test,
staged Windows help and mode-precedence checks, and existing icon-label pixel
checks also passed. The main executable was staged without terminating existing
user sessions; those sessions retain their previous executable until restarted.
