# Kid Pix compatibility checks

## Silly-idea number conversion

The PowerPC `StringToNum` import must store its longword result in guest byte
order. Its existing native helper returns host byte order, so the bridge now
converts a local result before copying exactly four bytes to guest memory.
The native helper and 68K calling convention are unchanged.

A live Windows stack and guest-code inspection traced Kid Pix's silly-idea
freeze to a recent-sound limit of `0x09000000` instead of 9, alongside a
byte-reversed sound-resource ID. The resulting exclusion loop repeatedly
called `Random` without returning to the event loop. Correcting those parsed
values let the current operation finish; subsequent calls required the bridge
fix rather than further debugger corrections.

The existing `PPC Decimal Probe` now checks eight Classic numeric-string cases,
including these values, signed limits, guest result bytes, adjacent guards and
`NumToString` round trips, alongside its existing MathLib checks.
The expanded probe failed on the old Windows executable and passed on the
rebuilt executable under both the interpreter and JIT. The 64 pattern-lifetime
scenarios also passed on that build.

## Pattern preview lifetime

Kid Pix disposes each temporary `GetPixPat` preview while the pattern is still
installed in its offscreen graphics port, then restores a bit pattern with
`PenPat`. Windows GDB traced the first invalid lifetime to `DisposPixPat` leaving
the port's handle intact. Subsequent allocations reused that master pointer;
later `PenPixPat` disposal followed stale subsidiary handles and invalidated the
new pattern before `PaintRect` faulted in `xdata_for_pixpat_with_space`.

`DisposPixPat` now clears matching pen, background and fill references in every
registered color port before freeing the pattern and its subsidiary handles.
Bit-pattern setters recreate cleared slots. This follows the documented
[Color QuickDraw pattern lifetime](https://mirrors.apple2.org.za/ftp.apple.asimov.net/documentation/macintosh/Mac%20-%20SpInside%20Mac%20-%20May92%20Text.pdf)
contract rather than suppressing the renderer's fault or retaining freed data.

`system/tests/check-graphics.ps1 pixpat-lifetime` checks repeated shared-pattern
disposal across 8-bit and 32-bit GWorlds, all three pattern slots, unrelated port
preservation, actual deallocation, replacement bit patterns and painted pixels.
All 64 scenarios passed on the rebuilt Windows executable, along with the
existing CopyBits colorization and staged GWorld output checks. Windows GDB
also validated all four color-pattern preview pages (IDs 1300 through 1313,
1200 through 1213, 1100 through 1113 and 1000 through 1013) and a selected
color-pattern canvas fill. This checks preview navigation and basic filling,
not the complete Kid Pix workflow.

## PowerPC Studio Deluxe startup

The installed suite launcher and `Studio Stuff/Kid Pix` contain PowerPC
fragments. Their startup previously stopped at missing InterfaceLib bindings:
`GetVol`, `GetWDInfo`, `PBHOpenDenySync`, application-name/menu-hook/window-paint
and sound-volume low-memory accessors, `AllowPurgePixels`, and
`LaunchApplication`. The bridge now exposes the existing managers and real
low-memory state. `SetVol` and `NoPurgePixels` expose the corresponding existing
operations. LaunchApplication validates its extended block and delegates to
NewLaunch, retaining the existing separate-process launchContinue behavior.

The volume regression also caught a second byte swap in PBGetWDInfo when a
volume reference selects the current directory. That branch now copies the
already guest-endian default directory ID unchanged.

Windows validation reached the suite selection screen, launched Kid Pix using
its button and reached the canvas. A separate direct launch produced a visible
pencil stroke. This checks startup, handoff and basic drawing, not complete
multimedia support.
The file, menu and GWorld probes passed under both PPC interpreter and JIT
execution. They check real volume/directory IDs and file references, errors,
low-memory widths/pointers, malformed launch blocks and pixel purge state.
Regenerate the file probe between runs because its existing RemoveResource
test deliberately detaches a resource from the fixture.

The built-in Sound Manager target remains 3.2.1. The PowerPC version API now
writes the four-byte NumVersion record through its hidden result pointer;
returning the scalar in r3 left Kid Pix's stack result uninitialized and caused
the extension warning despite the earlier version change. The 68K convention
is unchanged. The PPC sound probe checks both InterfaceLib and
PrivateInterfaceLib result bytes and adjacent guards before exercising queued
PCM playback and callbacks, under interpreter and JIT execution. The Carbon
service check uses the same structure-return convention. The rebuilt Windows
Kid Pix opens its canvas without the Sound Manager warning.

The Goodies menu also calls the installed MBarHook through a 68K JSR. Its
valid routine descriptor uses the special `0xCF` ProcInfo from
[Universal Interfaces Menus.h](https://www.ioi-xd.net/os9docs/Menus_8h_source.html).
The Mixed Mode entry now handles that specific ABI: a stack Rect pointer,
callee stack cleanup, and a signed short result in D0. Other unsupported
special conventions continue to report an error.

The menu regression executes an original 68K trampoline against a real PPC
descriptor three times, checking the Rect pointer and contents, signed result,
callback count and stack preservation. It reproduced the original rejection
under Windows GDB, then passed with the fix under interpreter and JIT execution.
The existing PPC callback probe passed in both modes too.
Windows validation also opened Goodies successfully in the rebuilt GDB session.

Cancelling the native job dialog opened implicitly by PrOpenDoc now sets
`iPrAbort` when returning a null print port. Previously it left PrError at zero,
so Kid Pix proceeded with a null graphics port and faulted in CopyBits. Explicit
PrJobDialog cancellation still returns false without setting a printing error;
actual host-dialog errors remain intact. The `print-cancel` Windows UI check
exercises both PPC engines, repeated cancellation, host failure and graphics-port
preservation, while forbidding raster output. The user's Windows retest returned
to the canvas after Cancel, and the debug session subsequently exited normally.

This records the native-dialog checkpoint below. Current Standard File behavior
uses the classic picker described in [the runtime guide](runtime-guide.md#file-dialogs-and-printing);
the observations below are historical test evidence.

## Pick A Movie file picker (2026-10-30)

Kid Pix Studio Deluxe's `Studio Stuff/Kid Pix` calls Image Compression Manager
trap `$AAA3`, selector `$44` (`CustomGetFilePreview`). Before the fix this reached
the unimplemented-trap fatal dialog. Windows GDB reproduced the exact selector;
the native CustomGetFile entry received one file type, dialog ID 2050, a null
file filter and the application's guest hook/reply pointers after dispatch.

The Image Compression dispatcher now routes `$43`/`$44` to the existing
StandardGetFile/CustomGetFile Pascal ABI. Selector definitions and signatures
were checked against Apple's ImageCompression.h in Universal Interfaces 3.4.1.
This supplies real file selection and cancellation through the platform dialog.
Native panels still omit guest dialog controls/hooks and QuickTime thumbnails.
Other Image Compression selectors are not implemented by this change.

A second failure occurred before Windows displayed its picker: the initial
filename contained only the requested directory and its trailing separator.
GetOpenFileNameW returned FNERR_INVALIDFILENAME (`$3002`). Open dialogs now append
`*.*` to that directory, retaining the requested path without passing a bare
directory as a filename. Save dialogs keep their suggested filename.

Manual validation with the rebuilt Windows ROMAW opened Pick A Movie in
`Kid Pix Studio Deluxe/Studio Stuff`. Cancel returned to the WackyBox movie
screen without a fatal error. This establishes picker opening/cancellation;
selection and playback of the application's original movies were not validated.
The built-in movie backend remains limited to its bounded raw-RGB format subset;
compressed video/audio, movie controllers and embedded previews remain incomplete.

`system/tests/check-ui.sh native-dialog` checks requested/remembered directory
arguments, selection, cancellation and controlled failure with intercepted host
dialogs. Real Windows picker behavior was verified separately in Kid Pix.
