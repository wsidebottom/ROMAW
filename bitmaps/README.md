# Shared ROMAW and Finder artwork

Label menu artwork comes from `label-menu.png` only. Finder `cicn` resources
301–307 share that source. `finder/resources/label-menu.r` selects the
base resource, tint palette index and each menu entry's RGB color; the generator
changes that single ColorSpec while preserving the CIcon's pixels and mask.

This directory holds icons (`.icns`), cursors (`.cur`), pictures and indexed
UI artwork (`.png`).
Resource IDs and flags remain in `system/resources/`; builds read the
artwork from this directory.

`type_ffil_1.icns` represents Macintosh type `FFIL`; `type_ffil_2.icns` represents
`ffil`. Their distinct filenames also work on case-insensitive Windows filesystems.

The `PICT-*.png` files are lossless RGB raster versions of the original Macintosh
pictures, rendered through ROMAW's QuickDraw implementation. The build
wraps their RGB pixels in PICT v2 resources for Macintosh callers. Rez
`$$pict("PICT-128.png", 134, 272)` specifies the original frame's top and left
coordinates. Keep these coordinates when editing artwork. PNG decoding uses
Pillow, also used for PNG-backed ICNS files. Legacy RGB BMP sources remain
accepted by the picture compiler; alpha is ignored for picture resources.

The menu arrows, Color Picker buttons, color wheels and native Finder scrollbar
track use opaque indexed PNGs. Keep their indexed PNG mode, bit depth, dimensions and
palette order when editing; RGB PNGs and transparency are rejected for these
controls. The compiler packs their palette indices directly, without RGB
conversion or palette reordering. QuickDraw recolors these indices at runtime.
Color-wheel images are 208×208 at 4 and 8 bits per pixel; their displayed colors
are dynamic. The compiler retains their palette indices exactly.
The fallback menu-bar alarm is an ICNS icon. Generated native arrays belong
in `system/obj`, and rebuilding is required after an artwork edit.

Popup controls share the down-arrow artwork in `3061.icns`. The build extracts
its 11×6 glyph from `(2, 6)` through `(13, 12)` in the 16×16 monochrome frame,
preserving the popup arrow's placement and size.
`3061.icns` is the 90° clockwise rotation of `arrow_open.icns`, including its
monochrome mask and indexed color frames.

Submenu controls use `arrow_closed.icns`, converted from the former
`menu_right_arrow.png`. Its 6×11 glyph occupies `(5, 2)` through `(11, 13)`
within the 16×16 canvas; the build extracts that region at its original size.
Menu scrolling arrows use the same glyph, rotated clockwise for down and
counterclockwise for up during the build. All three retain the menu text-color
tint, and the scrolling arrows remain 11×6 pixels.

The ICNS files in this directory are shared artwork sources. The native Finder
build selects its default icon families in `tools/romaw/native_finder/build.py`.
System and classic Finder resources select their artwork through
`system/resources/system/artwork.r` and `finder/resources/artwork.r`,
using separate resource forks. There is no Finder-local artwork directory.

The files include standard Mac preview members and preserve the original
indexed frames, palette order, dimensions and transparency in `exfr` members.
ROMAW uses those source frames for classic pixel art and for controls that
recolor individual palette indices. Standard indexed ICNS and PNG-backed ICNS
families are also accepted. Reading PNG-only artwork requires Pillow.

Rebuild ROMAW or Finder after editing an ICNS file. To replace the artwork,
save a fresh ICNS from your editor so it does not retain stale `exfr` members.
The build generates missing classic sizes and depths from the nearest frame.
It uses nearest-neighbor scaling, maps colors through typed `clut` resources in
`mac-palettes.r`, and treats alpha below 128 as transparent in classic masks.

Palette resource IDs 1, 4 and 8 select the 2-, 16- and 256-colour tables.
Each entry has an explicit index and 16-bit RGB components. Existing 8-bit
components expand exactly as `value * 257`; the icon reader exposes their high
bytes as before. Explicit indices preserve palette slots even if rows are
reordered. Device tables (`ctFlags` 0x8000) instead use record order.

Windows resource compilers require ICO, so Windows builds generate those files
in their build directories. Finder also generates Rez and embedded PNG data
from the same ICNS sources.

Run `python3 tools/complete-icon-depths.py` to add missing 1-, 4- and 8-bit
frames to square families. Pass individual ICNS paths to limit the operation.
The tool preserves existing frames.

Pass `--huge` to add the 48×48 `ich#`, `ich4` and `ich8` members to shared
families except the UI assets listed in `no-huge-icons.txt`. Those assets keep
their small representations, and completion removes any 48×48 members from them.
Other existing members are preserved; missing sizes use the nearest
source frame. This changes shared files only, without adding resource catalog entries.

Identical exported artwork shares one source file. Resource IDs, resource flags,
file-type associations and generated icon-table positions remain separate:

| Shared source | Consumers formerly using separate identical files |
| --- | --- |
| `-4000_genericDocumentIconResource.icns` | Generic documents and `TEXT` files |
| `mac.icns` | `FNDR` and `ZSYS` files |
| `type_INIT.icns` | `INIT` and `shlb` files |
| `system-patterns/PAT-16.png` | `PAT ` 16, `PAT#` 0 entry 4, `ppat` 16 fallback |
| `system-patterns/PAT-17.png` | `PAT ` 17 and `PAT#` 0 entry 23 |
| `system-patterns/PAT-list-0-20.png` | `PAT#` 0 entry 20 and `ppat` 17 fallback |

These eight removed duplicate files were byte-identical, including indexed
palettes, masks and all ICNS members. Similar-looking images with different
palette indices, transparency or resolutions remain separate.

Repeated resource payloads are not necessarily removable resources. `KCHR`
pairs 0/150, 133/146 and 136/137 retain their distinct lookup IDs. System and
Finder retain their own CDEF 128, WDEF 128, gest 128 and hcsl 0 veneers. Shared
folder icons and watch cursors already read common artwork while retaining
independent Resource Manager handles. Do not merge stored resource records or
remove these identities merely because their current bytes match.
