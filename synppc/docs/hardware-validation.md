# PPC hardware comparison and benchmarks

Hardware captures validate only the machine and defined fields recorded by the
probe. 603 and G5 measurements do not establish 750 behavior where the processors
differ. Captured-vector provenance is in [tests/data](../tests/data/README.md).
Commands in this document that start with `tests/` are relative to `synppc/`;
workspace-root commands explicitly include `synppc/`.

## Capture and replay

`tests/mac-fpu-reference.py OUTPUT_DIR` emits a standalone original PPC Mach-O
probe and `cases.json`. Copy `ppc-fpu-probe` to a PowerPC Mac, make it executable
and capture its output through `od -An -v -tx1` into a UTF-8 hex file. Decode it
with `mac-fpu-reference.py OUTPUT_DIR --decode-hex CAPTURE.hex`, then run
`bash synppc/tests/check-core.sh hardware-fpu OUTPUT_DIR/hardware.txt` in WSL.
The probe uses Darwin system calls directly and needs no compiler, SDK or Apple
runtime binaries. Generated executables and captures belong under `build/`.

For a classic PowerPC Mac, use
`python synppc/tests/mac-fpu-reference.py build/ppc-classic-fpu --classic`.
Transfer `PPC FPU Hardware Probe` with its resource fork and the precreated
`PPC FPU Results` file into the same folder on the Mac. Launch the application
there, then copy the results file back without text conversion. Decode with
`python synppc/tests/mac-fpu-reference.py build/ppc-classic-fpu --decode-binary "PATH/TO/PPC FPU Results"`
and use the same hardware comparator. This is a classic CFM application using
InterfaceLib file calls, not a Darwin executable; a compiler, terminal and SSH
are unnecessary on the test Mac. The generated `.AppleDouble` directory holds
the application resource fork; preserve it through an appropriate AFP transfer
or another fork-preserving method rather than copying the bare data fork.

The classic wrapper uses the same 2,464-case arithmetic body as the Mach-O
probe, preserves the caller's FPSCR and locates its output beside the application
through Process Manager metadata. It verifies file-call results and the write
count, and replaces only its dedicated precreated results file. Windows ROMAW
JIT/interpreter captures matched, and the capture decoder/comparator passed.
The user's Macintosh 5260/100 running Mac OS 8.5 returned all 2,464 cases over
the AFP share. All 848 applicable IEEE value/FPSCR cases matched synppc.
The same exclusions apply: NI behavior, implementation-dependent estimates and
invalid single representations are not established by this comparison.
ROMAW-generated captures remain transport checks, not independent evidence.

The same 5260/100 on System 7.5.5 returned the full FPU capture again: 848
applicable cases matched, with the same 1,616 exclusions. Its original integer
probe is generated with
`python synppc/tests/mac-integer-reference.py build/ppc-integer-hardware`.
Transfer `PPC Integer Hardware Probe`, its resource fork and the dedicated
`PPC Integer Results` file together, launch it, then fetch that results file.
Decode with `mac-integer-reference.py OUTPUT_DIR --decode CAPTURE.bin` and run
`bash synppc/tests/check-core.sh hardware-integer OUTPUT_DIR/integer-hardware.txt`.
All 24,576 captured multiply/divide cases matched the independent widened
integer model and both runtime modes. The comparator excludes 1,632 undefined
division result/CR0 values, while still checking defined overflow, sticky SO,
carry preservation and the other CR fields. Cases cover operand/destination
aliasing, record forms and all combinations of SO/OV/CA. These measurements
describe the 603e; they do not establish privileged or full 750 compatibility.

Use the integer generator's `--alu` option for the separate `PPC ALU Hardware
Probe` and `PPC ALU Results` files. The 5260 on System 7.5.5 returned all 18,432
add/subtract/negate cases, including OE/Rc variants and operand aliases. All
values, XER and CR fields match the model and interpreter/JIT on Windows
IA-32/x64 and sanitized Linux, without undefined-result exclusions. The IA-32
results are historical; current host builds require x86-64.

The `--carry` generator option emits the separate `PPC Carry Hardware Probe`
and `PPC Carry Results` files. All 27,648 cases captured on the same 5260 match
the widened model and both runtime modes, with no exclusions. This covers all
eight register carry operations with either OE setting, all SO/OV/CA inputs,
record forms and destination aliases. Preserve the generated resource fork
and dedicated output file as with the other probes.

The user's iMac G5 on OS X 10.4 returned all 2,464 cases; 848 applicable IEEE
arithmetic values and FPSCR results matched on Windows x86/x64 and Linux.
The comparator excludes NI, implementation-dependent estimates and single
arithmetic inputs without valid single representations. G5 NI on/off pairs
showed no change beyond the NI bit for this sample, so this hardware result
does not validate the 750 manual's denormal-flushing behavior. The remaining NI
implementation gap is unchanged; do not treat G5 measurements as a 750 oracle.

For a native Linux audit, run `bash synppc/tests/check-core.sh sanitized`.
It instruments the whole runtime, adapters, loader and SoftFloat with ASan/UBSan
in `obj/sanitized`. Its default run shares one executable across the 22
instruction checks and also exercises runtime/JIT, hardware comparisons,
integer ALU and PEF fixtures. Optional arguments select individual fixtures,
for example `runtime alu`.
`CXX`, `AR`, `OBJ` and `JOBS` can override native build settings. This found a
zero-count rotate shifting by 32 in the imported interpreter; both rotate
helpers now mask each C++ shift count. Existing instruction fixtures exercise
all masks and zero-count rotations against a bit-at-a-time model.

The base opcode map is built once and copied into each CPU before its
host-service instruction is added. Nested invocations retain independent tables.
This avoids rebuilding invariant decoding metadata for every PPC call; it does
not cache guest instruction bytes or generate native code.

After `tests/check-core.sh core`, the optional
`obj/tests/core/ppc-core-checks.exe runtime --benchmark`
checks 5,000 two-instruction invocations. `--benchmark-loop` checks one invocation
containing 8,000,001 instructions. Both validate results and instruction counts;
timings are diagnostic and are not pass/fail thresholds. On the Windows checkout,
decoder reuse reduced the first benchmark from 649-653 ms to 56-57 ms; the loop
remained approximately 27-30 ms. These are synthetic measurements, not a claim
that entire applications run eleven times faster.

The combined raw FPR/FSEL JIT fixture checks 15,360 source/destination, record/non-record,
live-state, rewrite, CR1 branch, poll and exact-limit cases on Windows x86/x64
and sanitized Linux, plus 160 reserved-FRA cold/hot cases. It checks native
execution counters and preserves signaling NaN payloads and host fenv. An
original `tests/mac-fpr-reference.py` probe matched 8,192 cases on the Macintosh
5260/100 with System 7.5.5, including all source/destination FPR pairs. The
probe preserves ABI nonvolatile FPRs before OS calls. Another original
`tests/mac-fsel-reference.py` probe matched 28,672 FSEL cases on the same Mac,
covering all guard/destination register pairs, signed zeros, denormals,
infinities, quiet/signaling NaNs (including low-word-only NaN payloads), aliases,
record forms and NI settings. Selected SNaN payloads remain unchanged. The host
fixture also mixes these operations with native CR1 branches and live FPSCR
changes. This adds moves/signs/selection to native blocks; FP arithmetic uses the same software arithmetic through compiled semantic tails;
this does not replace exact software rounding/status with host FP operations.

## FPSCR register captures

MFFS also compiles as a raw integer FPSCR-to-FPR copy. It reads live status on
each native block entry, preserves FPSCR and records CR1 when requested.
The upper word matches the interpreter's zero policy; UISA leaves it undefined.
`tests/check-hardware.ps1 classic-fpr -Fpscr` runs an original 2,048-case application
covering every destination FPR, Rc and FPSCR basis/complement patterns. Its
defined low result word, CR and complete FPSCR matched the 5260/100 on 2026-10-29.
Binary capture SHA-256:
`1afc86a7112fe688c15afcf6e1a5d3458aba2ffbd8abcc2f73c1e82a7878220b`.
The expanded native FPR fixture checks 15,648 valid cases and 280 cold/hot
reserved forms, including live callback status, MFFS-to-FMR/FNEG chains,
branches, source rewrites, polling and exact instruction limits.

MCRFS compiles natively, copying the original FPSCR field to CR before clearing
its exception causes and recomputing VX/FEX. FX is preserved unless its field
is explicitly cleared. Native blocks retain updated status for following MFFS
and record forms, then commit it before interpreted code or callbacks resume.
Rounding and enable controls are unchanged. An original 4,096-case probe covers
all source/destination fields and FPSCR basis/complement patterns; CR and FPSCR
matched the 5260/100 on 2026-10-29. Run
`tests/check-hardware.ps1 classic-mcrfs` with optional `-Interpreter` for the same
application in Windows ROMAW. The original binary capture SHA-256 is
`55b5b2f92e9a0372fbedc930bcb13cff67e19e5d7b8c899470904e5ea08a3a40`.
Each case repeats sixteen times to warm a native MTCRF/MCRFS/MFCR/MFFS block.
The Windows runner requires a native-execution trace in JIT mode and its
absence in interpreter mode; this checks the application path as well as the
standalone fixture.

### Comparison/conversion NI hardware audit (2026-10-30)

The original `tests/mac-ni-conversion-reference.py` probe ran 1,920 cases on
both the physical 603-family 5260/100 (System 7.5.5) and the G5 (OS X 10.4.11
Classic). All defined results matched between the Macs and staged Windows
ROMAW, with PPC JIT enabled and disabled. Each capture includes FCMPU,
FCMPO, FCTIW and FCTIWZ, both NI settings, all rounding modes, signed zeros,
denormals, minimum normal, finite values, range overflow, infinities and
signaling/quiet NaNs. Conversion record forms also check CR1.

The 960 IEEE rows independently match the rational/NaN reference models.
All 960 NI rows retain ordinary input behavior on these machines; 176 rows
would differ if denormal inputs were replaced by signed zero. Conversion
FPSCR[FPRF], MFFS's high word and the conversion's high result word are
architecturally undefined and excluded. In particular, the G5 clears FPRF
where the 603 preserves it. This audit does not change the runtime's 750 NI
policy or establish 750 behavior, enabled exceptions or MSR trap delivery.

Generate an application with `python tests/mac-ni-conversion-reference.py DIR`,
launch it on a Mac and copy `PPC NI Compare Convert Results` back as binary.
Decode with `--decode CAPTURE`; add `--reference OTHER_CAPTURE` to compare
all defined fields, including NI-on results. On Windows, run
`tests/check-hardware.ps1 mac-ni-conversion -Reference CAPTURE` to check both PPC
dispatch settings against that capture. Without `-Reference`, IEEE rows are
asserted and NI results are reported as measurements rather than a 750 oracle.

Original binary SHA-256 values (undefined fields can differ):

- 603: `2eeb8ebc61fa4e31b9187fadac979f4ec24f25391ef773a940097c853afef7b3`
- G5: `16f94b0269a05409d45731d99cada470985a3a39506eba7c98fb1dd0fedab63d`

Validation: both physical captures, both staged dispatch runs against the 603
capture, decoder mutation checks and both repository formatter checks passed.
Mutation checks accept changed undefined fields but reject changed defined
conversion results in IEEE rows and NI rows compared against a capture.
This batch changes probes and documentation only; the staged runtime is unchanged.
