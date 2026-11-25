# Embedded System resources

ROMAW compiles 251 System resources, including a native
`dcmp 0` dispatch resource and default drive icons into its
executable. `catalog.r` includes the authoritative Rez declarations of each resource's
type, signed ID, attributes, Macintosh name bytes and payload. Dialogs,
patterns, keyboard maps, international tables, pictures and code are retained.
These are the installed ROMAW support resources, not a complete bootable
Apple operating system.

All `*.r` files in this directory are free of literal hex blocks. `interface.r`
uses typed alerts, dialogs, item lists, strings and pattern references. Help items
include their subtype/resource ID; an optional final byte inside an odd-length
item preserves its original alignment byte, including historical nonzero values.

`payloads.r` exposes native definition-table entries, international number/date
formats, keyboard modifiers and character/dead-key tables, color tables and
pixel-pattern metadata as typed fields. Native definition symbols name the
existing C implementations. Keyboard tables are split into sixteen-key rows;
counts and resource-relative pattern offsets are derived by the encoder.
The inherited `fwst` window rectangle is named, while its remaining state words
and the unidentified `movp` preference words retain explicit signed 16-bit fields.
Their undocumented meanings are not guessed.

`resource_types.r` documents these layouts and native definition symbols.
`tools/rez_types.py` and `tools/rez_system_types.py` check fields and emit
Macintosh byte order without an external Rez SDK. PNG expressions, explicit
DITL padding and pixel-pattern layout synthesis are native builder extensions.
Existing external artwork declarations and native code expressions remain
`data` declarations; they contain no embedded hexadecimal payloads.

Forty-three shared PNG inputs in `bitmaps/system-patterns/` supply the original
47 bitmap uses: three individual monochrome patterns, 38 pattern-list entries,
three pixel patterns and their three monochrome fallbacks. Identical patterns
share files as documented in `bitmaps/README.md`. `$$pattern` reads opaque 8x8
black/white pixels; `$$pixels` reads the indexed PNG without quantizing or
reordering its palette. Keep color patterns indexed and at their declared size.
RGB colors are regenerated from the PNG palette; color-table indexes, seed,
flags and PixMap state remain explicit in Rez. These palettes originally used
8-bit-expanded 16-bit components, so their reconstruction is exact.

`system/tests/system-typed-resources.json` records original hashes, sizes and
metadata for all 55 converted resources. The reporting suite checks exact
reconstruction, image edits, malformed fields and absence of raw hex blocks:

```bash
python3 system/tests/test-system-typed-resources.py
```

Conversion preserves historical quirks, including the unused selector 8 in the
eight-table keyboard layouts 136/137, nonzero dialog alignment bytes and legacy
preference words. It does not silently repair these resources. INTL 1's native
localization hook is audited separately from its unchanged 330-byte data record.

Icon and cursor families live in the
workspace-root `bitmaps/` as ICNS files containing all available
sizes and colour depths. `artwork.r` selects the corresponding representation through its resource type.
System cursors use `curs_beam.cur`, `curs_cross.cur`, `curs_plus.cur` and
`curs_watch_1.cur` in `bitmaps/`, in Windows cursor format, with one
16x16 monochrome image. The builder converts its XOR and AND planes to classic
bitmap and mask planes, preserving transparent and inverse pixels, and converts
the hotspot to Macintosh vertical/horizontal order. Compressed icon resources were decoded
with rsrcfork 1.8.0 before conversion. The builder clears the compression bit
for the rebuilt resources.

The generic hard-disk icon family (`-3995`) comes from `bitmaps/-3995_genericHardDiskIconResource.icns`, and
the generic file-server family (`-3972`) comes from `bitmaps/-3972_FileServerIconResource.icns`, in
large/small monochrome, 4-bit and 8-bit forms. Classic Finder can load these
defaults directly from the embedded System. The ROMAW volume uses the
classic custom-icon ID (`-16455`) with `bitmaps/ROMAW_volume.icns`; its root
catalog name is the mounted alias `ROMAW`, rather than the host folder name
`Apps`. Its custom volume-icon lookup
continues to take precedence; these defaults do not replace a custom icon.
Host disk-driver controls 21/22 return an image/mask pointer selected by the
mounted drive's host kind, including mapped network drives. The IDs match
`Interfaces/CIncludes/Icons.h` in the user's SuperMario reference; the control
ABI matches `Drivers/Sony/SonyIcon.a`.

`tools/romaw/system_resources.py` rebuilds a deterministic Macintosh resource fork
and C byte array under `obj/generated/`. Resource names and non-storage
attributes survive this rebuild. `tools/romaw/font_suitcase.py` uses Pillow with
FreeType to generate MacRoman bitmap strikes and family records from the
repository's `fonts/*.ttf`. It includes the Chicago outline as `sfnt` 0 for the
ATSUI default font lookup. The build stages the generated `ROMAW.rsrc`
beside the executable and the TrueType files in its `fonts/` directory for
enlarged Windows text. ROMAW loads the suitcase into its System resource map
at startup. It also
imports FONT, NFNT, FOND and sfnt resources from additional suitcases in the
profile's `Apps/System Folder/Fonts`. The old build-owned `ROMAW Fonts`
suitcase is skipped on upgrade.

Startup no longer opens `System` or its AppleDouble sidecar. A private temporary
raw fork provides a real File Manager reference number, resource purging/reloads,
and writable Resource Manager behavior. It is deleted automatically. It is not
published in the Apps folder or used as an OS X Classic System file.

Editable System resources and PRAM share the profile's
`System Folder:Preferences:ROMAW` file. See
[the runtime guide](../../../docs/runtime-guide.md#editable-system-resources)
for persistence and override deletion, and
[application preferences](../../../docs/runtime-guide.md#application-preferences)
for window settings. These user settings are separate from the embedded defaults.

## Decompiled code

The standard Toolbox executable resources use ten-byte ARDI dispatch stubs:

```asm
move.l  $0058.w,a0
move.l  slot*4(a0),a0
jmp     (a0)
```

Its exact C-level behavior is a tail call to the corresponding entry of the
guest dispatch table stored at low-memory address `0x58`, with the original
Pascal arguments/return convention. That table already connects to maintained
C implementations:

| Resource | Slot | C implementation |
|---|---:|---|
| CDEF 0 | 0 | `C_cdef0`, `controls/ctlStddef.c` |
| CDEF 1 | 1 | `C_cdef16`, `controls/ctlArrows.c` |
| WDEF 0 | 2 | `C_wdef0`, `windows/windDocdef.c` |
| WDEF 1 | 3 | `C_wdef16`, `windows/windDocdef.c` |
| MDEF 0 | 4 | `C_mdef0`, `menus/stdmdef.c` |
| LDEF 0 | 5 | `C_ldef0`, `lists/listStdLDEF.c` |
| MBDF 0 | 6 | `C_mbdf0`, `menus/stdmbdf.c` |
| snth 5 | 7 | `C_snth5`, `sound/snth5.c` |
| CDEF 63 | 9 | `C_cdef1008`, `controls/ctlPopup.c` |
| dcmp 0 | 10 | `C_dcmp_template`, `resources/resOpen.c` |

Finder CDEF 128, WDEF 128, gest 128 and hcsl 0 use slots 11 through 15.
Their generated veneers also pass the actual resource base for per-instance
state. See [the guest integration notes](../../../finder/guest/README.md).

INTL 1's localization entry at byte 330 is also implemented in C. Its checked
slot-16 veneer preserves the original no-op's registers, CCR and caller stack.
The data prefix stays unchanged; the resource grows to 344 bytes for the native
entry. `python3 tools/audit-native-resources.py` validates the complete executable
inventory, including this embedded hook and the early-boot DSAT procedures.
See [the native resource inventory](../../../docs/native-code-resources.md).

The added `dcmp 0` resource exposes the bounded native System 7 decoder through
the classic four-argument Pascal interface. Its guest dispatch stub can be
copied or relocated. Resource Manager decoding retains the declared output
length (including odd-size padding) and prefers application-supplied
decompressors. Run `bash system/tests/check-toolbox.sh dcomp0` for decoder checks and
`system/tests/check-toolbox.ps1 dcmp0-resource` for staged guest calls, overlap and malformed input.

The build verifies every instruction byte and generates the actual C dispatch
assignments from these decoded stubs. Modified or new executable resources
require review and stop generation. The original guest stubs remain for ABI
compatibility. There are no CODE, INIT, DRVR or PACK resources in this System;
`movp` is preserved as opaque data; ROMAW has no executable entry point for
that resource type.

Keyboard pairs 0/150 (US/Canada English), 133/146 (Suomi/Svensk) and 136/137
(Vlaams comma/France) include shared typed field fragments in `keyboards/*.rinc`.
Edit each shared layout once; its two resource declarations keep their original
IDs, names and attributes. The native reader expands quoted field includes
relative to the including file, within the resource scope, and rejects cycles.
These are ordinary Rez preprocessor includes, not runtime resource aliases.
Each ID still has its own stored resource record and Resource Manager handle.
The build and source distribution include the fragments as dependencies.

Resource content retains its original provenance/copyright; extraction and
format conversion do not relicense historical fonts or artwork.

Run `bash system/tests/check-toolbox.sh system-resource` in WSL. Windows runtime
checks should also verify startup without any external System file and a
resource preference save/relaunch round trip.

`system/tests/check-toolbox.ps1 system-resource-runtime` also checks application-supplied
`PACK 1` dispatch with original 68K test code: selector arguments, stack balance,
register preservation and temporary resource locking. ResEdit uses its own
`PACK 1` for the icon editor. Real package resources take priority over the
legacy presence markers for packages implemented by native Toolbox traps.

The standard alert icons (IDs 0, 1 and 2) retain their legacy `ICON` bitmaps
and also provide masked `cicn` resources from the same ICNS artwork. Dialogs
use the masked representation at all display depths, including monochrome.
Classic masks retain pixels with alpha at least 128.

`syserr.r` defines the user's edited `DSAT` 0 and 2 tables. They remain separate
from `catalog.r`: the generator embeds writable arrays in
`generated/syserr_table.inc`, available before Resource Manager initialization.
The default table has the edited Quit button. Table 2 supplies its retained
specialized alerts, including force quit and Code Fragment/Mixed Mode failures.
Referenced artwork lives in `bitmaps/syserr/`.

Procedure records contain signed native handler identities; maintained C bodies
in `diagnostics/syserr_procedures.c` implement the extracted 68K procedures.
Built-in table identity gates native dispatch. External guest `DSAlertTab`
procedures still execute as inline 68K code through syn68k. Text explicitly
includes its NUL terminator and word padding; the generator rejects wrong lengths.
See [the procedure mapping and redundancy review](syserr.md) for the native
service adaptations, retained aliases and validation commands.
