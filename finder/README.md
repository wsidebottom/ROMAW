# Native Finder

Finder is the **native Windows x64 desktop in ROMAW**, with classic Finder
menus, desktop icons and child folder windows inside a native Windows frame.
Starting `ROMAW.exe` without an application opens this desktop using the
initialized Toolbox rendering services. Starting it with an application follows the
existing classic runtime startup path. Applications opened from the native
desktop run in separate instances of the same executable. Those processes exit
when the guest quits, and the desktop remains usable while they run.

The configured host System Folder is blessed in the classic volume Finder
information using its actual catalog ID; the record is persisted alongside the
host volume metadata. Mounted HFS images keep their own blessing. Native Finder
uses the classic System Folder artwork for the configured folder and recognizes
its special subfolders by path. An unrelated directory with the same name stays
an ordinary folder; explicit custom icons retain their normal priority.

This project branches the behavior and artwork of `finder7/` into a native host
implementation. It reuses the shared Macintosh icon parser and special-folder
artwork in `finder-metadata/`, AppleDouble conventions and the
Finder-style copy dialog. Native C++ structures, Unicode paths, Windows controls
and file APIs replace the existing Finder's guest-memory and Toolbox adapters.
The classic `finder7/` tree is reference-only and is never built or launched by ROMAW.

Desktop drive icons appear immediately with standard artwork. Drive-root
metadata and custom artwork load independently in background threads, so a
disconnected mapped drive cannot hold up desktop creation or icon refreshes.
Completed results are published on the UI thread; each drive has at most one
pending read and completed snapshots are reused for at least 30 seconds.

The desktop uses ROMAW's `ppat` 17 (Blue Tiles), selected through
`system/resources/system/catalog.r`. The build decodes the resource's
indexed pixels and color table into an embedded native bitmap, preserving its
64 by 64 tile. Edit the System resource and rebuild to change its artwork;
the renderer does not synthesize a replacement pattern.

## Build and run

From the repository root in WSL, with the x86_64 MinGW GCC/G++ and windres tools:

```sh
make -C system
```

The x64 ROMAW wrapper generates the artwork, builds `libfinder.a`, and links
the frontend and the application manifest from
`platform/os/cygwin32/ROMAW.manifest` into `windows-binaries/bin/ROMAW.exe`. It reuses
ROMAW's shared icon parser and executable icon. Python requires Pillow as
listed in `system/requirements-native-finder.txt`.

ROMAW owns the native implementation in `finder`, its headers in
`include/finder`, the generator in `tools/romaw/native_finder`
and the regression fixtures in `system/tests/native-finder`. Generated files
belong in the selected ROMAW profile's `obj/native-finder` directory; see
[build profiles](../README.windows-build#build-profiles-and-output). There is no separate desktop executable
or dynamically loaded launcher.

Run `make -C system native-finder` to build just the frontend archive, or
`make -C system test-native-finder` to build and run its regression fixtures.
The generator also accepts `--debug` and `--output-dir` for isolated builds.
On Windows, use Python 3 with the requirements above and the MSYS2 x64 toolchain;
`FINDER_TOOLCHAIN` overrides its default `C:/msys64/mingw64/bin` location.

Existing `%LOCALAPPDATA%\Finder64` state is moved to `%LOCALAPPDATA%\Finder`
on first launch when the new directory does not already exist. The INI file and
Desktop/Trash contents are preserved, including Unicode names. Legacy saved
window paths, path-keyed geometry, icon positions and Put Away records are read
through compatibility mappings. If both directories exist, current Finder state
takes precedence and the old directory is left untouched.

## Classic software launcher

The compiled-in launcher always starts this ROMAW executable. Classic
applications, accessories and panels run in separate processes, leaving the
native desktop usable. The ROMAW desktop volume (guest path `ROMAW:`) points at
`%USERPROFILE%\.romaw\Apps` when present.

Macintosh type/creator information and resource forks come from `._filename`
AppleDouble files (also `.AppleDouble/filename` when reading existing Netatalk
metadata). The launcher recognizes CODE/cfrg applications, DRVR desk
accessories and cdev control panels. Applications open by double-click, and
documents can be dropped onto an application. Classic documents with type/creator
metadata use ROMAW's existing association handling. The header
`include/finder/launcher.h` defines internal host launch requests. The desktop owns and closes the returned process handles.

ROMAW determines whether to execute 68K or PPC code and supplies the Toolbox,
desk accessory and control panel hosts. Accessory/panel launches reserve a 2 MiB
system heap and a 4 MiB application heap, leaving space below the 24-bit address
limit for the host and guest display. Application memory follows ROMAW
configuration and SIZE resources. Its existing compatibility limits still
apply, including incomplete PPC/Carbon support. A separate ROMAW window is
used for each launch; this project does not embed emulated windows into native
folder windows or implement a shared classic Process Manager session.

## Shared rendering

The shared host graphics adapter calls ROMAW's real WDEF, CDEF, MBDF/MDEF
and Font Manager for classic frames, scrollbars, menus, buttons, checkboxes,
radios, text, edit-field borders and progress bars. Get Info shows stored type
and creator codes above Size; ordinary DOS files without Macintosh metadata use
the current per-user FTM. Icon labels remain single-line and clip without an
ellipsis. Native structures and RGB pixels cross the adapter boundary; packed
records, byte order and GrafPort state stay inside ROMAW.

Windows retains the outer application title bar, control models, keyboard and
mouse handling, accessibility, caret, clipboard and operating-system dialogs.
`text.cpp` keeps a bounded raster cache and paints the existing edit models'
text/selection with shared QuickDraw glyphs. Unicode characters unavailable in
MacRoman, nonclassic host fonts and unavailable rendering services use the
original host font rendering so names and editing remain readable. Image/icon
assets retain their existing native compositing path.

## Native file manager

- The main application retains its Windows title bar. Child windows use the
  System 7 Chicago title bar, six rules, and the actual `wind_close.icns`,
  `wind_zoom.icns` and `wind_grow.icns` artwork used by ROMAW's WDEF.
  Scrollbars use the shared `wind_scroll_arrow_active.icns`,
  `wind_scroll_arrow_inactive.icns`, `wind_scroll_arrow_clicked.icns` and
  `wind_scroll_thumb.icns` artwork, with
  the Control Manager's direction transforms and light-gray dotted track.
  Fixed-size thumbs and the nonclient mouse tracker share the same geometry;
  the list view supplies its actual scroll ranges, including row/column modes.
  Both scrollbar gutters and the framed grow icon are exactly 16 pixels.
  The title bevel uses the classic WDEF highlight and half-tinge shadow.
- Embedded Chicago and Geneva fonts, unwrapped Geneva 9 filenames and a 64 by 52
  icon grid match the classic Finder presentation. Existing classic icon positions
  and folder window bounds are read from Finder information in AppleDouble files.
  Subsequent native arrangements are saved separately; View > Clean Up arranges
  the current window on the grid.
- Classic Apple, File, Edit, View, Label, Special and Help menus; no Restart/Shutdown.
  The Apple menu reads the installed Apple Menu Items and Control Panels folders,
  including original SICN accessory artwork. Accessories and panels use the built-in launcher.
  File, Edit and Special use the original `fmnu` resources in
  `finder/resources/interface.r`, with native additions below the original items.
  Commands enable according to the selection and available history. Shortcut
  columns are measured separately. Cut/Paste moves items with their forks;
  Undo covers moves, renames, new folders and new aliases. Copies, moves and
  Put Away from the desktop run in independent background jobs with separate Stop
  controls. Completed moves are registered with Undo on the UI thread.
  Find opens with the name field. More Choices offers name, size, kind, label,
  creation and modification dates, version, comments, and lock state. Search
  the current folder (ROMAW on the desktop), a specific disk, all disks, or
  the selected items. Items Found lists up to 10,000 results. Enable **all at
  once** to display the list after scanning completes. Find Again repeats the
  last criteria and scope. Host searches skip junctions and symlinks. Mounted
  images, including read-only images, are searched through the live catalog
  without extracting file data. Make Alias creates native Windows
  `.lnk` shortcuts; opening them routes the original through the native desktop or its built-in launcher.
- Large icons, compact 16 px Small Icons and a detailed List view. The seven
  resource-named columns sort by Name, Date, Size, Kind, Comments, Label and
  Version; column clicks reverse direction. Each folder remembers its view and
  sort independently of large-icon positions. See `REFERENCE_AUDIT.md` for the
  finder7 comparison and scope.
- In list view, the disclosure triangles use `arrow_open.icns` for collapsed folders
  and `3061.icns` for expanded folders. Click a triangle or use Left and Right
  on a focused folder. Children appear under their parent and each sibling
  group follows the current sort.
- Right-click an item or the folder background for its contextual commands.
  Clean Up Selection aligns selected icons to the grid while leaving other
  icons in place. Spring-loaded Folders opens the highlighted folder after a
  short drag hover or immediately when Space is pressed. Windows opened by the
  drag close after drop or cancellation.
- Get Info, rename, new folders and Windows Recycle Bin integration.
  Modeless Get Info child windows use the Finder resources for files, folders,
  stationery-capable documents and applications. The Memory Requirements panel
  reads Suggested size from `SIZE` -1. Closing Get Info saves Minimum and Preferred
  in `SIZE` 0 for the next launch. Locked files and read-only images display the
  values without enabling edits. Comments are editable in host AppleDouble
  metadata (up to 200 MacRoman bytes). The Locked control updates the host
  read-only flag, and Stationery Pad updates the Macintosh Finder flag.
  Opening a pad asks for a new document name, copies both forks and opens the
  unlocked copy. Copies from read-only images go to the Finder desktop.
  Text editing uses the Edit menu's normal clipboard commands.
  Get Info shows size on disk alongside the exact logical byte count.
  Edit an item's name in Get Info and press Return or close the window to save.
  Escape restores the last saved name. Locked files and read-only images
  display the name without enabling edits.
  Click the Get Info icon to select it. Use Edit > Copy, Paste, or Clear
  to exchange or remove custom artwork. Copy also publishes a Windows bitmap.
  Paste accepts Finder icon families and Windows clipboard bitmaps. Locked files
  and read-only mounted images allow copying their artwork.
  Folder totals are calculated in the background. Both totals include data and
  resource forks, including on read-only mounted images. For host files with
  resource forks stored in companion files, size on disk includes the allocation
  of the companion that holds the resource fork.
  Alert geometry comes from `ALRT`/`DITL` 6011; notices
  use classic fonts, icons and rounded default buttons. Find uses `fvew` 11014
  and 11015 and the criteria names and operators from the Finder menus.
  About This PC uses `fvew` 10001 with real host memory figures.
  Show Clipboard is a live modeless view of the Windows clipboard: Unicode/ANSI
  text, file paths and bitmap images. It refreshes without activating Finder.
  File Copy/Cut publish standard Windows file-list and preferred-action formats;
  Explorer can paste these selections, and Finder can paste Explorer selections.
  Macintosh companion files are published too and remain paired by Finder's copy
  backend. A successful Cut clears only the original clipboard sequence after
  the move completes; failures/cancellation and newer host clipboard contents
  remain intact.
  Escaped legacy MacRoman names display their original characters while file
  operations retain the exact host paths.
- Desktop volumes, shared-drive artwork, ROMAW volume and movable Trash.
  Clear, Move to Trash and unmodified drops on Trash use Windows' recycle
  routine for each source drive, including Macintosh sidecars. Windows handles
  same-name discarded items and warns before permanent deletion when recycling
  is unsupported, disabled or unavailable for an item. Ctrl/Option-copy drops
  are rejected rather than silently deleting the source.
  Opening Trash opens the native Recycle Bin, where Windows' Restore commands
  retain original names/locations. Get Info opens its native Properties. Empty
  Trash uses Windows' confirmation, progress and emptying routine for all bins,
  including items recycled from other applications. Embedded empty/full Trash
  artwork follows native bin status, including changes outside Finder.
  Existing contents in the old `%LOCALAPPDATA%\Finder\Trash` folder are left
  intact; newly discarded items no longer go there. Its saved Put Away
  records still work if the old folder is opened directly. Native-bin restoration
  is provided by Windows, rather than Finder's session-local Undo.
- Page Setup and Print Window use the Windows printer system with classic frames,
  fonts and controls. Print dispatches native documents through installed Windows
  print handlers. Classic documents without a native handler remain ineligible.
  Sharing opens Windows folder-sharing properties. Eject disconnects mapped drives
  or asks an eject-capable removable device to release its media. Erase Disk opens
  Windows formatting after confirmation, for removable volumes only; the system,
  program and preferences volumes are excluded. Sharing, formatting and printer
  driver property pages remain Windows-owned dialogs.
- Embedded classic icons, application/custom icons from resource forks and
  Finder label colors. The Label menu uses the supplied classic reference's
  order, palette and pixel geometry from `finder/resources/label-menu.r`, including a
  readable checkmark bitmap. All labels share the edited `label-menu.png` artwork
  through its Finder Rez `cicn` object. The label menu selects the resource and
  palette index in Rez, then changes only that color-table entry for each label,
  as Finder 7 does. Masks, pixel indices and full-size swatch geometry are retained.
  Filename tinting follows the converted Finder; the portable SuperMario-derived
  icon palette transform is reused directly.
  Label changes preserve existing sidecar entries.
- Completion refreshes and progress-window hiding share a nested redraw batch.
  Direct QuickDraw/GDI frame, scrollbar and desktop painters honor the same
  suspension as native controls, then present the final hierarchy together.
- Multiple background copies with independent Stop buttons, byte progress,
  remaining time and source/destination details always visible. The copy view's
  geometry comes from editable `finder/resources/resources.r`, `fvew` 8751.
- Copies preserve AppleDouble forks, reserve distinct duplicate names and stage
  unfinished logical items privately before publication. Stop removes incomplete
  items and retains already completed items. Junction/symlink copying is rejected.
- Open folder windows, including descendants, follow completed moves and Undo.
  Windows for displaced/deleted folders close rather than showing private
  replacement storage. Completed results wait until dragging and inline rename
  release their row snapshot. Virtual drive icons remain desktop objects and
  cannot be transferred as ordinary host folders.
- Window geometry, open folders and icon positions saved in
  `%LOCALAPPDATA%\Finder\settings.ini`.

Desktop items use the ROMAW volume's existing `Desktop Folder` when present;
otherwise they use `%LOCALAPPDATA%\Finder\Desktop`. Desktop items with a saved
classic position take their coordinates from Finder Info. A native drag or
Clean Up saves a local override until the item's classic coordinates change.
Icons use the same classic Desktop DB/DF
storage as ROMAW's Desktop Manager on configured Macintosh-compatible
volumes. The native reader looks up creator/type families, including monochrome
masks and 4/8-bit large/small artwork. Custom item icons retain priority.
Missing databases trigger a background volume walk that registers BNDL/FREF
families; opening folders also discovers new or updated owners. Database and
source changes invalidate displayed artwork without restarting Finder. Repeated
discovery does not append identical bitmaps. Malformed databases are preserved;
discovered source bundles can still supply session artwork. Corrupt/unsupported
resource forks cannot abort discovery of other applications.
Copy/Paste shares the Windows clipboard file list and preferred copy/move effect. Dragging normally moves within
one host volume and copies between volumes. Alt/Option (or host Control) forces
copying; Shift forces moving, and the copy modifier takes precedence. An
Alt-drag within the same folder duplicates instead of moving icon positions.
Real transfers reserve exact destination names; Duplicate reserves unique copy
names. Existing items offer Replace, Skip and Stop through a job-owned waiting
state. Other workers continue while one job waits. Replacement checks the
approved destination identity/tree before publication and preserves old data and
resource streams in recovery storage for the job/Undo lifetime. Failed
publication attempts rollback; changed backups are retained rather than erased.
Cross-volume moves verify source bytes and identity under no-write locks before
removing the source. Stop discards unpublished items and keeps committed ones.
Junction/symlink transfers and overlapping source/destination trees used by
active moves are rejected. Opening a recognized disk image (double-click or File > Open) mounts it in the
current ROMAW runtime instead of sending it to a Windows file association. Image extensions are matched
without regard to case, including `.image`, `.iso`, `.cdr`, `.toast`, `.dmg`, `.img`, `.dsk`, `.hfv` and `.hda`.
Mounts use the existing access policy and persistent selections. Incomplete images opened through the
direct read-only projection mount their available contents without a repair/backup/replacement question. Cancelled mounts do not produce an error alert. Macintosh applications opened afterwards see those
mounts. Mounted image volumes appear on the desktop, including restored mounts. Double-click a volume
to browse its Macintosh catalog directly; Put Away, Eject or dragging its icon to Trash unmounts it.
Image-file artwork comes from the extension mapping in `tools/generate-folder-icons.py` and the shared
`bitmaps/file_*.icns` sources. The Windows build stages matching 16/32-pixel `.ico` files under
`windows-binaries/bin/image-file-icons`. Run `register-image-file-types.ps1` beside the executable to
register the ten extensions for the current Windows user; the script saves the previous registry
values before changing them. Windows opens these files with `ROMAW.exe -mount "%1"`.
For a machine-wide installation, run the script with `-AllUsers` from an administrator PowerShell.

Mounted folder windows read their DInfo/DXInfo and item FInfo/FXInfo directly from the live catalog.
Saved folder bounds, icon/small-icon/list view selectors and initialized icon coordinates seed the
native layout, including small classic windows. Classic list selectors also choose the matching sort
column. Mounted folders use ImageViewsNative for local overrides, avoiding old generic view defaults. Desktop/DB/DF metadata continues to supply icons, application associations and comments.
On writable mounted images, manual icon arrangements, view choices and folder window bounds are saved through the
live File Manager, preserving other FinderInfo, dates and both forks. Default optical mounts and host
read-only files stay unchanged; optical images require an explicit read/write mount. Read-only images
and failed writes retain local ImageWindowsNative/ImagePositionsNative overrides. Successful writes
remove those overrides so a fresh session reads the image's layout. Older native defaults do not mask
the image's initial layout. Layout reads leave the image untouched.

Get Info uses the normal file/folder/application windows for mounted items, with live catalog dates,
both-fork sizes, Macintosh locations, source Desktop comments and bounded `vers` resource reads.
Get Info saves comments for files and folders on writable images in the volume's Desktop DB,
with a limit of 199 MacRoman bytes. It also supports file locks, the Stationery Pad flag and
application SIZE overrides. Volume Get Info shows the backing image and capacity.

Mounted image windows browse the live catalog. Drag between volumes to copy, or drag within a writable
image to move by catalog ID. Option or Control forces a copy. Duplicate and same-image copies read both
forks through the mounted File Manager, in bounded chunks. New Folder, inline Rename, Label,
Open Enclosing Folder and Paste of copied host items work in image windows. Move to Trash and a drop on
Trash require confirmation of permanent deletion because Windows cannot recycle inside an image.
Read-only images reject writes. Destructive changes check catalog identity and honor file locks.
Replace, Skip and Stop apply to image destinations as well as host destinations. Image replacement
rechecks the selected catalog tree and sets aside the old item before publishing the new one. If
publication fails, it restores the old name. Stop removes unfinished copies and retains completed items.
An open catalog guard prevents Put Away while an image operation is active.
Application bundles and custom file/folder icons are read through the Resource
Manager with preloading disabled; only decoded display icons are retained. Documents can also use
their creator application's bundle when that application appears in the same folder. Classic `alis` resolution and full classic Finder
extensions are not implemented. Native shortcuts are distinct from classic Mac
alias files. Undo history lasts for the current session.

## Tests and provenance

Implementation contracts are documented in the source and public headers:

- `main.cpp` owns UI registries, window state and cached artwork. A window only
  adopts heap state after successful creation, so failed creation cannot cause a
  double deletion in `WM_NCDESTROY`.
- `files.cpp` handles bounded big-endian metadata and independent copy workers.
  Atomic counters/state cross threads directly; progress strings require the
  job mutex. Destination names are reserved on the UI thread before work starts.
  Unfinished staging is removed on cancellation, while completed items remain.
- `actions.cpp` pairs data/fork relocations and records Windows identities for
  Undo. Created files receive additional size/time checks before removal. Search
  windows consume owned snapshots of a worker's locked result vector.
- `classic.cpp` owns shared fonts/brushes and borrows artwork handles. Its menu
  records keep stable addresses for owner-draw callbacks; nested notice/popup
  loops preserve focus and quit messages.
- `drag.cpp` resolves the nearest folder in the window hierarchy. Drag endpoints
  come from their input messages, rather than a cursor position sampled after
  later events have already arrived.
- `settings.cpp` preserves serialized settings during directory migration and
  resolves old path hashes on read. The historical hash seed is part of that
  persistence format.

The build tool documents generated-resource ownership and asset ordering. Edit
the maintained resource sources or generator, rather than generated `.inc`
tables in `obj/`.

`make -C system test-native-finder` checks the real file backend, menu painter
and built-in launcher's four launch kinds, quoting, input validation and
child-process ownership. The tests link the same internal launcher as ROMAW.
After building ROMAW and this test, run
`python system/tests/native-finder/integration_test.py --desk-hosts` with Windows x64 Python
to verify native startup bypasses SDL/guest initialization, run original 68K/PPC
fixtures through the embedded launcher, and check the installed Calculator and
Sound control panel reach their guest UI and close cleanly. The latter two
require the local classic system assets; these are startup checks, not complete
application workflows.
The Label-menu test renders the shared popup painter to a bitmap, compares all
label glyph masks and swatch pixels against the supplied reference, and compares
every menu RGB pixel below its cursor halo. Pointer pixels are excluded from the None
text reference. The output is retained at `system/obj/native-finder/windows-x64/label-menu.bmp`.
Copy tests deliberately hold several jobs inside real CopyFileEx callbacks to
verify overlap, unique destinations, independent cancellation and exact data and
resource-fork preservation. Other checks cover nested folders, Unicode names,
labels, comments, malformed metadata and Windows argument quoting. The action
suite covers transaction rollback, Undo identity guards, aliases and recursive
Unicode search. Set `FINDER_CROSS_VOLUME_TEST_ROOT` to a local test directory
on another volume when running `actions-test.exe` to check folder moves and Undo
across volumes. The same setting enables physical cross-volume background
folder/fork move and Stop checks in `files-test.exe`. The launcher test records the real child process arguments for
applications, documents, DAs and panels.

`integrated-launch-test.exe --probe ROMAW FILE KIND RESOURCE_ID` additionally runs a
terminating classic fixture through the actual ROMAW runtime (kind 1 for
applications). The repository's original 68K and PPC probe generators can provide
fixtures without redistributing Apple binaries. The integration checks use Windows
GDB to verify a live guest event loop before requesting a clean close. These are
startup/return checks, not comprehensive application compatibility tests.

The icon parser retains its MIT license in `finder-metadata/LICENSE`.
Existing artwork and reconstructed Finder material retain their repository
provenance. No Apple System 9 binaries are included in this project.

Folder Zoom uses classic user/standard rectangles rather than `WS_MAXIMIZE`.
The standard rectangle fits all icon/label bounds (or list rows/columns), adds
the status strip, gutters and frame, and is limited to the desktop workspace.
A second click restores the previous rectangle. Manual moves/resizes establish
a new user rectangle; host resizes refit standard-state folders. This follows
the separate rectangles used by SuperMario's `WindowMgr.a` / `WStateData`.

Close and Zoom track the visible resource tile until mouse-up, as classic
`TrackBox` does. Dragging away, Escape, capture loss or app deactivation cancels
the action; returning to the tile before release rearms it. Both buttons use
the Window Manager's `wind_close_clicked.icns` artwork while pressed. Hidden
buttons on inactive titles only activate/drag the window. A sibling window paints the
fixed scrollbar gutters and grow tile, forwarding input to the list and grow
hits to the parent frame even when scroll ranges are inactive. Its window
region excludes the content viewport, so it cannot cover icons or intercept
their clicks. The list's actual window equals the visible content area; native
scroll ranges therefore include complete bottom-row icons and labels rather
than counting the gutter as usable space. Caller resize requests update both
windows, and destroying the list releases its gutter window.

### Shared Macintosh storage layouts

The native Finder and guest File Manager use `rsys/mac_storage.h` to decode
bounded AppleSingle/AppleDouble entry tables and compact Finder information.
Only descriptor offsets and lengths become host-order integers; Finder
information and resource-fork payloads retain their Macintosh byte order.
Platform I/O and guest-pointer marshalling remain with their callers.

Native discovery supports `._Name`, `.AppleDouble/Name`, validated `%Name`
containers, and the split `.finf/Name` plus `.rsrc/Name` layout used by
BasiliskII. Copies and cross-drive moves follow the destination convention,
including nested folders, stationery creation and resource-only icon carriers. Existing `._` files or
`.DS_Store` declare OS X, `.AppleDouble` declares Netatalk, `.finf`/`.rsrc` declare
BasiliskII, and validated `%Name` containers declare percent storage. An empty
destination inherits the nearest declared ancestor up to its drive/share root;
without a declaration, OS X `._Name` is the default. OS X takes precedence in
mixed directories. Source conventions never change the selected output format.
Conversion happens in private staging before publication. Cross-drive moves verify
the staged original streams and hold source locks throughout conversion; source
files are removed only after the converted destination is published. Failed or
cancelled conversion retains the source. AppleDouble layout changes retain complete
containers, including extended FinderInfo/ATTR records. BasiliskII cannot represent
AppleDouble comments or extended attributes; conversions requiring their loss fail
before publication.
Combining split forks with extended FinderInfo also fails rather than relocating
unvalidated attribute offsets. Rename and same-drive move preserve the existing
convention.
The split layout was checked against macemu2's `extfs_windows.cpp` and
HfvExplorer's `CFATVolume.cpp` / `HFVExplorerDoc.FatVolumes.cpp`; no source from
those projects was copied. AFP named streams and MacBinary are not implemented
by this native reader.

Folder custom icons resolve `Icon<CR>` through raw, percent-escaped and SMB/SFM
U+F00D spellings. Artwork loading and cache signatures use the same alias list;
the emulated Windows filename adapter shares the naming constants. A data fork
is optional when AppleDouble or split `.rsrc` metadata supplies the icon family.

Configured Macintosh drives are registered at startup. A populated Desktop DB
is used immediately; an absent or empty icon index triggers background source
discovery. Opened folders also register their application bundles. Run
`bash system/tests/check-files.sh mac-storage` for bounded layout checks and the
native Finder suite for split-fork copying, metadata updates and rename rollback.

Both readers and writers use the shared descriptor codec. Guest descriptor
updates validate the new payload extent before publishing it and retain the
caller's file mark. Full compact records expose FInfo plus FXInfo (32 bytes);
new split metadata uses this layout. Historical short records still expose only
their complete FInfo, preserving their trailing reserved bytes.

Synchronous Finder work uses nested `classic::WaitingCursor` scopes. Styled
windows and lists retain the busy cursor through `WM_SETCURSOR`; scope exit
restores the previous cursor on success or exception. Folder/icon loading,
metadata edits, moves, Trash and Undo use these scopes. Background copying and
searching keep their progress UI and leave the rest of Finder interactive.

Mounted image documents use existing Desktop DB/DF application records, icon
families and comments. Reads traverse source-backed B-tree nodes and fetch only
the requested icon range; they create no Desktop files or lock files on the
image. Custom resource-fork icons retain priority. Classic Desktop resource
forks provide a fallback, and opening can discover an application by creator
through a bounded catalog scan if its stored registration is missing or stale.

Dragging from an image to a host folder streams both forks directly to the
chosen destination, preserving FinderInfo through AppleDouble. Stop discards
an unfinished destination item. Folder copies preserve their descendants and
root metadata. Image contents are never expanded for browsing or launching.
Native Finder launches forward every mounted source and its access selection;
legacy volume-qualified Launch targets resolve their own working directory.

Mounted-volume custom artwork takes priority over generic disk icons. Finder
reads the root `Icon\r` resource fork, including packed `icns` families, then
`.VolumeIcon.icns` data-fork bitmap ranges, then an older Desktop-stored custom
family. Missing or malformed artwork falls back to the usual volume icon.
Source image timestamp changes invalidate the desktop volume artwork cache.
These lookups never extract image files.
