# Regression suites

Select focused checks from the subsystem suites below. Commands run from the
workspace root; Bash suites run in WSL, and PowerShell suites run on Windows.
Cross-built executables require Windows interop or a separate Windows launch.
See [the build guide](../../README.windows-build) for prerequisites and profiles.

Run a suite without arguments, or with `--list`, to list its checks. Select one
check by name and pass its existing arguments after that name:

```bash
bash system/tests/check-media.sh aiff
bash system/tests/check-toolbox.sh toolbox-additions parameter_preferences
bash syn68k/tests/check-core.sh fpu --oracle
bash synppc/tests/check-core.sh core
```

```powershell
powershell.exe -NoProfile -File system/tests/check-runtime.ps1 help -ROMAW D:\Source\ROMAW\windows-binaries\bin\ROMAW.exe
```

`./system/tests/check-runtime.ps1 ecf` generates a small PPC window application
inside an isolated user profile. It checks preferences save/reload, Defaults and
application overrides, direct Gestalt rules, heap validation and launch restart
arguments, then lets the real window creation/disposal path finish normally.
The suite uses x64 GDB by default, retains its GDB script and logs under
`build/ecf-test.*`, bounds each debugger run to 60 seconds, and restores the
caller's profile environment. It does not depend on the Appearance graphics
regression application or modify the user's saved preferences.

The live clipboard probe replaces the Windows clipboard with generated test data.
It checks transfers through the staged runtime, including QuickDraw pixel comparisons
for DIB/PICT round trips. Run it explicitly with
`system/tests/check-runtime.ps1 clipboard-live -AllowClipboardChanges`; optional
`-ROMAW` and `-Application` arguments select a build and a disposable classic app.
Other clipboard fixtures intercept Windows calls and preserve the real clipboard.

`./system/tests/check-graphics.ps1 copybits-colorize` checks indexed `srcCopy`
colorization through initialized ROMAW on Windows. It covers 4/8-bit sources,
8/32-bit destinations, gray and colored backgrounds, conversion-cache reuse,
and preservation of the source pixels, palette and port colors. `-Executable`
selects an isolated staged build; this fixture does not require SimFarm assets.

`./system/tests/check-graphics.ps1 pixpat-lifetime` checks disposal of patterns
installed in multiple GWorlds, clearing of pen/background/fill references,
subsidiary deallocation, replacement bit patterns and actual 8/32-bit painting.
It reproduces Kid Pix's preview lifetime without requiring its application assets.
`-Executable` selects an isolated staged build.

`./system/tests/check-graphics.ps1 window-exposure` checks bringing overlapping
windows forward after application drawing changes a window's `visRgn`. It
verifies that covered pixels are repainted and invalidated, uncovered pixels
are preserved, and repeated activation leaves the window order intact.
It also checks occlusion, exposure and hiding inside `BeginUpdate`/`EndUpdate`,
where the saved visibility snapshot must be invalidated and recalculated,
plus mismatched `EndUpdate` calls and origin changes during an update.
The exposure calculation follows `BringToFront` in the local SuperMario
`Toolbox/WindowMgr/WindowMgr.a` reference identified by
[`references/finder7/SOURCE_REFERENCES.md`](../../references/finder7/SOURCE_REFERENCES.md):
union the structure regions of visible windows above the target, intersect
with the target structure, then recalculate visibility over its full structure.
The update snapshot handling follows `UpdatedVisRgn` in `LayerMgr.c` and the
matching-window check in `WindowMgr.a`'s `EndUpdate`.

| Area | Suites |
| --- | --- |
| Build and generated files | `check-build.sh` |
| Files, drives, images and sidecars | `check-files.sh`, `check-files.ps1` |
| Drawing, bitmaps, palettes and fonts | `check-graphics.sh`, `check-graphics.ps1` |
| Sound and movies | `check-media.sh` |
| Runtime, preferences and command-line options | `check-runtime.sh`, `check-runtime.ps1` |
| Toolbox and resource management | `check-toolbox.sh`, `check-toolbox.ps1` |
| Windows, controls and input | `check-ui.sh`, `check-ui.ps1` |
| 68K runtime and hardware comparison | `syn68k/tests/check-core.sh`, `syn68k/tests/check-hardware.ps1` |
| PPC runtime and hardware comparison | `synppc/tests/check-core.sh`, `synppc/tests/check-runtime.ps1`, `synppc/tests/check-hardware.ps1` |

`./system/tests/check-ui.ps1 print-cancel -ROMAW D:/Source/ROMAW/windows-binaries/bin/ROMAW.exe`
checks explicit print-dialog cancellation and PrOpenDoc's implicit cancellation,
retry, host errors and graphics-port preservation under PPC interpreter and JIT
execution. Windows GDB intercepts every host dialog and forbids raster output;
the check submits no print jobs.

Root `make check-*` targets and `tools/run-checks.py` coordinate broader checks
and retain logs in `build/checks/`. PPC application/diagnostic runners are
described in [synppc](../../synppc/README.md#romaw-integration). Hardware checks
require their documented physical machine and explicit arguments; listing a
suite never runs them.

For Macintosh file interchange, use the [sidecar regression guide](README.sidecars),
[archive codec and Windows UI checks](../../archives/README.md), and
[StuffIt bridge checks](../../stuffit/README.md). The archive checks require
independent unar/lsar decoders and are separate from the root suite selection.
The [DSAT dialog app](dsat-dialogs/README.md) provides manual system-error UI
scenarios; [the procedure mapping](../resources/system/syserr.md) describes
the native handlers and guest fallback.

For the C-backed Finder resource procedures, `bash system/tests/check-ui.sh
finder-code` runs the portable Windows fixture. After building ROMAW, run
`bash system/tests/check-ui.sh finder-guest /absolute/path/to/ROMAW.exe` to
compile a Retro68 probe and exercise real guest calls on Windows. It checks
resource reloads, Pascal stack/result conventions, QuickDraw and trap patches,
Gestalt registration, allocation failure/retry and malformed selector tables.
Reports and trace logs stay under `build/finder-guest/`.

`bash system/tests/check-toolbox.sh native-resource /absolute/path/to/ROMAW.exe`
audits the shipped native entries and runs the INTL localization ABI, relocation,
reload and date-formatting probe on Windows. See the
[native resource inventory](../../docs/native-code-resources.md) for the complete
scope and distinctions between executable procedures and opcode-shaped data.

Validate suite dispatch independently with:

```bash
python3 -m unittest discover -s tools/tests -p test_regression_suites.py
```


For Finder 8.6 native service integration, run the grouped Windows check in both
PPC engines (the default):

```powershell
./synppc/tests/check-runtime.ps1 finder-services -ROMAW D:/Source/ROMAW/windows-binaries/bin/ROMAW.exe
```

It checks actual callback/descriptor lifetimes and control/window operations,
including unsupported optional-service returns. CFM release/termination and icon
ownership are covered by `PPC Native Libraries Probe` through
`synppc/tests/run-romaw-tests.ps1` with `-Interpreter` and `-Jit`.

`bash synppc/tests/check-core.sh thread` also checks interpreter/JIT Windows-fiber
scope lifetime after the launching CPU is destroyed. Set `SYN_PPC_SKIP_BUILD=1`
with `OBJ` pointing at an already built archive to preserve its build profile.

The private ScriptUtil Pascal ABI can be checked against the user's original
Finder without redistributing it:

```powershell
python system/tests/create-script-enable-probe.py build/script-enable "C:/Users/William/.romaw/Apps/Desktop Folder/Finder 8.6"
C:/msys64/mingw64/bin/gdb.exe -batch -x build/script-enable/probe.gdb windows-binaries/bin/ROMAW.exe
```

The probe checks argument consumption, guards, byte order, invalid input lists
and per-process script restrictions. Exercise desktop interaction, disk/folder
browsing and normal Windows window-close separately from these focused checks.
