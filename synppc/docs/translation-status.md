# PPC native translation

The Windows and Linux x64 backends prepare every instruction recognized by the
current application decoder, apart from embedding-service dispatch boundaries.
Both also prepare architectural 750 singleton records. This is translation
coverage of the existing runtime, not a claim of complete physical 750 or
Macintosh compatibility.

Both backends directly lower the supported PPC 750 instruction set. They emit
integer, branch, CR, raw FPR, FPSCR, floating comparison/conversion and guarded
flat-memory operations. Ordinary finite floating arithmetic uses SSE2. Exact
fused multiply/add uses closed integer instruction templates copied into the
native block; the template build rejects calls and relocations. FRES uses an
extended-precision intermediate and FRSQRTE uses deterministic SSE2 operations.
NaNs, enabled invalid exceptions and arithmetic range exceptions retain checked
exits. SSE2 is part of the required x86-64 host architecture.

Architectural instruction fetch now invokes a shared emitted native memory
kernel, including normal MMU walks, history updates and L1/L2 fills/writebacks.
The kernel compiles the maintained model sources, resolves internal references
at build time and rejects live runtime imports and writable global state.
Publication relocates its verified internal pointers before making pages RX.
Checked device callbacks catch embedding exceptions before native frames return;
exception propagation then resumes in compiled C++ code. Interpretation and
native publication/allocation failures retain the original checked path.

Architectural integer/FPR loads and stores, multiple-register and string
transfers, reservations, cache/order and external-control instructions now call
the same emitted kernel from generated guest bodies. Normal RAM access, MMU
walks, reference/change updates and L1/L2 fills/writebacks execute inside that
kernel. Destination/update registers commit only after successful transfers;
streams retain their precise partial-fault behavior. Each packet checks live
DABR state, including changes made by an earlier callback. STWCX consumes the
reservation before access and commits CR0 from live callback-updated CR/XER.

Checked physical callbacks run in the host floating environment. Host mutations
persist between beats of one packet, and the next packet starts from the
embedding's original environment. Guest FP state, including MXCSR, is restored
before native execution resumes. Callback exceptions are caught and rethrown
after the native frame has returned.

MTMSR, RFI and side-effect MTSPR writes now call emitted state operations from
the generated guest body. These cover decrementer/time-base, PMU, IABR, HID0,
L2CR, ICTC and thermal controls, including defined HID1/PVR no-op writes. ISYNC
acknowledges pending instruction-breakpoint state inside the generated body.
Checked errors remain outside native frames; allocation/publication failure
keeps the original interpreter/state path available. The emitted call preserves
the live CPU/invocation bases and each host ABI's stack alignment.

Normal supported 750 instructions no longer require instruction-semantic or
original C++ bus helpers on either supported host ABI. Checked helpers remain
for CPU faults, exceptional floating paths, device/embedding callbacks and native
allocation/publication failure. Unsupported instruction encodings remain
illegal; this does not add instructions absent from the 750.

Generated code uses live CPU and private invocation bases. No CPU, scratch,
guest-RAM or helper address from compilation is embedded in persisted code.
Each backend preserves its x64 ABI's nonvolatile registers and stack alignment;
exceptions and nested callbacks unwind through compiled C++ frames. Each block
validates live source words, honors
poll and instruction-limit boundaries, and commits its prefix before callbacks.
Privilege, exception and trace dispatch retain their existing checked boundaries;
architectural fetch and translation preflight use the emitted memory kernel.

JIT and AOT share the bounded cache, replacement policy and code emitter.
`EXPAOT04` records identify the build and host ABI; Windows and Linux x64
artifacts are distinct, and retired IA-32 ABI tags remain invalid. Loading
validates all metadata, source words and freshly re-emitted
machine code before executable allocation. Publication transitions writable
pages to read/execute, rolls back failures, and saves through an atomic sibling
replacement. Cache failures retain checked execution. Cold preparation and
fresh-process replay cover both flat and architectural records.

Detailed preparation, eviction and disk persistence rules are in
[the cache lifecycle](translation-cache.md).

## Build and regression commands

Run from the workspace root in WSL:

```bash
bash synppc/tests/check-core.sh translation-all
make -C synppc check
bash synppc/tests/check-core.sh sanitized kernel-jit
bash synppc/tests/check-core.sh native-memory750
```

Individual instruction groups can be rerun in that same core executable, for
example `bash synppc/tests/check-core.sh core reservation fpscr estimate`.
`native-memory750` routes the existing seven grouped hardware fixtures through
the published native kernel, with checked exception adapters and Linux
ASan/UBSan boundaries. It covers Windows x64 and native Linux x64.

`translation-all` builds one reporting translation executable per host ABI.
The Windows and Linux x64 executables group state/memory/fault/limit matrices,
decoder emission,
cache pressure, rewrite/reentry, corrupt artifacts and publication failures.
The same executable performs save/reload in separate processes, then each ABI
rejects the other's cache. The broader core suite groups instruction fixtures
in its existing reporting executable. Both ABIs check oversized native bodies,
malformed files and publication rollback. The shared scalar memory runner covers
306,254 endian, FPR, alias, bound, limit and self-modification cases per ABI.

Standalone x64 builds use `synppc/obj`, `win64-prefix` and
`windows-binaries/bin`. Stage with `make -C synppc stage`. ROMAW's supported
host remains Windows x64 and is rebuilt and staged through `make -C system`.
The Makefile and public execution header reject IA-32 hosts and compilers.

ROMAW integration uses `synppc/tests/check-runtime.ps1 aot` for linked-layout
cold/warm cache reuse, `check-runtime.ps1 system750` for the reporting 750 batch,
and `run-romaw-tests.ps1` with `-Jit` and `-Aot` for the grouped application
and CFM probes. Passing these checks establishes the tested paths, not general
application performance or full Carbon compatibility.

## Historical validation checkpoint

The following records the earlier four-target checkpoint, including the IA-32
backend that has since been removed. Current build and regression commands above
exercise only x64 hosts; these historical results do not imply current IA-32 support.

The November 20, 2026 direct-lowering checkpoint is implemented through
`a68181e1`, following the incremental integer, floating, flat-memory, emitted
MMU/cache/fetch and state-control commits. Regression validation covers:

- 9,155 architectural CPU cases on Windows x64/IA-32 and Linux x64/IA-32
  ASan/UBSan. Link-time guards reject original C++ fetch, data, reservation,
  cache/external and state setters in pretranslated matrices. Live DABR changes
  preserve successful prefixes and three-byte load-tail atomicity. Three packet
  tests verify per-beat host FP mutations, packet reset, full MXCSR restoration
  and callback exceptions. Raw emitted STWCX checks live callback CR/XER changes.
- Both Windows translation runners pass native state/fault/limit and decoder
  coverage, 3,200 shared memory cases, 76,800 floating cases, corrupt-cache
  rejection, publication rollback, fresh-process flat/750 replay and foreign-ABI
  rejection. Saved code executes without runtime compilation.
- Both Windows widths pass 306,254 scalar memory cases, 32,768 multiple
  transfers, 1,048,576 string-range cases, 3,200 eager string cases, 7,680
  reservation cases and 21,238 DCBZ cases. Each also passes 43 host-cache
  callback/nesting/poll/failure cases.
- The emitted MMU/cache kernel passes all seven hardware groups on Windows
  x64/IA-32 and Linux x64/IA-32 with sanitized boundaries. These include 561 bus
  cases, 83,999 clock cases and 3,117 MMU cases. The callback-abort regression
  checks full state and MMU search-cycle PMU parity against actual C++ unwinding;
  the pre-fix behavior fails the same check.
- Earlier independent arithmetic validation includes 187,460 rounding/status
  cases, 312,800 fused FPSCR cases, 8,624 rational conversions, 4,000 comparisons
  and 141,553 reciprocal-estimate vectors. Exceptional paths retain helpers.

ROMAW was rebuilt in the isolated `build/ppc-direct/system` directory and
published after PE validation. The fixed snapshot is
`windows-binaries/bin/romaw-ppc-direct.exe`, SHA256
`e0b795c09ca281ac9a6d6a935619f23ea862ae5555ffefdd98d0146a532bb366`.
The same executable was staged as `windows-binaries/bin/romaw.exe`; other
workspace builds may subsequently update that normal filename. Both PPC archives
and the ROMAW bridge archive were staged alongside it, with the IA-32 PPC
archive under `windows-binaries/bin/x86`.

That snapshot passes the twelve-check architectural batch under interpreter,
JIT and AOT, including injected-failure rejection; all 26 Core application
checks under JIT and AOT; and cold/warm disk-cache reuse at two relocated linked
layouts with zero runtime compilation. These are targeted regression results,
not a claim of complete Macintosh/Carbon compatibility or application performance.

Repository checks passed for 1,715 file headers, 1,614 formatted source files
and the PowerShell sources (19 headers). Build logs, generated code and staged
binaries remain ignored local artifacts.
