# Type-9 compressed resource regression

Build the original 68K probe in WSL with the installed Retro68 toolchain:

```sh
cmake -S system/tests/type9-resources -B build/type9-probe \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/retro68-toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
cmake --build build/type9-probe -j10
python3 system/tests/type9-resources/fixtures.py
```

Run `./system/tests/type9-resources/run-tests.ps1` in PowerShell after building
and staging ROMAW. The GDB driver requires an explicit guest success marker.
The generated files stay under `build/type9-resource-check`.

The original decoder in `decoder.s` is embedded as machine code in `fixtures.py`.
It implements the three-entry type-9 protocol, validates the separate full header,
and copies literal bytes instead of using Apple's compression algorithm. This
checks custom-decompressor preference and the calling convention for odd, even,
and large resources. Malformed headers, negative sizes, missing decompressors
and invalid entry offsets must return -186. An executable-resource reload checks
that decompression invalidates stale CPU translations at reused heap addresses.

To compare a locally installed System file against python-rsrcfork, add
`--system /path/to/raw/System/resource/fork` to the fixture-generation command.
This optional check uses the reference decoder installed under
`build/resource-reference`; it copies local decompressors into ignored fixtures.
No Apple resources or code are committed. The System 7.0.1 fork from this
workspace supplied 390 comparisons, in addition to five original test cases.

The fix also preserves the existing type-8 and native dcmp-0 paths. Run
`bash system/tests/check-toolbox.sh dcomp0` for the bounded native decoder checks.
