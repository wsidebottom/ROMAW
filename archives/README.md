# Native Macintosh archives

Finder owns ZIP (`.zip` / `ZIP `, `Zip `, `pZIP`), classic StuffIt
(`.sit`, `.sit5` / `SIT!`, `SITD`), BinHex (`.hqx`, `.bhx`, `.binhex` /
`BinH`) and MacBinary (`.bin`, `.macbin`, `.macbinary` / `mBIN`). Built-in
associations take priority over the FTM and Macintosh creator associations;
automatic application registration cannot replace them.

The portable codec uses miniz **3.1.2**, commit
`77d0dce8627735138c51770d1799a1ef48f2117d`, under its retained upstream license,
and the pinned StuffIt C ABI described in [the bridge guide](../stuffit/README.md).
The small `miniz_export.h` is the static-library equivalent of its generated export header.
Sources: [miniz](https://github.com/richgel999/miniz),
[BinHex RFC 1741](https://www.rfc-editor.org/rfc/rfc1741),
[MacBinary format](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxcmail/ec1a8b63-ae1e-47d2-ba3e-473a4b27eb45).

ZIP creation follows `ditto -c -k --sequesterRsrc --keepParent`: normal UTF-8
entries hold data, and `__MACOSX/<parent>/._<name>` entries hold complete
AppleDouble containers. Extraction attaches those containers to their logical
items, including folder metadata and empty data forks. It never exposes
`__MACOSX` as a user folder. ZIP CRCs, BinHex header/fork CRCs and MacBinary
II/III header CRCs are checked. MacBinary I has no CRC, and MacBinary has no
fork checksums. StuffIt fork lengths and available CRC-16 checksums are checked;
the format's method-15 checksum fields are not interpreted as CRC-16.

The codec supports classic StuffIt through version 5, including data compressed
by the pinned engine's supported methods; it does not support StuffIt X.
Encrypted ZIP/StuffIt entries and unsupported methods fail without publication.
No archive scripts or applications are launched.

Inputs and aggregate expanded output are bounded to 256 MiB, 65,535 entries
and 128 path levels. Before creating staging output, the codec rejects absolute
paths, traversal, alternate data streams, links, reserved names, duplicate names,
file/folder collisions and malformed AppleDouble spans. Finder additionally
checks Unicode case-equivalent collisions before writing its private cache.

`Extract Here` requires a writable Mac-compatible destination. `Extract To…`
opens a classic, movable folder picker with host folders and live mounted
images. Windows-format drives are excluded; mapped Apps paths retain their Mac
semantics. Destination policy is rechecked immediately before starting the
existing transactional host or image import. Images that cannot represent
extended AppleDouble entries reject those archives rather than discard metadata.
The original archive is always retained. Temporary extracted input remains owned
by the copy job until its worker/import has finished.

Windows-bound folder transfers offer ZIP or StuffIt. Files inside the private
stage are normalized to OSX metadata first; the chosen encoder verifies its
result before the normal transfer publishes it and retires move sources.

Individual files from Mac-compatible storage (or with Macintosh companions)
sent to Windows drives become `original-name.bin`. Ordinary Windows files remain
unwrapped. MacBinary stores standard data/resource forks and primary Finder
fields; its secondary header retains complete FinderInfo and AppleDouble entries
without duplicating a large resource fork. ROMAW restores that metadata when
extracting the package. Other readers can skip the secondary header and recover
the standard forks and primary fields. Names must fit MacRoman in 63 bytes
(MacBinary III for up to 31, II for longer names); unsupported names or metadata
fail before publication. Source files and companions are retired only after a
verified package is published, including moves within the same host volume.

Windows-to-Mac transfers decode valid `.bin`, `.macbin` and `.macbinary` files
using their contained names, both forks and FinderInfo. Other Windows files use
the current FTM's last enabled Mac-direction mapping, including its extension
stripping setting. Explicit Macintosh companions retain their names and metadata.
Folders apply these rules to their contents; final-name collisions fail before
publication, while names vacated by another converted file are supported.
Undeclared destinations retain the OSX default.

On Windows drives, Finder reads MacBinary metadata and resource forks without
reading the data fork for listings. Get Info shows the contained codes and flags;
custom icons and application BNDL icons are available while the container remains
owned by Finder's archive handler. Extract before editing packaged Mac metadata.

Writable-image imports use the same decoded names and FTM rules. Windows-to-image
moves verify staged forks against locked original file objects, check source
identity/tree/metadata, and flush and publish the image item before deleting any
source files or companions. Unrepresentable image names/metadata and failed
writes retain the originals. Mixed host/image move batches and moves from other
host storage formats into an image remain unsupported.

Run `python3 archives/tests/check.py` under WSL for independent ZIP, BinHex and
MacBinary fixtures, actual StuffIt decoding, corruption/expansion/cycle checks,
and rejection before output. It also requires `unar` for independent MacBinary
fork read-back. `python3 stuffit/tests/check.py` additionally tests
StuffIt output with independent `lsar`/`unar` decoders. Windows frontend compile
and runtime validation are separate requirements.

For the Windows UI regression, prepare the existing `DesktopMetadata.hfv`
image fixture, build the independent reader in WSL, then run the staged normal
executable through the private desktop harness:

```sh
cmake -S disk-images -B build/archive-image-reader -DCMAKE_BUILD_TYPE=Release
cmake --build build/archive-image-reader --target archive-image-readback --parallel 10
```

```powershell
python system/tests/native-finder/archive_ui.py --runtime windows-binaries/bin/ROMAW.exe
```

This covers native Open/type associations, all four extraction formats,
classic picker acceptance/cancellation, Windows-drive refusal, real C:/D:
folder packaging, successful/failed move source handling, and extraction into
and from a writable HFS image. Persisted image forks/FinderInfo are read back
through the separate disk-image library. Fixtures and the executable hash are
recorded in `build/archive-ui-report.json`; the user's desktop and clipboard
remain separate from the test station.
The same suite checks automatic MacBinary copy/move packaging from OSX,
Netatalk, BasiliskII and Executor layouts, same-volume conversion, unchanged
ordinary Windows files, and retained sources after a failed MacBinary move.

The Windows-to-Mac regression uses disposable C: sources and a temporary drive
letter backed by a private D: directory; real drive roots and user preferences
are not changed. Run each destination format serially:

```powershell
python system/tests/native-finder/archive_ui.py --runtime windows-binaries/bin/ROMAW.exe --import-format OSX
python system/tests/native-finder/archive_ui.py --runtime windows-binaries/bin/ROMAW.exe --import-format Netatalk
python system/tests/native-finder/archive_ui.py --runtime windows-binaries/bin/ROMAW.exe --import-format BasiliskII
python system/tests/native-finder/archive_ui.py --runtime windows-binaries/bin/ROMAW.exe --import-format Executor
```

These check contained filenames, legacy MacBinary I/II/III, full secondary
FinderInfo, FTM names/codes, explicit metadata, folder conversion/collisions,
same-volume moves, and actual Get Info custom/BNDL icon clipboard bytes. The OSX
run also checks writable HFS copies/moves by independent persisted-fork read-back,
including raw AppleDouble/percent companions and a disk-full failure. Reports
record the tested executable hash in `build/windows-import-<format>.json`.
