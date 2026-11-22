# Kid Pix Deluxe 4 Installer startup

The user's installer on the HFS volume in `Kid Pix 4.iso` fails during
CarbonLib initialization when launched from Finder with the default emulated
System 7.1.1 and a 2 MB system heap. A separate diagnostic launch using
`-system 9.2 -syszone 8m -applzone 32m` completes CarbonLib initialization and
enters the installer. These settings are diagnostic, not a change to defaults
or a claim about the installer's minimum requirements.

The previous startup message attributed a failed CarbonLib initializer to a
missing startup extension whenever an earlier `clqu` query failed. That query
does not establish the cause of a later initializer failure. The diagnostic now
reports the actual initializer error and emulated system version instead.

The installer next requested InterfaceLib `MakeDataExecutable`. The bridge now
accepts a bounded guest-memory range. The interpreter fetches guest instructions
directly from RAM, and native blocks compare every source instruction before
entry, so no host instruction-cache operation or executable guest-memory mapping
is needed. The original Code Publish probe replaces a function after warming its
native block, publishes it again, and verifies the changed result. The staged
application suite runs this in both modes and requires native execution in JIT
mode. Empty ranges do not dereference their pointer.

With that service, the installer loads its next fragment. The bridge also exposes
the existing QuickDraw `HasDepth` implementation, with checks for a real display,
zero depth and a null graphics device.

`PBXGetVolInfoSync` uses the packed 138-byte version-zero XVolumeParam from the
local Universal Interfaces 3.4.2 SDK. It delegates volume selection and legacy
metadata to the existing File Manager, then supplies 64-bit capacity and available
bytes from GetDiskFreeSpaceEx for host directories. Native HFS volumes use their
allocation-block counts. Filesystem dispatch distinguishes these by vcbCTRef,
as in hfsXbar.c; the host VCB's driver reference is not an identifier for this.
The probe compares both halves of the real C-drive capacity against an independent
Windows API query and checks invalid versions, volumes, names, pointers, output
guards and preservation of capacities on failure.

The installer now reaches the missing `WideDivide` import. No installation has
been performed, and no initializer has been skipped. This is not yet a successful
installer launch.

## Automatic startup profiles

A failed CarbonLib initializer, exhausted guest memory before application code
starts, or an unsupported private-copy path in a startup launcher now retries in
a fresh ROMAW process. The fallback profiles are:

| System | SysZone | ApplZone |
| --- | --- | --- |
| 8.6 | 8 MB | 16 MB |
| 9.0.4 | 8 MB | 32 MB |
| 9.2.2 | 16 MB | 64 MB |

Already larger heaps are retained. The current version is never lowered. A
profile at the current version can increase undersized heaps once; after the
final version and heap sizes have been reached there is no further retry.
Original document operands, mounted images and return-to-Finder mode survive
replacement. The trial overrides an older saved SystemVersion for that run,
preventing the saved setting from restarting the same failed profile forever.

Successful application completion and library termination save SystemVersion,
SysZone and ApplZone together in the existing application section of
ROMAW.prefs. Other preferences and sections are preserved. Merely completing
CarbonLib initialization does not save a profile. Ordinary application faults
are not automatically replayed. Entering normal GetNextEvent processing also
stops compatibility retries, and failed runs do not replace the saved settings.

`check-runtime.ps1 startup-profile` uses original library/application code in an
isolated preference profile. It verifies the complete retry chain, reuse of the
saved values, launcher stubs, final initializer failure, an ordinary application
fault and a private-copy failure after event processing starts.
No Apple binaries are part of those fixtures.

The actual installer image was also checked with automatic replacement. Four
processes covered the original 7.1.1 launch and all three fallback profiles.
8.6 and 9.0.4 select unsupported private-copy paths; 9.2.2 gets past those and
stops at `WideDivide`. No installation or successful-profile save occurred.
HFS-backed application FSSpecs now relaunch using bounded, volume-qualified
classic paths rather than requiring an individual host filename. The image
itself remains mounted directly; no application export/cache copy is needed.
