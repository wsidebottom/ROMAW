# VGA Startup for Macintosh LC 475

Native 68K application requesting **640 x 480, VGA 60 Hz, 256 colors** on the
main display, without a window or confirmation dialog. It leaves the new mode
active when it quits and asks the video driver to save it as the startup mode
in PRAM. It does not select Apple's incompatible 640 x 480 / 67 Hz timing.

`VGAStartup71` is the separately named System 7.1 build of the same source.
Use `VGAStartup71.bin` or `VGAStartup71.dsk` in place of the filenames below
when installing that build. System 7.6 includes Display Manager 2.0; System
7.1 requires the separate extension described below. Recompilation does not
remove this runtime dependency.

## System 7.1 requirement

**Apple Display Manager 2.0 or later must already be installed and enabled.**
Apple supplied a Display Manager 2.0 extension compatible with System 7.1.
Plain System 7.1 does not provide the enumeration/switching APIs this application
uses. The extension is not included in this project or its binary package.
Without it, the app writes an explanation to its log and exits without changing
the display or PRAM. The program does not display an error that would block a
blind startup.

This program bypasses the optional-mode confirmation policy by requesting the
VGA mode directly. It still needs a VGA timing exposed by the video driver and
a correctly wired Mac-to-VGA adapter. It cannot override monitor-sense wiring,
create a missing driver mode, repair sync wiring, or run before the System/Finder
has loaded. "Out of range" alone does not identify the incoming resolution or
prove that PRAM is its cause.

## Installation

1. Transfer `VGAStartup.bin` to the Mac **as a binary file**. Decode the MacBinary
   file with a classic utility such as StuffIt Expander to recover `VGAStartup`
   with its resource fork. Alternatively mount the supplied `VGAStartup.dsk`
   Macintosh disk image and copy the application from it. Do not copy a bare
   zero-byte `.APPL` file from Windows: the code is in its resource fork.
2. Install Apple's System 7.1-compatible Display Manager 2.0 extension into
   `System Folder:Extensions` if needed, preserving its resource fork.
3. Put the decoded `VGAStartup` application in
   `System Folder:Startup Items` on the LC 475's boot disk.
4. Restart. Once the Finder launches Startup Items, the application attempts
   the switch and persistence writes automatically, without keyboard input.

The app creates/replaces `VGA Startup Log` alongside itself. It lists the
enumerated modes, errors from the mode switch and save operations, and the
readback result. `Set mode error (0 means success): 0` reports a successful
switch. `PRAM default matches requested mode: 1`, following successful write
and read calls, reports that the classic driver's saved mode matches the
requested VGA mode. A successful disk-preference save alone is not a PRAM
verification. File logging is best effort; a read-only startup disk does not
prevent a switch.

After a successful run, shut down and power on to check that the new battery
retains the setting and that the early boot display is usable. Only that test
establishes persistence on the physical machine. Keeping the app in Startup
Items also reapplies VGA after the Finder starts on subsequent boots.

Hold either Shift key while the application starts to skip its operation.
Removing it from Startup Items prevents future runs but does not undo a saved
PRAM setting. A PRAM reset can clear saved settings; it does not fix incorrect
adapter sense wiring.

## Build in WSL

Requires a working Retro68 68K toolchain and its classic Mac interface headers.
The installed local toolchain uses the open Multiversal interfaces. No Apple
runtime or ROM code is included in the application.

```bash
cd /mnt/d/Source/ROMAW
cmake -S tools/lc475-vga-startup -B build/lc475-vga-startup \
  -DCMAKE_TOOLCHAIN_FILE=/home/william/retro68-toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DVGA_BUILD_TESTS=ON
cmake --build build/lc475-vga-startup
```

The app uses 68020 instructions and software floating point, suitable for the
LC 475's 68LC040 without an FPU. Its preferred partition is 128 KB, minimum
64 KB. Generated binaries and disk images stay under `build/`.

## Implementation and validation

Display Manager enumerates all timing modes, including optional modes. Selection
requires 640 x 480 video parameters at 8 bits per pixel and a 59-61 Hz refresh
rate, or the documented VGA 60 Hz timing identifier for older drivers reporting
zero refresh. It uses the driver's depth ordinal rather than assuming a fixed
ordinal for 256 colors.

After `DMSetDisplayMode`, the app calls `DMSaveScreenPrefs` to save the system's
display preferences. For classic byte-sized functional sResource IDs it then
uses Device Manager control selector 9 (`SetDefaultMode`) and status selector 9
(`GetDefaultMode`) to write/read the driver's slot-PRAM startup mode. The driver
owns its PRAM layout. If that operation is unavailable, it tries selector 16
(`SavePreferredConfiguration`/`GetPreferredConfiguration`) and logs the result.
It never guesses PRAM byte offsets or directly programs video hardware registers.

`VGAStartupTests` exercises the actual mode selector and persistence path using
synthetic video records and intercepted driver controls. It checks 67 Hz
rejection, 59.94 Hz selection, resolution/depth mismatches, unknown refresh,
VGA timing identification, classic PRAM control/readback and selector-16
fallback. It never changes real display settings or PRAM. The tests passed in
the staged Windows ROMAW with `-system 7.1`; the production application's
missing-Display-Manager exit also passed without a Windows exception.

Compilation and synthetic tests do **not** establish actual LC 475 mode
switching, adapter compatibility, or retention across power cycles. These
hardware checks have not been performed. ROMAW does not emulate the LC 475
video controller or its PRAM persistence.

## Interface references

- Apple's [Display Manager SDK announcement](https://www.savagetaylor.com/wp-content/uploads/documents/Apple_Directions/Apple_Directions_1996/Apple_Directions_05-96.pdf)
  documents the System 7.1-compatible extension.
- Apple's [Designing Cards and Drivers for the Macintosh Family, second edition](https://vintageapple.org/inside_o/pdf/Designing_Cards_and_Drivers_for_the_Macintosh_Family_2nd_Edition_1990.pdf),
  chapter 9, describes `SetDefaultMode` and slot-PRAM persistence.
- Apple's Universal Interfaces [Displays.h](https://raw.githubusercontent.com/elliotnunn/UniversalInterfaces/master/3.4.1/Universal/Interfaces/CIncludes/Displays.h)
  and [Video.h](https://raw.githubusercontent.com/elliotnunn/UniversalInterfaces/master/3.4.1/Universal/Interfaces/CIncludes/Video.h)
  define the public trap and record ABIs used by the minimal local declarations.
- Apple's [LC 475 / Quadra 605 developer note](https://www.macdat.net/files/pdf/apple/developer_notes/macintosh_lc475_quadra605.pdf)
  documents VGA support and monitor-sense codes.
