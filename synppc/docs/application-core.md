# Flat application-core validation

These implementation notes describe the flat application adapter and its focused
instruction fixtures. Supervisor/MMU integration is tracked separately in
[750 architectural status](750-status.md), and current native lowering in
[translation status](translation-status.md). Statements about missing physical bus
or MSR delivery below refer to the flat adapter, not the architectural backend.
Commands beginning with `tests/` are relative to `synppc/`; commands beginning
with `synppc/` are relative to the workspace root.

## Floating arithmetic and instruction fixtures

The CPU suite also checks 41,168 binary32/binary64 add, subtract, multiply,
divide, square-root and round-to-single cases against a Fraction/integer-root
oracle. All four rounding modes, NaN preference, signed zeros and IEEE flags
are covered in the arithmetic kernel; sampled cases also run through instruction
dispatch. Ordinary arithmetic and FRSP report signaling NaNs, operation-specific
invalid causes and division by zero, including sticky FX/VX/FEX, FR/FI clearing,
VE/ZE write inhibition and record-form CR1. A dispatch matrix covers all rounding
modes, aliases, enables and unused NaN operands. Ordinary add/subtract/multiply/divide/sqrt and FRSP now maintain OX/UX/XX,
FR/FI and FPRF, including before-rounding tininess and scaled OE/UE results.
A separately rounded extended-exponent path retains the target precision before
scaling, without host floating arithmetic or double rounding. An independent
Fraction oracle checks 187,460 value/status cases, sticky transitions and all
rounding/enables, with sampled instructions and destination aliases. The new
path restores SoftFloat precision as well as its rounding/flags. MSR trap
delivery remains unfinished.
Fused multiply-add/subtract forms also report VXSNAN, VXIMZ and VXISI from raw
source registers before NaN quieting. Product invalid causes are retained when
the addend is a NaN, including simultaneous VXSNAN/VXIMZ. VE inhibits destination
and FPRF writes; valid fused operations classify even with VE enabled. A separate
instruction matrix covers every fused form, precision, rounding direction,
register alias, enable and record bit. Fused forms now also update OX/UX/XX,
FR/FI and FPRF from the exact accumulator, including enabled scaled range results
and before-rounding tininess. A separate 312,800-case Fraction oracle checks all
forms and precisions with aliases, record bits, sticky transitions and enables.
The CPU path uses raw register bits and does not change host floating flags.
Ordinary arithmetic also preserves full NaN payloads, including single forms;
FRSP alone clears the low 29 bits, as specified by PEM 3.3.1.
The legacy bits-only test wrapper retains its host-fenv interface. MSR trap
delivery remains unfinished.
FPSCR control instructions now update VX/FEX through the software adapter even
with host-fenv exceptions disabled. MTFSFI places each immediate in the selected
field and can explicitly set/clear FX; MTFSF honors all field masks. MTFSB1 sets
FX only for a new sticky exception cause, and MCRFS copies the original field
before clearing causes and recomputing summaries. Reserved instruction fields
are rejected before state changes; reserved FPSCR bit 20 is not writable. A
15,442-case instruction fixture covers every field/mask/bit, record forms,
sticky transitions, source payload preservation and host-rounding restoration.
The x86-64 JIT and AOT inline MTFSB0, MTFSB1, MTFSFI and MTFSF. Writes to
the rounding field end a flat block and apply the guest mode before a
following floating-point store. Other FPSCR writes stay visible to later
instructions in the block. The existing FPSCR fixture checks the native
results, reserved forms and single-subnormal stores in all four modes.
FRSQRTE now uses software binary64 square root and reciprocal operations instead
of narrowing through a host float. Its implementation-dependent estimate stays
within the UISA's 1/32 relative-error bound throughout the double exponent range.
Raw NaNs, VXSQRT/ZX/VXSNAN, VE/ZE inhibition, FPRF and CR1 are handled without
host floating exceptions; FR/FI, which are undefined for estimates, are cleared.
A 7,293-case fixture checks special/status/alias behavior and all finite
exponents against an independent host square-root accuracy bound. This does not
match a particular processor's estimate table. FRES now also uses software
arithmetic, with an extended exponent range before a single binary32 conversion.
OE/UE produce scaled finite values, OX/UX and ZX/VXSNAN are sticky, and estimates
do not raise XX. The combined fixture runs 141,553 cases, including reciprocal
powers across every binary64 exponent, all rounding/sign/OE/UE combinations,
repeated causes and tininess before rounding. The 750's non-IEEE arithmetic mode
remains under audit. FRES now flushes tiny NI results to signed zero with UX
when UE is clear, preserving XX and retaining enabled scaled results. The
full-exponent fixture checks normal-input NI cases, repeated causes, sticky XX
and a tiny result that IEEE rounding raises to minimum-normal. An original
2,496-case Mac capture checks exact powers, signs, RN/NI/OE/UE, aliases and
record forms. Hardware FR/FI are undefined and the 603's additional range-path
XX is excluded; independent instruction tests require unchanged XX. No
processor-specific estimate table or hardware-validated NI denormal-input
compatibility is claimed by that capture.
The 750's NI arithmetic operand policy now follows MPC750UM table 2-20:
single/double denormal operands become signed zero before arithmetic and
invalid/zero-divide detection. FRSP and estimates retain binary64 source
semantics. A 4,544-case original fixture checks known exact results, infinity
times flushed zero, division by flushed zero, signed-zero square roots,
enabled-invalid/zero-divide write inhibition, sticky causes, aliases, record
forms and host-fenv preservation on Windows x86/x64 and sanitized Linux. The
user's 603 differs for denormal inputs, so those cases use the 750 specification
and explicit expected values rather than the 603 hardware captures. NI effects
on comparisons, conversions and other instructions still need separate audits.
With UE disabled, ordinary/fused arithmetic and FRSP now flush tiny NI results
to signed zero with UX/XX/FI, cleared FR and the zero FPRF. This includes tiny
values that IEEE rounding would raise to minimum-normal. The original
`tests/mac-ni-reference.py` probe captured 816 value/FPSCR pairs on a Macintosh
5260/100 running System 7.5.5. The normal-input subset avoids the 603/750
denormal-input difference; replay checks aliases, record bits and both dispatch
settings. Another 816 captured normal-input cases check UE-enabled scaled
results and UX/FEX in both NI settings. Capture provenance is in
`tests/data/README.md`. NI effects on other instructions and MSR-controlled
trap delivery remain open.
The architectural references are [MPCFPE chapter 3](https://www.nxp.com/docs/en/user-guide/MPCFPE.pdf)
and the [PowerPC 750 manual](https://www.nxp.com/docs/en/reference-manual/MPC750UM.pdf).
The integer-conversion reference also checks FCTIW/FCTIWZ using exact rational
rounding, including post-round range checks and PowerPC NaN results. The
x86-64 JIT and AOT lower both instructions using integer shifts and comparisons.
The same checks cover conversion status, enabled-invalid write inhibition, CR1,
aliases and reserved operands. The architectural 750 matrix also checks FP
availability and enabled exceptions in each MSR FE mode.
FCMPU/FCMPO have 4,000 independent comparison/NaN cases, including CR fields,
FPCC, signaling-NaN status, ordered-comparison precedence and sticky summaries.
The x86-64 JIT and AOT compare the raw binary64 encodings and update the
selected CR field and FPSCR in the native block. The same fixture covers inline
execution, including an FPSCR read and conditional branch after a comparison.
An original 864-case physical Mac FCTIW/FCTIWZ capture additionally checks
normal-input NI off/on, signed range boundaries, NaNs, rounding and preserved
FPRF. Replay compares the defined low result word and complete FPSCR on
Windows x86/x64 and sanitized Linux, with aliases, record forms and both
dispatch settings. `tests/check-hardware.ps1 classic-conversion` runs the same probe
through Windows ROMAW and compares it with the physical capture. The high
result word, NI denormal inputs and trap delivery are outside this comparison.

STFIWX transfers the raw low FPR word to bounded guest memory, without changing
FPSCR, CR or registers. The 48,448-case fixture checks every source FPR, RA=0,
address-register aliases, wrap, callback/direct RAM paths, boundary/write faults
and reserved Rc rejection. Warm native prefixes also reach the interpreted
store and reject a later reserved-word rewrite. The conversion replay now
checks FCTIW/FCTIWZ followed by STFIWX. This is the flat-memory model; physical
bus cycles, MMU and exact alignment exception delivery remain unfinished.

DCBZ rejects reserved operand/Rc bits before address calculation or writes.
Its 21,238-case fixture checks all 32 byte offsets within the 750's 32-byte
block, RA=0, register aliases, wrapped addition, callback/direct RAM, complete
block bounds and rejected writes. Reserved words are also checked after native
prefixes warm. No partial block bytes change when a mapped span is rejected.
This validates flat-memory effects, without modeling cache allocation,
coherence, cache-inhibited pages or delayed physical bus exceptions.

`tests/check-hardware.ps1 classic-compare` runs an original 20,992-case FCMPU/FCMPO
application and verifies CR and FPSCR against rational ordering and the
architectural NaN rules. The same probe matched the physical 5260/100 on
2026-10-29, including all eight CR fields, signed zeros, IEEE denormals,
infinities, both NaN signs/classes, sticky causes with FX clear and VE-dependent
ordered-comparison precedence. NI denormal inputs and MSR trap delivery are
excluded. The probe saves and restores the caller's nonvolatile CR fields.
The binary capture SHA-256 is
`67806897264f39c93f861c614682b418480708c7d75f5322d47481d706d1c3f1`.

## Descriptor and reserved-encoding regressions

### Empty AppleEvent descriptor integration

The classic descriptor path accepts zero-length descriptors with null data
handles in both list items and record keys. The PPC bridge exposes AECreateList,
AEPutKeyDesc, AEPutDesc, AESizeOfKeyDesc and AESizeOfNthItem through the existing
classic implementations. The AppleEvent probe inserts null and typed-empty
descriptors, checks their types/sizes and counts, and disposes the aggregates.
This covers the MATM 1.5 null-descriptor crash; it does not establish complete
AppleEvent object-model support.

### Reserved memory instruction encodings

X-form integer, floating and string loads/stores reject a set reserved Rc bit
before data access or register updates. D-form displacement low bits remain
valid. The 3,900-case fixture checks every source/destination register with
callback and linear memory, with PPC JIT requested and disabled. It also checks
rewriting a valid instruction behind a warmed native prefix. Rejection follows
the runtime's controlled unsupported-instruction policy; physical 750 behavior
for invalid forms and program-exception delivery are not claimed. See
[MPC750UM sections 2.3.4.3 and Appendix B](https://www.nxp.com/docs/en/reference-manual/MPC750UM.pdf).

### Multiple-load base overlap

LMW rejects a base-register field in its destination range before memory access
or register changes, including RA=RT=0. Legal absolute addressing with RA=0
and RT>0 remains supported. STMW permits source/base overlaps and stores the
original base value. The 8,192-case fixture checks every register pair in both
directions, callback/linear memory, signed displacements and both PPC dispatch
settings; 2,112 invalid overlap cases must preserve registers and memory.
This follows the runtime's controlled invalid-instruction policy rather than
claiming physical 750 behavior for invalid forms. See
[PowerPC Programming Environments Manual section 4.2.3.5](https://www.nxp.com/docs/en/user-guide/MPCFPE_AD_R1.pdf).

### String-load register ranges

LSWI and LSWX reject address-register fields inside the destination range before
loading data, including ranges wrapping through register zero. LSWI's zero
count means 32 bytes. LSWX's count is read from XER on every execution; zero
length still rejects RT aliases with RA/RB, while an accepted zero-length load
preserves the undefined destination value. Store-string behavior is unchanged.
The 1,048,576-case fixture covers every immediate count, all register-field
combinations at indexed count boundaries, callback/linear memory and both PPC
dispatch settings. Two warmed-prefix tests change XER from a valid to an
invalid range and verify rejection before further data access. Invalid forms
follow the runtime's controlled diagnostic policy, not physical trap delivery.
See the [PowerPC Programming Environments Manual, LSWI/LSWX and Appendix B.13-B.15](https://www.nxp.com/docs/en/user-guide/MPCFPE.pdf).
