# Native resource procedures

ROMAW's maintained resource inputs are the System and Finder catalogs and the
two early-boot DSAT tables. The executable procedures in these inputs are
implemented in C. Resource bytes contain checked ABI veneers or signed native
handler identities, not retained procedure implementations.

Run the inventory and regression suite from the workspace root in WSL:

```sh
python3 tools/audit-native-resources.py
bash system/tests/check-toolbox.sh native-resource /absolute/path/to/ROMAW.exe
```

The audit checks all 251 System and 251 Finder resource declarations, including
the embedded INTL localization entry, plus the two separate DSAT tables. It
rejects changed native veneers, unregistered entries of executable resource
types and non-native DSAT procedure bodies. It uses resource
semantics rather than treating an instruction-shaped data word as executable.
The embedded System and early-boot table generators run the same checks.

## Implementations

| Resources | C implementation |
| --- | --- |
| CDEF 0, 1, 63 | `controls/ctlStddef.c`, `controls/ctlArrows.c`, `controls/ctlPopup.c` |
| WDEF 0, 1 | `windows/windDocdef.c` |
| MDEF 0, MBDF 0 | `menus/stdmdef.c`, `menus/stdmbdf.c` |
| LDEF 0 | `lists/listStdLDEF.c` |
| snth 5 | `sound/snth5.c` |
| dcmp 0 | `resources/resOpen.c`, bounded decoder in `include/rsys/dcomp0.h` |
| CDEF 128, WDEF 128, gest 128, hcsl 0 | `finder/guest/`; real Memory Manager, QuickDraw and Gestalt adapters |
| INTL 1, entry at byte 330 | `ROMlib_intl1_localization`, `text/intl_resource.c` |
| DSAT 0 and 2 Procedure records | `ROMlib_syserr_procedure`, `diagnostics/syserr_procedures.c` |

Standard System veneers use dispatch slots 0 through 10, with slot 8 reserved
for the existing mount service. Finder procedures use slots 11 through 15.
The INTL localization callback uses slot 16. These are real runtime callbacks
in the existing table at low-memory address 0x58.

The original INTL 1 localization implementation was one RTS instruction.
Its C body is therefore a no-op. The generated entry saves A0 with MOVEM,
which preserves flags, then dispatches to the C callback. The callback restores
A0 and consumes only the caller's return address. It preserves the original
registers, condition codes, stack convention and absence of side effects.
The data prefix and entry offset remain unchanged. The resource grows from
332 to 344 bytes to hold the checked native veneer. Date formatting continues
to use the existing native internationalization services.

`$$intl1Code()` appends this entry to the literal 330-byte data prefix. Typed
DateFormat declarations can name the same entry with `NativeLocalization`.
Both forms use the same encoder; the builder rejects an unreviewed executable
tail even though INTL is primarily a data resource.

## Data and code outside this inventory

`ROMlib_dcmp0_table` contains the fixed word dictionary used by the native
resource decompressor. Many dictionary words resemble 68K instructions because
the compression format was designed for executable resources. The dictionary
is read as data; it is not a procedure to translate or register.

`movp 0` is retained preference data. ROMAW has no executable entry for that
resource type. Icons, internationalization tables, pictures and other data
resources can also contain instruction-shaped byte sequences by coincidence.

Native dispatch veneers, CPU/JIT output and trap adapters are ABI machinery.
External Macintosh applications, ignored Apple assets, upstream FPSP assembly
and original guest test fixtures are separate from ROMAW's shipped resource
implementations. Their code must not be removed or replaced merely because
it is encountered during a byte-pattern search.

## Runtime validation

The native-resource suite compiles a Retro68 application and runs it through
the chosen Windows ROMAW. It checks direct localization calls, A0 and CCR
preservation, stack balance, relocation, resource reload and long-date formatting.
The existing `finder-guest` suite covers the four Finder resources, including
guest trap patches, allocation failure/retry, per-resource state and malformed
selector tables. These focused checks do not establish complete Classic Mac OS
or application compatibility.
