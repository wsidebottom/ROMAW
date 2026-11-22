# Native Carbon compatibility

`libromaw-carbon.a` incrementally replaces CarbonLib functions with native C
implementations. It records explicit guest ABI signatures and CarbonLib 1.4
code offsets or re-export origins in `src/exports.c`. Implementations are grouped
by subsystem. No Apple executable bytes are compiled into this archive.

Missing APIs, foreign reference profiles and retained workers use real-library
fallback. Original library initialization and dependency ordering remain active;
Carbon's event bookkeeping must run normally. Binding counts describe native
entry bodies, including bodies that still call original workers, and do not
establish complete Carbon or application compatibility.

## Build and validation

Build/stage the integrated archive through the root Windows build:

```bash
JOBS=10 make
make -C carbonlib check OBJ="$PWD/system/obj/carbonlib"
```

The second command runs the standalone Windows archive fixture. The integrated
build uses the selected profile's `carbonlib` object directory; it stages the
archive with the dependency prefix and executable. Changes to the archive must
relink ROMAW. See [build profiles](../README.windows-build#build-profiles-and-output).

For each CarbonLib implementation batch, run the shared regression app in
PowerShell. One launch reports Values, Collections, Frameworks, ClassicAccess
and Toolbox results.

```powershell
./carbonlib/tests/check-corefoundation.ps1
```

Add new assertions to the relevant group in that app. Use `-Group Frameworks`
or another group name for a focused run. `-Group Textension` runs Textension
and Script Manager type selection in the shared app. Add `ATSUI` for the build's
font and layout checks under a separate process because the saved FontManager
patches the menu path that Textension exercises. Both results go into one report.
The runner names failing assertions in `validation.json`. Use `-Full` when
validating the separate synthetic fallback profile, and `-Engine Both` for PPC
engine changes. Add `-Plan` to see the app and launch counts before generating
fixtures. Run the affected groups together after the implementation batch is
ready.
Use MyABCD itself for startup checks. Each controlled startup failure requires
its own launch. Generated fixture fragments contain original test code. Apple
runtime libraries and disassemblies remain ignored local assets.

Specific ABI, ownership and failure cases are recorded in
[API contracts and validation notes](docs/api-notes.md). For additional real-object
differential groups use `synppc/tests/check-runtime.ps1 carbon-cf-real -Group NAME`.
The [PPC suite guide](../synppc/README.md#romaw-integration) describes application
and controlled-diagnostic checks.

## Coverage reporting

Run from the workspace root against the ignored local reference metadata:

```sh
python tools/carbon-coverage.py --reference <CarbonLib/pef.json> --application <MyABCD>
```

The report at `build/carbon-coverage.json` separates strong/weak application
imports, native archive bindings and missing bindings. Optional `--trace
<stderr.txt>` reports observed calls separately; calls do not establish API
completeness. Recompute the report for the checkout rather than carrying forward
cumulative totals from earlier batches.

The tool discovers sibling companion metadata. Supply, for example,
`--companion-reference StdCLib=<StdCLib/pef.json>` for separately installed
fragments. Per-library totals overlap through re-exports; companion-only bindings
require matching metadata before the audit succeeds. The list of unaudited
companions includes the sources of pending re-exports. Other libraries do not
gain an implementation merely by exporting the same name.

## Reference material

- [API contracts and validation notes](docs/api-notes.md)
- [Disassembly and migration audit](../synppc/docs/carbon-native.md)
- [MyABCD investigation](../synppc/docs/myabcd-startup.md)
- [PPC CFM ownership and private copies](../synppc/docs/cfm.md)

Preserve file-specific copyright, provenance and licensing notices. Reference
SDKs establish interfaces; a resolved export or an SDK declaration is not an
implementation or evidence of complete application compatibility.
