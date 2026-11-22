# Syn68k

Synthetic Motorola 68K translation/runtime library used by ROMAW. The FPU
implementation is experimental; see [68040 status](docs/68040-status.md).

## Layout

- `src/runtime/`: CPU runtime, opcode descriptions and native x64 translator.
- `src/syngen/`: opcode generator and its private headers.
- `src/include/`: public header and private configuration templates.
- `tests/`: CPU fixtures, reference output and regression scripts.
- `third_party/`: separately licensed arithmetic code and attribution.
- `docs/`: status and historical documentation.
- `obj/`: ignored configuration, generated sources, helpers and compiled output.

The root Makefile is a build wrapper, matching ROMAW. Autotools inputs remain
at the root and in the source directories; configure is run only inside `obj/`.
Do not compile in `src/` or place generated headers there.

Configured headers are refreshed before generators run. Check stale-header and
incremental-refresh handling with `bash syn68k/tests/check-core.sh configured-header`.

## Windows build from WSL

From `/mnt/d/Source/ROMAW`, run `make -C syn68k`. This needs GCC with working
native Linux linking, MinGW x86_64 GCC/binutils, Make, Perl, Python 3 and the
Autoconf/Automake/Libtool tools. `CC` and `CC_FOR_BUILD` override the target and
build compilers. The x86_64 Windows target supports a threaded interpreter whose
computed-goto handlers stay inside one C function. Build with
`SYN68K_DISPATCH=threaded` to select it, and pass the same setting when running
`check-core.sh threaded-dispatch`. The default remains `switch`: a SUBQ/BNE loop
benchmark is currently faster with switch dispatch. Changing this setting
rebuilds the runtime. Windows x64 is the sole supported runtime target.

The superseded IA-32 translator, its generator, and the `NONNATIVE` /
`GENERATE_NATIVE_CODE` switches have been removed. The active x64 JIT and both
interpreter dispatch modes remain supported. Historical native annotations in
the opcode descriptions are accepted and skipped by the generator.
The public runtime ABI requires an x86-64 host and keeps 32-bit condition-code
elements. Obsolete IA-32 register dispatch, byte-sized CCR helpers, byte-swap
assembly and the hand-scheduled Pentium checksum path have been retired.

Block checksums, interrupt polling and register-array storage are built-in
invariants. The generator uses local temporary declarations and preserves
operand order; the unused alternative generation paths have been removed.

## Execution modes

The switch and threaded interpreters share the checked runtime. Native x64
execution is enabled by ROMAW's default native-code setting; `-notnative`
selects interpretation. Native blocks validate live operands, preserve fault and
interrupt boundaries, and fall back when publication or allocation fails.

Instruction and exception details belong in [68040 status](docs/68040-status.md);
[the MMU contract](docs/mmu040.md) distinguishes mapped stepping from global
ROMAW paging. [JIT/AOT](docs/aot-x64.md) describes native coverage, the opt-in
`SYN68K_AOT_CACHE`, code publication and validation. Counts there describe the
existing decoder rather than complete physical-68040 compatibility.

## Build outputs

The build is serial because generators share intermediate files. Select another
dependency prefix with `WINDOWS_PREFIX`; Linux generators must
have the same pointer width as the target. The wrappers choose this by default.
The guest FPSP image also needs m68k Linux GCC/binutils; set `M68K_TOOL_PREFIX`
to override the default `m68k-linux-gnu-` tool prefix.

The wrapper stages `libsyn68k.a` and `syngen.exe` in `windows-binaries/bin`,
installs the library/header in `win64-prefix`, and retains Linux helpers in
`obj/`. Run `make -C system` afterward to relink and stage ROMAW.
`make -C syn68k configure` prepares configuration without compiling;
`make -C syn68k clean` cleans compiled output without changing maintained files.

## Validation

Run checks from the workspace root after building. Dated results and their
limits live in [68040 status](docs/68040-status.md) and
[the AOT validation contract](docs/aot-x64.md).

Run `bash syn68k/tests/check-core.sh fpu --oracle`,
`bash syn68k/tests/check-core.sh jit-x64` (Win64),
`bash syn68k/tests/check-core.sh control-register`,
`bash syn68k/tests/check-core.sh rte` (focused flat/mapped RTE and stream checks),
`bash syn68k/tests/check-core.sh threaded-dispatch`, and
`python3 syn68k/tests/test-assembly-cleanup.py` from the workspace root.
`bash syn68k/tests/check-core.sh mmu040` checks the internal 68040 translation
engine on Linux with ASan/UBSan and in the Windows archive. See
[MMU implementation and integration limits](docs/mmu040.md); guest paging is
not enabled by this component.
The FPU oracle also requires m68k Linux binutils/GCC and qemu-m68k.
`bash syn68k/tests/check-core.sh fpsp` checks the installed Motorola package,
relocation, bounded installation, software functions and exception forwarding.
The integer harness is `syn68k/obj/tests/.libs/syngentest.exe`; compare its output with
`tests/output/` using `bash syn68k/tests/testall.sh`. A successful compile is not evidence of
full CPU or application compatibility.
`bash syn68k/tests/check-core.sh jit-x64-benchmark` optionally compares steady-state
register and indirect-RAM loops against synthetic execution on Windows. It
checks results and native execution counts, excludes initial translation, and
reports timings without a speed threshold. These small loops do not establish
application performance.
The integer script accepts optional mode names (`native`, `interpreter`,
`noncc`, `noncc-interpreter`) and runs a private executable snapshot so a
reference check does not lock the build's output against relinking.
It queries the active native backend before choosing a CRC reference. When
native translation is unavailable, the default runs the two distinct
interpreter variants rather than repeating them under native-request flags.
On Windows, a fixed-address RAM reservation can fail before guest execution.
The script allows up to five launches only for exit 1, empty test output and
that exact error-487 diagnostic. Reservation failures are retained in
`obj/tests/reference/MODE.startup.log`. CPU errors, additional diagnostics,
partial instruction output and CRC differences fail immediately. Run
`python3 syn68k/tests/reference-runner-test.py` to check these distinctions
without executing CPU instructions.

The unused standalone profiling tools and pre-Autotools platform make fragments
were removed; the live runtime profiling API remains available. License terms are in `COPYING` and the notices accompanying third-party sources.
