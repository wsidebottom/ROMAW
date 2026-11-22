# Kid Pix compatibility checks

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
