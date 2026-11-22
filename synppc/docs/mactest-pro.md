# MacTest Pro for Power Macintosh

The local test input is
`C:/Users/William/.romaw/Apps/Power Macintosh copy/MacTest Pro for Power Macintosh`,
with its AppleDouble sidecar and adjacent `Test Modules` directory intact.
The application shell is classic 68K CODE. It loads an embedded PowerPC PEF
helper with `GetMemFragment`, including a zero container length.

## Application profile

The user's `ROMAW.prefs` has this application-specific configuration:

```ini
[APPL.BarT]
SystemVersion=9.0.0
SysZone=16m
ApplZone=64m
PretendScript=1
Gestalt('mach')={0,75};
Gestalt('sysa')={0,2};
Gestalt('cput')={0,257};
Gestalt('pclk')={0,60000000};
Gestalt('bclk')={0,30000000};
```

These are nominal Power Macintosh 6100/60 identification values for the application's
compatibility checks. They do not implement that machine's physical hardware or
cycle timing. The actual PPC interpreter remains a 750. This MacTest version
explicitly rejects a G3 identity and asks for the separate MacTest Pro for G3.

## Compatibility fixes

- Script Manager detection requires the existing `PretendScript` setting.
- `TruncText` must dispatch selector `0xde`; both it and `TruncString` need real
  Roman truncation, including the older selectors without `truncWhere`.
- `SysEnvirons.machineType` derives from Gestalt's machine ID minus two, rather
  than a hard-coded value independent of per-application overrides.
- A rejected classic CFM request must return its trap error without leaving an
  execution error in the classic connection context. Otherwise a patched
  `EventAvail` makes `WaitNextEvent` return before consuming input.
- A zero-length memory PEF is bounded by its header, section table and packed
  section spans. Classic clients can initialize a PPC connection context from
  the first memory container's architecture, with a valid PPC return sentinel.
  This does not provide arbitrary mixing of architectures in one context.
- `_MPIsFullyInitialized` reports false. Multiprocessing is not implemented.
- Font-resource disposal must invalidate dependent width caches before resource
  handles can be reused. MacTest's resource inspection exposed stale font data
  and a negative text bitmap allocation in the absence of that invalidation.

## Validation and limits

Run the focused fixtures with:

```powershell
./synppc/tests/check-runtime.ps1 cfm68k
python system/tests/create-truncstring-probe.py build/mactest-pro/trunc-probe M:/Browser
C:/msys64/mingw64/bin/gdb.exe -batch -x build/mactest-pro/trunc-probe/probe.gdb windows-binaries/bin/ROMAW.exe
```

```bash
bash system/tests/check-toolbox.sh toolbox-additions
```

The CFM fixtures include a rejected memory fragment followed by a patched event
loop and a valid library load, plus a classic client that rejects malformed
PPC extents, loads an original PPC memory fragment and invokes it through a
routine descriptor. The latter also checks the negative MP availability probe.

MacTest reaches its test-selection window and accepts test controls. Hardware
Info originally faulted at `0x5fffeff0`. Disassembly subsequently identified this
as a fixed pointer to cache-information records, rather than a device register.
ROMAW now supplies a bounded, read-only data-bus view of these records. The
virtual cache sizes are zero, the L1 unified flag is set, and the later cache
record version is zero: no physical cache hierarchy or secondary-cache board is
advertised. This does not implement physical logic-board diagnostics.

Software Info can be selected separately. The validated configuration includes
System, QuickDraw, Communications Toolbox, Debugger, Network Connections,
Volumes Online, Apple Menu Items, System Extensions, Control Panels, Chooser
Extensions and Startup Items. MacTest reported **All selected tests passed**
on November 15, 2026. The application remained responsive after completing it.

System Heap, Installed Fonts, Applications and Miscellaneous Boot Volume File
Statistics were excluded from that final run. A broader inventory exceeded the
application's log capacity; its whole-boot-volume file statistics scan was stopped
through the normal Stop control. Continuing past the capacity notice verified
that the font-cache fix corrected the broken dialog text and stack-overflow path.
No Windows exception appeared in the final run's trace.

The six CFM scenarios, six truncation ABI cases, fourteen Toolbox fixtures,
resource-map checks, generated-file checks and Windows help checks passed.
The repository-wide check passed for 1,643 file headers and 1,492 formatted
source files; the PowerShell check passed as well.
The 115 PPC interpreter probes passed after the CFM changes; the later font-cache
change was checked by the targeted fixtures and the actual MacTest run. Keep
these results separate from unimplemented physical hardware diagnostics. Logs
and debugger captures are local artifacts under `build/mactest-pro`.

## MacTest Pro for G3

The separate application at
`C:\Users\William\.romaw\Apps\MacTest Pro (G3)\MacTest Pro for G3`
uses creator `bart` (lowercase), unlike the older application's `BarT`.
Its separate, case-sensitive `ROMAW.prefs` section is:

```ini
[APPL.bart]
SystemVersion=9.0.0
SysZone=16m
ApplZone=64m
PretendScript=1
Gestalt('mach')={0,510};
Gestalt('sysa')={0,2};
Gestalt('cput')={0,264};
Gestalt('pclk')={0,233000000};
Gestalt('bclk')={0,66000000};
```

These nominal G3 identification values let the app display **Power Macintosh G3
series/Macintosh Server G3**, ID 510. They do not implement physical G3 hardware
or cycle timing. The older application's profile remains separate.

On November 15, 2026, Software Info v1.1 reported **All selected tests passed**
with the same software-information selections listed above. The app remained
responsive, and no unsupported-import or Windows-exception diagnostic appeared
in that initial run. Hardware Info v1.6, with its default selections, raised an unhandled
trap 2 (bus error). Windows GDB confirmed A0 and the fault frame's access address
were `0x5fffeff0`, the same cache-information pointer access as the older version.
That initial run was not a passing hardware diagnostic. Local logs and the debugger
capture are under `build/mactest-g3`.

The cache compatibility view handles only the 64-byte virtual record at
`0x5fffe000` and 28-byte pointer/version area at `0x5fffefd8`. Other unmapped
addresses remain invalid. The optional syn68k data-bus callback does not map
instruction fetches or writes. Interpreter clients with a bus use the shared
checked memory-operation path; native compilation retains its checked fallback.
Install the callback before translating guest code, or invalidate existing blocks
when changing it. Boundary, endian, instruction-fetch and write rejection tests
live in `system/tests/powerpc_cache_test.c` and `syn68k/tests/bus-read-test.c`.

After this fix, the staged Windows executable completed Hardware Info v1.6 with
its default selections and then with all 14 enabled setup options checked. Both
runs reported **All selected tests passed**, and the app remained responsive.
The expanded run included model, processor, coprocessor, memory manager, physical
and logical RAM sizes, power-on/production date, hardware attributes, keyboard,
ROM, expansion slots, mass storage, floppy and display information. Portable
Battery was disabled by the application for this model. The report identified
the G3 at the profile's nominal 233 MHz and correctly reported 84 MB of guest
RAM, no SCSI devices at the probed device IDs, unavailable floppy information,
and the emulated display dimensions. SCSI probing takes time because the legacy
SCSI stub reports busy; this is not evidence of an emulated physical controller.
An additional actual-app run with `-notnative` completed Hardware Info's default
selections and reported **All selected tests passed**, verifying the interpreter
path independently. The traces contained no new unhandled trap, Windows exception
or PPC/CFM fault.
Logs for this follow-up are under `build/mactest-g3/fix`.

Only Hardware Info and Software Info appeared in this application's available
test list. The separate board, RAM, USB and other physical-device diagnostic
modules were not validated by these information checks. No test success was
forced or application code patched to bypass the cache read.

Follow-up validation passed: the complete
`syn68k/tests/check-core.sh control-register` suite (including the new data-bus
fixture in both backends), 15 Toolbox fixtures, 24-bit memory/heap checks, six
CFM scenarios, Windows help and generated-file checks. The repository-wide
format/header check passed; after the interpreter dispatch adjustment, the
changed C files passed clang-format verification and all source headers passed
again. The PowerShell check passed. ROMAW, syngen and libsyn68k were staged in
`windows-binaries/bin`.

## DiagC99I2C startup

The G3 `Test Modules/DiagC99I2C` helper imports the obsolete in-place
`InterfaceLib:p2cstr` conversion. The bridge now handles that function and its
`c2pstr` counterpart, returning the original guest pointer. Pascal conversion
copies its counted bytes, including embedded NULs, and appends the C terminator.
C conversion truncates at 255 bytes. Both validate the full writable destination
before moving bytes; invalid memory produces a controlled diagnostic. Null input
is preserved as null. This is separate from CarbonLib's two-pointer copy APIs.

The original PPC fixture `make_ppc_inplace_string_probe.py` checks payloads,
surrounding guards, pointer returns, empty/high-byte/embedded-NUL strings, the
255-byte limit, truncation and null input. Invalid-pointer cases are included in
the diagnostic regression runner. The actual DiagC99I2C application called
`p2cstr` and exited normally in both PPC interpreter and JIT modes after the fix.
That standalone launch verifies the import failure is resolved; it does not
establish a passing physical I2C diagnostic or a full MacTest-hosted test cycle.

Validation for this conversion fix: 117 standard PPC probes passed per engine
across the initial and continuation runs; the File Probe failed at ResolveAlias
with `dirNFErr` in both engines and reproduced unchanged in the pre-fix binary.
The full standard suite therefore was not a clean pass. All 101 interpreter
diagnostic scenarios passed, as did the two new invalid-string scenarios under
JIT. Header/format, PowerShell, Windows help and generated-file checks passed.
After a concurrent workspace build refreshed the staged executable, the new
conversion probe and actual DiagC99I2C launch were rerun successfully in both
engines against that latest executable. Logs are under `build/diagc99i2c`.
