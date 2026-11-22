# ROMAW resources

`system/catalog.r` and [`finder/resources/catalog.r`](../../finder/resources/catalog.r) are the authoritative Rez roots for
251 embedded System resources and 323 editable Finder resources.
Each root includes its interface, payload/text and artwork declarations.
Types, signed IDs, names and attributes come directly from those declarations;
there is no separate JSON catalog or original-payload checksum requirement.
System and Finder remain distinct resource forks, including their colliding
`vers` IDs 1 and 2.

The four former Finder executable resources (`CDEF 128`, `WDEF 128`, `gest 128`
and `hcsl 0`) have native implementations under `finder/guest/`. Their resource
declarations use `$$finderCode()` to generate minimal guest dispatch veneers;
no binary inputs remain. Matching embedded System fallbacks expose the native
procedures to guest applications through the normal resource chain.
See [the integration notes](../../finder/guest/README.md) for ABI and state ownership.

Artwork stays in the workspace-root `bitmaps/` directory. `artwork.r` uses
`data` declarations with `$$read("filename.icns")` or `$$read("filename.cur")`.
The ROMAW generator converts these editable assets to the requested classic
resource type. Multiple ICNS reads in a `SICN` declaration concatenate the small
icons in declaration order. This artwork conversion is a ROMAW build step;
a general Rez compiler would instead read the source file bytes literally.

PICT declarations use `$$pict("PICT-128.png", 134, 272)` to compile an RGB PNG
into a PICT v2 bitmap resource. The final two arguments preserve the original
frame's top and left coordinates. Pictures remain editable PNG files under
`bitmaps/`; resource type, ID and attributes remain in the Rez declaration.

`tools/resource_catalog.py` reads local includes, declaration headers, literal
hexadecimal `data` bodies and external reads. `tools/rez_types.py` also encodes
typed `STR `, `STR#`, `vers`, `ALRT`, `DLOG`, `DITL` and `clut` resources, plus Finder's
Pascal-string `MACS` signature. Strings are editable text with MacRoman byte
escapes where needed. Dialogs expose rectangles, procedures, button states,
item references and positioning; older dialogs may omit the position word.

The reader checks duplicate identities, field widths, string lengths, attributes
and include cycles/path escapes. It does not expand arbitrary templates or
compile private Finder layouts. Finder's menus already use typed `fmnu`
declarations; native Finder reads these and the particular view layouts it uses
directly. Building ROMAW needs no Rez installation. Opaque payloads and four
System item lists remain literal `data`: two have extended item records, and two
have nonzero padding bytes that ordinary typed DITL output would discard.

The Windows activation manifest lives beside `ROMAW.rc` at
[`platform/os/cygwin32/ROMAW.manifest`](../../platform/os/cygwin32/ROMAW.manifest).
It controls application privileges, DPI awareness, long paths and common controls.
Native desktop configuration lives under [`finder/resources/`](../../finder/resources/):
`label-menu.r` uses the custom typed `lMnu` layout for menu dimensions, eight label names and
RGB colours, a shared Finder `cicn` resource ID and palette tint index, and an
8 by 9 checkmark bitmap. The generator reads this through
`load_label_menu`, using the same checked Rez field encoder as classic resources. These are independent of Macintosh resource forks.
Generated forks, bitmap tables, Windows ICO files and compiled resource objects
belong in ignored `obj/` directories.

Edit the relevant Rez file or shared artwork, then build ROMAW normally:

```sh
python system/tests/test-resource-catalog.py
python system/tests/test-system-resources.py
python system/tests/test-edited-icons.py
make -C system
make -C system test-native-finder
```

The Python image/resource tests require Pillow.
