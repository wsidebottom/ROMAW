# synppc

PowerPC 750 execution and bounded PEF loading for ROMAW. The standalone
archive builds independently of ROMAW; the optional bridge supplies CFM,
Mixed Mode and Toolbox integration. Host execution is x86-64 only: Windows x64
for ROMAW, plus native Linux x64 regression builds. The IA-32 backend has
been retired; Macintosh guest registers and addresses remain 32-bit.

- [750 architecture](docs/750-status.md): experimental `-ppc-system`, MMU, caches,
  supervisor state and integration limits.
- [Native translation](docs/translation-status.md): current x64 lowering
  and validation; [cache lifecycle](docs/translation-cache.md): preparation and persistence.
- [Flat application-core fixtures](docs/application-core.md) and
  [hardware comparison](docs/hardware-validation.md).
- [PPC CFM ownership](docs/cfm.md) and [CFM-68K](tests/CFM68K.md).
- [Carbon compatibility](../carbonlib/README.md) and
  [application investigations](../docs/README.md#application-investigations).

## Layout

- `src/`: interpreter adapter, PEF loader and guest formatted I/O.
- `src/ppc-jit.cpp`: native translation and cache backend.
- `src/ppc-fused.cpp`: exact bounded binary fused arithmetic.
- `src/ppc-arithmetic.cpp`: SoftFloat-backed ordinary arithmetic and rounding.
- `src/romaw/bridge.c`: ROMAW's CFM, Mixed Mode and Toolbox integration.
- `include/synppc/`: public execution/PEF API and optional ROMAW launch header.
- `platform/`: platform definitions used by the interpreter.
- `third_party/sheepshaver/`: adapted Kheperix sources, license and provenance.
- `tools/`: generator for ROMAW's Toolbox import table.
- `tests/`: standalone CPU/PEF tests and original Macintosh PPC integration probes.
- `obj/`: ignored objects, dependency files, generated code, tests and archives.

## Build from WSL

Run from `/mnt/d/Source/ROMAW` with GNU Make, Python 3, Perl and the MinGW x86-64
C/C++ toolchain installed:

```bash
make -C synppc -j4
make -C synppc check
make -C synppc stage
```

`bash synppc/tests/check-core.sh core --oracle` additionally requires `qemu-ppc` and
compares 7,536 defined integer cases with its 750 model. The test emits its own
Linux ELF; no PowerPC assembler/compiler or Apple assets are needed.
The core suite links instruction fixtures, runtime and integer checks, the
PEF loader, memory and cache checks, and hardware FP replay into
`obj/tests/core/ppc-core-checks.exe`. It reports each check by name. Five
arithmetic model checks share their own runner.
Run the arithmetic checks on their own with `bash synppc/tests/check-core.sh model` or
`bash synppc/tests/check-core.sh sanitized model`. Cache save/reload tests still use
separate processes to verify that saved code survives process exit.

The default `all` target creates `synppc/obj/libsynppc.a` without using ROMAW
headers or configuration. `check` builds PE32+ x86_64 test executables and runs them through
WSL's Windows interoperability. Tests optionally inspect the local MyABCD binary
when present; generated fixtures contain original test code, not Apple binaries.
`stage` copies the public headers and archive into `win64-prefix`, and the archive
into `windows-binaries/bin`. Override `CXX` (or `PPC_CXX`), `CC`, `AR`, `OBJ`,
`PREFIX` or `STAGE` on the make command line when needed.

For routine ROMAW architectural checks, run
`& synppc/tests/check-runtime.ps1 system750` in PowerShell. One original-code
`PPC 750 Batch Probe` app runs twelve named architectural and Toolbox checks in
one JIT launch. Use `-Engine Interpreter` or `-Engine Aot` to select one other
engine. Use `-Full` to run all three engines and the injected failure check.
`-Detailed` prints individual check results. `system750-isolated` retains the
individual programs for diagnosis. Prefer adding cases to reporting suites over creating a separate
executable or launching ROMAW for every assertion; keep isolated fixtures
when they establish process/startup or library-lifetime behavior that a shared
invocation cannot test.

`bash synppc/tests/check-core.sh system750` also uses one host process. It runs
seven PEF images in interpreter and JIT modes and reports the standalone CPU
cases in the same run.

Run `bash synppc/tests/check-core.sh hardware750` for the seven standalone
750 hardware suites in one process per platform. The runner reports each
suite's case count and runs the same checks on Windows and under Linux
ASan/UBSan. The previous individual check names still select one suite.

The fused/reference arithmetic checks require native `g++` with ASan/UBSan;
`NATIVE_CXX` overrides it. Optional real-application checks require local runtime assets.
`bash synppc/tests/check-core.sh sanitized` instruments the runtime, loader,
adapters and SoftFloat under Linux; optional arguments select fixture groups.

For an isolated native Linux build:

```bash
make -C synppc OBJ="$PWD/synppc/obj/linux" CXX=g++ AR=ar
SYN_PPC_SKIP_BUILD=1 CXX=g++ OBJ="$PWD/synppc/obj/linux" bash synppc/tests/check-core.sh core
```

Standalone x64 commands and object/staging defaults are in
[translation status](docs/translation-status.md#build-and-regression-commands).

## ROMAW integration

The optional `romaw-bridge` target also builds `libsynppc-romaw.a`. It requires
a configured Windows SDL ROMAW tree and its dependency prefix. ROMAW's normal
build performs this integration and stages both libraries:

```bash
make -C system
```

Its link order is `libsynppc-romaw.a`, `libsynppc.a`, then the existing syn68k,
SDL and Windows libraries. The final executable depends explicitly on all three
CPU/bridge archives so updated libraries trigger a relink. Toolbox metadata is
generated under the selected PPC object tree's `generated/` directory from
ROMAW's live trap table.

Run the staged application probes from PowerShell:

```powershell
& D:\Source\ROMAW\synppc\tests\run-romaw-tests.ps1
& D:\Source\ROMAW\synppc\tests\run-romaw-tests.ps1 -Interpreter
```

The default suite runs grouped checks in `PPC Regression Checks`. It covers
arithmetic, classic Toolbox calls, callbacks, threads, resources, sound,
components, Carbon Toolbox, popups, offscreen graphics, CoreFoundation
string search, Carbon event hooks, JIT arithmetic and code publication. The app
reports the start and completion of each check, and the runner validates its
service trace within those boundaries.
`-Full` adds process, file, sound playback and movie checks in
`PPC Full Regression Checks` to the default app. `PPC Carbon Profile Checks` runs
fallback and classic record access with its own synthetic CoreFoundationLib.
The runner prints one result per app and saves individual check results in
each app's trace. Pass `-Detailed` to print each check as well.

Use `-Exhaustive` to include the isolated startup and synthetic CoreFoundation
profiles. Its extended regression app runs the full Toolbox, media and fragment
lifecycle suites in one launch. The synthetic ownership and array checks
share one app. C and Pascal string conversion and
borrowed-pointer checks share one app per synthetic CoreFoundation version.
Use `-Group 'PPC Fragment Lifecycle Checks'` to focus on closure, private named
and disk fragments, and shared-library initialization.
Use `-Group 'PPC Execution Checks'` to focus on JIT, executable-code publication
and guest memory in one app. The memory check needs a fixed set of loaded
fragments and runs on its own in the exhaustive suite. The extended regression
app includes JIT and executable-code publication.
Use `-Group 'PPC Toolbox Checks'` to run just the classic Toolbox group.
`-Group` combines compatible group and individual check names in one app per
profile. Overlapping selections run each check once. Add `-Plan` to see the
app and emulator launch counts without generating fixtures. For
CarbonLib implementation batches, run `carbonlib/tests/check-corefoundation.ps1`
from the workspace root. One JIT launch reports the installed-library checks
by subsystem. Add `-Full` to include the synthetic fallback app in the same
execution mode. Use `-Engine Both` to run each app under both PPC engines.

`synppc/tests/run-diagnostic-tests.ps1` checks controlled failures under Windows
GDB. Use `-Startup` for four representative checks covering OSDispatch, Text
Services, fragment initialization, and a truncated PEF resource. Each expected
exit needs a separate launch. Use `-Application 'PPC Dispatch Probe' -Case
signature` to run one case, or `-All` for the complete sweep. Add `-Plan` to see the
number of launches before generating fixtures, or `-Detailed` to print each
result instead of totals by application.
Build/staging and profile-specific output paths are owned by
[the Windows build guide](../README.windows-build).

`make -C synppc clean` removes generated output. Do not overlap builds against
the same object tree or staged executable.

## Execution memory

The adapter implements aligned LWARX/STWCX word reservations with the 750's
nonspecific CPU-side reservation (MPC750UM 3.3.5.3): an intact reservation can
permit a conditional store to a different word. Portable PowerPC software must
use matching addresses, since the architecture leaves that case undefined.
Every conditional store consumes the reservation. Host services, polls and
nested executions conservatively clear it. A 6,400-case matrix covers both
memory paths, aliases, CR/SO, replacement/consumption, callback clearing and
encoding/alignment/bounds errors. Execution remains on one CPU thread; host
thread atomicity, external DMA snooping and physical cache coherency are not
implemented by this flat adapter. The architectural bus model is described in
[750 status](docs/750-status.md).

`ROMlib_ppc_environment` can provide an optional stable writable RAM span through
`linear_memory`, `linear_address` and `linear_size`. The interpreter validates the
guest extent and checks each access against the span; other addresses use the
required `memory` callback. Leave the fields zero to use callbacks everywhere.
Exclude overlays, protected pages and host-pointer aliases. Keep the backing
allocation alive and unchanged for the whole invocation. Nested invocations may
use different spans; the outer mapping is restored after normal or failed return.
ROMAW supplies only ordinary RAM above its reserved alias window.

## Floating-point environment

Each invocation applies the rounding mode from the incoming guest FPSCR and
restores the caller's complete host floating-point environment on success or
failure. Memory, polling and service callbacks run with that saved host
environment. Nested PPC calls restore the outer guest's rounding and exception
state before it resumes. The runtime tests cover all four rounding modes,
guest changes to RN, memory callbacks, nested services, polling and failed exits.
These isolation checks do not establish complete PowerPC floating-point accuracy.

## License and provenance

The portable Kheperix interpreter was adapted from the user's SheepShaver reference
checkout. It is GPL-2.0-or-later; see `COPYING` and
`third_party/sheepshaver/README.executor`. Preserve source copyright notices and
provenance. Project reorganization does not change the imported core's license.
