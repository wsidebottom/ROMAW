# CFM-68K execution

ROMAW's shared fragment bridge also executes `m68k` PEF containers through
syn68k. Architecture selection is explicit: a PowerPC connection cannot load an
m68k image or pass its code to the PPC JIT/AOT translator.

CFM-68K queries report the emulated 68040 architecture. Classic 68K callers
can detect the built-in CFM before opening a connection using Gestalt `cfrg`.
The `mixd` selector reports the CFM-68K calling convention bit, without claiming
the separate Mixed Mode dispatch-trap or state-saving extensions.

Supported paths include flat application/library fragments, standard MPW
segmented applications (`rseg` 0/1 and version `$FFFD` `CODE` segments), and
classic `CODE` applications loading CFM-68K plug-ins. Flat libraries use the
existing dependency graph, imports/exports, shared connections and reverse
initialization-order termination. Public GetSharedLibrary, GetDiskFragment,
GetMemFragment, FindSymbol and CloseConnection calls share that implementation.
CountSymbols and GetIndSymbol enumerate real PEF exports, including re-exports;
enumeration of the virtual native Toolbox libraries returns unimpErr.

CFM-68K fixed scalar arguments use right-to-left four-byte stack slots, callee
cleanup and D0 results. External entries receive A1 pointing at the transition
vector's data-area word, allowing their standard `MOVE.L (A1),A5` prolog.
Native Toolbox imports adapt these to the same checked
scalar services used by the PPC bridge. Routine descriptors distinguish the
CFM-68K runtime architecture (`ISA | 0x10`) from classic 68K code. Mixed Mode
callbacks translate classic Pascal/C/register arguments to CFM-68K slots;
CallUniversalProc retains caller cleanup for its variadic arguments.

Segmented applications retain their code resources while running. Preparation
patches each segment's published jump-table and twelve-byte transition-vector
ranges, checking those ranges against allocated PEF sections. This avoids a
dependency on a boot-installed segment-loader extension. ExitToShell and classic
application replacement release the appropriate connection closure.

The portable loader still needs no ROMAW headers. Only its optional bridge
depends on ROMAW. This is runtime support, not a promise that every imported
Mac OS service exists: native floating-point/aggregate signatures, private
fragment copies, custom segmented formats and unsupported Toolbox imports need
separate implementations. No runtime-enabler extension is installed.

Run `synppc/tests/check-runtime.ps1 cfm68k` against the staged executable. Its original
machine-code fixtures cover flat/segmented startup, direct ExitToShell,
initializers/terminators, A5 worlds, scalar Toolbox calls, exported functions and
data, public connection operations, actual AAFE descriptor entry, variadic
CallUniversalProc, and a classic application using a CFM-68K plug-in. Run the
existing `check-core.sh core` and `run-romaw-tests.ps1` for PPC regressions.
Add `-TextCommon` to exercise a locally installed m68k Text Encoding Converter:
its real initializer, encoding routines and terminator run through the bridge.
That optional test includes only an original client, without Apple library bytes.

The layouts and conventions follow Apple's
[Mac OS Runtime Architectures, chapter 5](https://leopard-adc.pepas.com/documentation/mac/runtimehtml/RTArch-65.html),
[chapter 9](https://dev.os9.ca/techpubs/mac/runtimehtml/RTArch-102.html), and
[TN1077](https://leopard-adc.pepas.com/technotes/tn/tn1077.html).
