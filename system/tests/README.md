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

The live clipboard probe replaces the Windows clipboard with generated test data.
It checks transfers through the staged runtime, including QuickDraw pixel comparisons
for DIB/PICT round trips. Run it explicitly with
`system/tests/check-runtime.ps1 clipboard-live -AllowClipboardChanges`; optional
`-ROMAW` and `-Application` arguments select a build and a disposable classic app.
Other clipboard fixtures intercept Windows calls and preserve the real clipboard.

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

Validate suite dispatch independently with:

```bash
python3 -m unittest discover -s tools/tests -p test_regression_suites.py
```
