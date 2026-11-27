# ROMAW Finder metadata support

This library provides the bounded resource-fork and icon-file parsers,
special-folder, volume and generic type artwork, and .DS_Store window/layout
persistence shared by ROMAW's native Finder and classic file dialogs.

Maintained implementation and headers live in src/ and include/finder_metadata/.
The build generates icon tables from the workspace bitmaps/ directory using
tools/generate-folder-icons.py. No external Finder checkout is required.

Source files retain their original MIT license declarations.

Custom icons and application BNDL/FREF artwork precede the generic fallbacks
for `dfil`, `DFIL`, `FNDR`, `ZSYS`, `APPL`, `TEXT`, `PICT`, `pref`, `INIT`, `shlb`,
`kfil`, `FFIL`, `ffil`, `sfil` and `tfil`. Type codes are case-sensitive;
`FFIL` and `ffil` select their respective artwork.

`finder_decode_icon_file` renders ICO or ICNS bytes as straight-alpha RGBA at
16 or 32 pixels. Windows Imaging Component decodes ICO bitmaps and PNG images;
the shared parser also handles classic indexed ICNS families and editable
`exfr` frames. Inputs are limited to 32 MiB and checked before decoding.
`finder_parse_icon_file` preserves existing classic palette roles, or produces
mono, 16-colour and 256-colour families for Toolbox consumers. Native previews
retain full RGB and alpha; classic resource masks use the 128 alpha threshold.
The resource compiler accepts both `.ico` and `.icns` artwork paths in the
shared manifest. Cursor `.cur` handling is unchanged.

Run the icon regressions from WSL, then run the resulting executable on Windows:

```sh
python3 system/tests/test-image-file-icons.py
python3 system/tests/test-icon-file-decoding.py
```

```powershell
& .\build\icon-file-decoding\icon-file-test.exe D:/Source/ROMAW/build/icon-file-decoding
```

Run the preserved metadata regression suite from WSL.

```sh
cmake -S finder-metadata -B build/finder-metadata-check \
  -DFINDER_METADATA_BUILD_TESTS=ON
cmake --build build/finder-metadata-check -j10
build/finder-metadata-check/metadata-test
```
