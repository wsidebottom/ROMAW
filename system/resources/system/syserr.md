# System-error procedure port

`syserr.r` contains the user-edited DSAT 0 and 2 resources, normalized for the
native runtime. The original temporary extraction is left unchanged. Referenced
icons are maintained in `bitmaps/syserr/`, with the stop symbol shared from
`bitmaps/stop.icns`; temporary raw forks, procedure binaries,
disassembly listings and unrelated icon-family dumps are not build inputs.

The native dispatcher is selected only for the two built-in writable tables.
Guest-supplied DSAT procedures execute their original inline 68K instructions
through syn68k. A native Procedure record is four bytes containing its signed
handler ID, never a host pointer or a guest code address.

## Procedure mapping

All 15 extracted bodies map to 13 identities in
`diagnostics/syserr_procedures.c`.

| ID | Native C behavior |
| --- | --- |
| -21000 | Quit for edited table 0; `ShutDwnStart` for table 2 |
| -21101 | Insert-disk message and quoted guest module name |
| -21102 | Clear original guest flags and call `ExitToShell` for force quit |
| -21103 | Error description table; table 2 also draws application context |
| -21104 | Restore saved resource map/register context and invoke `ResumeProc` |
| -21105 | Call `ShutDwnPower` |
| -21106 | Draw error details using the native caller's saved context |
| -21107 | Quoted application force-quit prompt and alert-bounds union |
| -21108 | Application name and damaged-name restart/Shift-key advice |
| -21109 | Eject-disk message and quoted guest module name |
| -21110 | Insert-disk/in-use messages and quoted guest module name |
| -21111 | Shared quoted-name subroutine |
| -21112 | Shared alert-bounds union, also used by -21107 |

This is a native service port. ROM reset instructions, direct `ROMBase+10` jumps,
volume-ejection loops and virtual-memory supervisor-stack switching are handled
by ROMAW's existing shutdown/exit services. It does not reproduce a physical Mac
reboot or invent the ROM's private SysError stack frame. The native context saves
the resource map and guest registers explicitly. Guest module-state reads retain
the extracted offsets, require existing bounded guest memory, and never allocate
substitute module structures. These checks do not establish full System 7 module
or virtual-memory compatibility.

Native drawing uses Toolbox dispatch wrappers so guest trap patches remain active.
Text records explicitly contain terminating NULs and word padding. Slash separators
advance the baseline by fourteen pixels, including blank lines and trailing separators. The edited
`Quit ` button is six bytes after termination/padding; its old declared length of
eight was stale. Unterminated original button strings were also terminated and
their lengths recomputed. Missing icon records -20227, -20228 and -20229 were
restored because the edited -13, -10 and -11 alerts still reference them.

## Redundancy in the edited extraction

- The two -21000 binaries are byte-identical (110 bytes each). One C case serves
  both, choosing the table's requested Quit or Restart action.
- Both -21103 bodies use the same error description strings. Table 2 additionally
  calls -21108. The C port shares descriptions and adds that call conditionally;
  these bodies are not interchangeable byte-for-byte.
- -21112 duplicates the bounds-union tail of -21107. Both use one C helper.
- -21111 is only a helper called by the module-message procedures. It has no
  direct alert reference; its serialized native record is retained for identity
  compatibility, while C callers share `draw_module_name`.
- In table 2, -22001, -22000 and -22103 have identical one-button payloads.
  -22105/-22104 and -22107/-22106 are identical pairs. Keep these IDs: adding one
  to the primary ID selects the ResumeProc button set. -22102 is distinct because
  it adds Continue.
- The two original icon-family IDs (-16396 and -20024) have six byte-identical
  payload pairs: `ICN#`, `icl4`, `icl8`, `ics#`, `ics4`, `ics8`. Their raw dumps,
  the separate `cicn` dump, `picasso.icns` and `message.icns` are unreferenced by the
  edited resource. None is copied into the new fallback build.
- The seven referenced ICNS files are all distinct. Bomb artwork is shared by
  both tables; the three restored -2022x icons are still required by table 0.
- `DSAT.resource-fork`, `extract.py`, `disassemble.py`, `Procedures-68K.txt` and
  `.bin`/`.s` files are analysis artifacts. The C port replaces procedure binaries
  in the application build; the originals remain available in the temp folder.

## Validation

```bash
python3 system/tests/test-resource-catalog.py
bash system/tests/check-runtime.sh syserr
ROMAW_OBJ_DIR=/path/to/profile/obj bash system/tests/check-build.sh generated-file
```

The Windows x64 reporting fixture exercises drawing, offsets, all procedure
identities, Quit/Restart/shutdown routing, missing/present resume callbacks,
force-quit flags, module messages and malformed application-name pointers.
A GUI SysError launch is additionally required to validate the integrated flow.
