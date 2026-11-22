# SuperMario integration audit

This pass screened 823 tracked, maintained C/C++/header files under
`.`, `syn68k/src`, `synppc/src` and `packages`, excluding third-party,
generated and test directories. A search for unimplemented diagnostics, error
constants and comments found 864 matching lines in 58 files. Those are **not**
864 missing APIs: the trap table repeats reserved handlers, and many matches
describe supported error paths or partial implementations. Source review then
compared selected candidates with the local SuperMario tree.

The local reference used for this audit is
`Y:/Source Code/supermario/base/SuperMarioProj.1994-02-09`.
Its hardware assumptions and private records cannot be copied directly into
ROMAW's host filesystem, native UI or CPU bridges.
The `references/finder7/` paths below identify local reference material, which
is excluded from Git. Current implementation paths are relative to this
repository root; use the [source map](../system/README.sources.md) and
[Finder guide](../finder/README.md) for maintained code and runtime contracts.

## Implemented

| Reference | Gap | Implementation |
| --- | --- | --- |
| `Toolbox/IconUtils/IconUtils.c`, `IconToRgnGeneric` / `HitTestIconGeneric` | `IconIDToRgn` and rectangle hit tests were stubs; `PtInIconID` accepted transparent pixels. | Resource, suite and getter-based region/hit APIs now test the actual mask, including scaled transparent holes and half-open edges. |
| Same file, `PerformAlignment` | Suite plotting and regions ignored alignment. | Map the visible mask boundary, then shift the complete destination for all horizontal/vertical alignments. Drawing and hit testing use the same alignment. |
| Same file, `__MakeIconCache` / `FromSuite`; `IconUtils.a`, cache data/procedure accessors | Six icon-cache APIs were stubs. | Lazy loading retains successful getter results, survives callback-driven cache relocation, and preserves four-byte guest data/procedure outputs. Setters retain existing cached entries, as in the reference. `LoadIconCache` selects data without drawing. |
| Existing Icon Utilities ownership | `PlotIconID` leaked its temporary suite on plotting failure. | Dispose the temporary suite on both success and failure. |
| Native Toolbox / PPC InterfaceLib import tables | Selector-dispatched icon APIs were missing by name. | Expose the implemented family through both bridges, using existing Pascal metadata and callbacks. |
| `Toolbox/TextEdit/StyledTextEdit.a`, `iTENumStyles` / `GetNumStyles` | `TENumStyles` returned zero unconditionally. | Count contiguous runs, including repeated attributes, normalize reversed ranges, preserve insertion-point/unstyled results and pin the range end to `selEnd` as the source does. |
| Classic TERec's four-byte style-handle overlay | x64 `SetStylHandle` stored eight host bytes over `txFont`, corrupting the following mode/size fields and disabling styled text. | Copy four encoded guest bytes and use width-correct accessors for both TEHandle and TEPtr. |
| `Toolbox/TextEdit/StyledTextEdit.a`, `iSetStylScrap` / `StyleGuts` | `SetStylScrap` did nothing. | Apply relative scrap intervals through the existing style merge engine, preserve selection and handle state, sort/pin unsigned ranges, extend the last interval to the range end, and recalculate/redraw when requested. As in the reference, each interval replaces matching starting attributes while preserving other intervening styles. Reject truncated counts and descending/negative offsets before mutation. |
| PPC InterfaceLib TextEdit imports | Style-scrap entry points were unavailable by name. | Export `GetStylScrap`, `SetStylScrap` and `TEGetStyle` through the PPC bridge; handle-state/disposal operations support the returned handles. |

ROMAW retains its existing private icon-suite layout. This work implements
the public operations; it does not claim Apple private-structure ABI identity.

## Validation

`system/tests/check-toolbox.sh toolbox-additions textedit_style_handle` runs
the focused x64 style-handle guard, also included in the default
`toolbox-additions` suite. SDK-based icon and styled-text DLL probes were retired with
the external native application ABI; guest-runtime and standalone fixtures
remain available.

The extended `synppc/tests/make_ppc_icon_probe.py` imports the new InterfaceLib
services and checks cache data, mask hit testing and TextEdit dispatch. Existing
icon lifetime/disposal/callback fixtures remain part of validation. Generated
fixtures contain original test data, not redistributed Apple runtime binaries.

At the audit checkpoint, the native Toolbox SDK regression and six focused PPC
probes (Toolbox, shared libraries, callbacks, windows, resources and the extended icon/TextEdit probe)
passed. The MacWrite launcher restarted its guest to apply memory settings;
attaching GDB to that child confirmed the real 68K `GetNextEvent` trap path.
This is a startup smoke check. Full MacWrite interaction was not tested.

## Remaining candidates from the broad screen

These are outstanding integration work, not services made complete by this pass.

| Area | Examples in ROMAW | Relevant SuperMario area |
| --- | --- | --- |
| AppleEvents | Waiting/replies, interaction, object accessors and resolution | `Toolbox/AppleEventMgr`; requires process/event routing and descriptor ownership |
| TextEdit | `TECustomHook` and additional styled-text edge cases | `Toolbox/TextEdit`; callback hook contracts and remaining partial paths |
| Icon Utilities | Mini-icon families, transforms beyond labels and Apple private suite layouts | `Toolbox/IconUtils`; additional size/transform rendering and ABI work |
| Dialogs | Cancel-item and cursor-tracking dispatch | `Toolbox/DialogMgr`; modal input and per-dialog state |
| Balloon Help | Help content, state and balloon APIs | `Toolbox/HelpMgr`; resource parsing, window layout and event integration |
| AFP-style File Manager operations | Copy, move/rename and access/name-ID services | `OS/HFS`; host/network semantics and async completion must be implemented honestly |
| ADB, slots and hardware services | Host-specific devices, expansion and driver paths | `OS/ADBMgr`, `OS/SlotMgr`; hardware code is not a portable Windows backend |
| Component/Process Manager and newer runtime services | Incomplete registration, private dispatch and process behavior | `Toolbox/ComponentMgr` and OS managers; cross-subsystem lifetime and scheduling work |
| QuickDraw, sound, printing and numerics | Additional partial paths and unsupported formats | Corresponding Toolbox managers; each needs a focused behavioral comparison |

The CPU runtimes and native packages were included in the screen, but their
platform implementations are not replacement System ROMs. This audit does not
establish complete System 6/7/8/9, PPC or Carbon compatibility.

## Style-scrap follow-up

The real native Toolbox style-scrap probe checks relative boundaries, matching
and intervening styles, range reversal, unsigned pinning, zero-length ranges,
last-style extension, duplicate and very large offsets, malformed/truncated
scraps, unchanged selection, lock restoration, redraw and a Get/Set round trip.
It also verifies all public style attributes, recalculated metrics and output
guards. The focused PPC icon/TextEdit fixture exercises the new InterfaceLib
imports and preserves the selection while applying a scrap elsewhere in the
text, in both JIT and interpreter modes. These are focused API checks, not a
claim that all styled-text editing or callback hooks are complete.

## Native Finder QuickDraw presentation

Native Finder document frames, grow boxes, scrollbars, menu-bar titles and
desktop patterns now call ROMAW's real WDEF, CDEF, MBDF and PixPat drawing
through [the host graphics interface](../include/classic-ui/host_graphics.h).
Host-order requests and RGB output isolate
the native frontend from packed guest records. Windows still owns the outer
caption, input, file models and command dispatch. Hidden Toolbox windows supply
actual WDEF hit testing as well as drawing. Wide output uses horizontal bands
within the classic PixMap rowBytes limit.

The comparison with SuperMario `QuickDraw/DeviceLoop.a` exposed a missing
offscreen-device path: when TheGDevice is not MainDevice, DeviceLoop must start
with the current device. Its single-device fast path calls the drawing callback
without replacing visRgn with the physical screen intersection. ROMAW now
follows that path. Additional multi-device grouping semantics remain incomplete.
Colour image conversion also copies pixelType, cmpCount and cmpSize from its
destination format, allowing WDEF/CDEF resource icons to draw into 32-bit GWorlds.

`check-graphics.sh quickdraw-host` checks real manager output, native RGB ordering,
padded strides and guards, foreign-thread rejection, WDEF hit results, every
desktop-pattern pixel, and a 5000-pixel-wide offscreen surface crossing band
boundaries. A second probe compares every native scrollbar gutter pixel with
direct manager output while guarding content pixels. Native frontend regression
tests cover Zoom/restore, resize, concurrent file operations and Stop, settings,
dragging and the existing menu/dialog painters. Live QA checks moving, growing,
scrolling, content Zoom and menu interaction.

Ordinary popup rows now reuse StandardMDEF's measurement and item painter for
text, colours, selection, checkmarks, dividers and submenu arrows. The native
command model supplies literal Ctrl/F-key captions; the original guest MDEF
command-mark path is unchanged. Native application artwork is composited into
its reserved icon column. Host and presentation probes check real callback
measurement and every row pixel across state changes, with stride and exterior
guards. Windows still owns popup borders, shadows and tracking. The custom Label
popup, item content and dialog controls were the remaining presentation
migration work; their drawing path is described below. The custom menu bar
retains Windows command/keyboard routing and does not yet expose a replacement
MSAA top-level menu-bar provider.

### Icon updates and cleanup (native Finder)

Finder 7's `CODE 157:0004` update path in
`references/finder7/src/events/clipboard_stack_operations.c` brackets view
drawing with BeginUpdate/EndUpdate and passes the visible-region bounds to the
view. Native icon views now erase their background and draw every visible image
in one clipped postpaint pass, then draw labels. Previously a whole-client erase
could be followed by Windows item notifications for only a few icons, leaving
the other images blank. The QuickDraw Finder probe checks full repaint, an
erased buffered surface, partial clipping, selection and scrolled views.

Finder 7's cleanup dispatcher reaches `CODE 173:0004` in
`references/finder7/src/records/record_resource_procedures.c`, which iterates
records and invokes the layout grid helper. `CODE 49:014a` in
`references/finder7/src/catalog/volume_grid.c` snaps relative to a layout origin,
leaving exact half-cell ties on the lower grid line. Native **Clean Up to Grid**
uses that rounding rule and resolves occupied cells without alphabetizing.
**Clean Up by Name** separately sorts names and packs rows within the window.
Both save their resulting positions and preserve desktop volume/Trash anchors.
The native adaptation uses a rectangular grid and Windows Unicode ordinal
ordering; it does not reproduce Finder 7's optional staggered grid or script
collation. Standalone view tests cover ordering, rounding, collisions, narrow
windows, negative positions and fixed anchors.

### Finder drawing and update ownership

The host graphics interface now accepts bounded host-order drawing requests.
QuickDraw paints rectangles, frames, lines, rounded buttons and polygons in its
offscreen GWorld. Artwork is transferred with CopyBits. The adapter normalizes
native RGB byte order and resolves premultiplied alpha against the supplied
background before that transfer; fractional host alpha is an ingress extension,
not a claim that classic CopyDeepMask implements arbitrary alpha.

Finder-owned icon/list content, Label swatches and checkmarks, dialog artwork,
backgrounds, group boxes, text selections, clipboard pictures and drag artwork
use this interface. Classic text uses the Font Manager. Unicode glyphs absent
from MacRoman still come from host fonts as offscreen masks, then enter the same
QuickDraw image-transfer path. GDI transports completed RGB buffers and reads
host bitmap/glyph sources. Detached control fixtures retain a fallback painter
when there is no initialized Toolbox; the integrated desktop attaches QuickDraw
before creating its windows. Windows' outer application frame, system sharing
settings, printer/driver dialogs and the layered-window compositor remain host
facilities.

The Finder 7 update sequence is visible at CODE 157:009c..00e8 in
`references/finder7/src/events/clipboard_stack_operations.c`: test update-region
emptiness, BeginUpdate, obtain visible bounds, draw the view, EndUpdate. Native
folder content now owns WM_PAINT/WM_PRINTCLIENT and does not depend on Windows
NM_CUSTOMDRAW item callbacks. Erasure and the complete visible icon/text layer
are prepared in one private canvas; only the paint DC's update region is
published. Header and rename child controls are excluded. This preserves the
reference's update/clip ownership across the host window boundary; it is not a
claim that native HWNDs are packed classic WindowRecords. Finder 7's clipped
label/slack erasure helpers are at CODE 85:01b4 and CODE 133:01ea.

Two debugger-only graphics modules cover the host renderer and Finder
presentation. They include shape/image pixels, transparent/opaque/fractional
alpha, padded strides and guards, 5000-pixel band crossings, malformed requests,
update-region and child-control preservation, drag artwork and bounded selection
enumeration after handle destruction. A closed front child hands activation to
the next decorated child, preserving active WDEF/CDEF appearance. Rename fields
use the same text painter without suppressing other item labels. They run sequentially in isolated profiles
at the initialized built-in Finder entry, without a native application SDK.
`check-graphics.sh quickdraw-host --build-only` compiles both modules without
requiring WSL-to-Windows process interop.

The aggregate Finder module also runs the saved Label-menu glyph/checkmark
reference masks and swatch geometry through QuickDraw. Swatch colours follow the
current editable `label-menu.r` artwork, preserving the user's palette edit.
The next child's activation is also delivered after native teardown finishes,
so Windows' focus restoration cannot leave modeless Get Info's owner painted
inactive. Decoration retains the parent identity until destruction completes.
