# 68040 physical writeback recovery

The mapped runner can retire FPU stores whose last scalar transfer faults after
successful physical bus subcycles. This includes FMOVE, FMOVEM, control-register
lists and FSAVE, with an optional captured following longword. It preserves the
original scalar operand, commits the instruction's completed register/status
work once, and leaves memory cleanup to the guest access-error handler.

Early stores instead use certified abort/replay. The runner checks all general
registers, stack banks, SR/CCR and the complete FPU state. Only instruction-owned
non-null entry, captured numeric FPSR/FPIAR and an exact auto-update EA change
are permitted. It restores only that address register, retains successful memory
writes and numeric status, and keeps the original PC. A callback reversing
non-null entry or changing other certified state denies replay without undoing
its effects. Guest cleanup completes one WB1/WB2 scalar; RTE then reexecutes the
whole instruction and repeats earlier writes. No continuation bit is introduced.

## MOVE16 line cleanup

Forced-exception tracing is separate from access-error cleanup: the mapped
runner delivers the completed forced exception first, then its pending T1 trace
before entering the forced handler. Access faults do not acquire this latch.

Physical MOVE16 write faults retire normal address updates once and retain next
PC, SSW.TT=1/SIZE=line and memory-aligned PD0-3. WB1S is invalid (zero),
as required by UM section 8.4.6.3. Guest cleanup writes the whole
line without rereading source data. Entry T1 retains CT. Certified ATC aborts
instead retain original PC/registers and WB2/PD0-3; software chooses retry or
captured-line cleanup plus PC/register advance and trace emulation. Malformed
records are rejected before CPU or bus effects. See the current status
checkpoint for validation and exact-source/binary audit details. MOVE16 read
faults now use a separate certified abort: original PC/registers, line-sized read
status and original source burst address, with no pending writeback. Guest
repair/RTE repeats the complete source read. Both halves now use a nonallocating
physical line bus with a latched MMU translation. Physical beat callbacks stay
checked; normal cache/MMU/RAM work executes inside the emitted native image.
Malformed line diagnostics (cycle, progress, physical address or captured data)
are rejected before frame writes. The documented same-register postincrement
write-protection erratum invalidates a dirty cached source without writing it.

## CAS/CAS2 termination

The mapped 68040 always ends a completed CAS comparison with a write cycle.
A mismatch writes the value just read; CAS2 mismatch writes only its second
read operand. Shared compare-register failures retain memory operand 1 in that
register in both the mapped and generated legacy decoders. Faults in these terminating writes retire the compare-register
results and captured flags, retaining the old value in WB1/WB2 for guest MOVES
cleanup. They do not repeat either operand read. A successful CAS2 first write
retains LOCK without LOCKE and captures its second update if it faults. The
final write and mismatch termination assert LOCKE only on their final physical
beat. These certificates remain restricted to aligned locked stores. Normal
unaligned transfers are tested separately from fault continuation admission.

## MOVEM store continuation

MOVEM store faults retain CM and their original EA, plus the full failed scalar
in WB1/WB2. Guest cleanup completes that captured write; RTE repeats the whole
list from the saved EA, including earlier stores. Predecrement restores its
original address sequence and the stored base-An value. Live handler changes
to other source registers remain visible on replay. The original PC remains
pending until the instruction completes; T1 trace follows successful replay.

Resumed instruction-fetch faults retain CM/EA but no pending write. Store
records require ordinary data FC, word/long size, consistent fault/progress
addresses and valid physical/ATC metadata. Scalar retirement, FPU continuation,
locked or tail-write metadata cannot be combined with CM. Invalid records leave
CPU state and bus counters unchanged. See the current status checkpoint for
focused/grouped coverage. An additional 35,712-case matrix covers partial ATC
faults and A0 through A7, including active user/supervisor stacks, with real
guest WB2 cleanup and RTE. Odd supervisor SP/double-fault cases remain open.

## MOVEM read continuation checkpoint

MOVEM load operand faults offer CM and the original EA for all legal read
addressing modes. Replay repeats the operand list from that EA, including
postincrement, while retaining the architectural effects already visible at the
fault. Further instruction-fetch faults retain the same continuation. Mask,
extension or indirect-pointer faults before operand execution can instead earn
ordinary restart only with unchanged general registers and the full state and
callback guards. They carry no CM/saved-EA continuation.

The 2026-11-25 grouped MMU suite passes 7,600,300 stream/recovery cases and
101,224 control-flow/RTE cases. Complete cold/warm AOT passes 3,365,486 recovery
cases per run, with zero warm compilations. The matching ROMAW bundle was
rebuilt, smoke checked and promoted to `build/68040-current/bin`. See the current
checkpoint in [68040-status.md](68040-status.md) for source/binary audit details.

## Read restart certification

Generic read restart restores speculative An updates only when operand and
descriptor callbacks preserve An and FPU null state, alongside the existing
checkpoint comparisons for registers, flags, stack banks and FPU state. External
null-state changes must not be mistaken for normal FPU non-null entry. Callback
changes deny rollback and remain live; they are never overwritten by a saved An
snapshot. RTE instead retains its explicit restart certificate at the current
privileged SP, preserving completed frame progress and callback effects.

## Completed integer stores

Scalar decoders that finish all nonmemory work can offer their final byte, word
or longword for retirement. Nonlocked offers permit unaligned addresses and
successful physical or first-page ATC prefixes. The runner commits deferred CCR
and following PC once, retaining all completed address-register updates. CT
eligibility uses the instruction-entry SR; callback changes to live trace bits
remain in the saved SR without creating or cancelling that trace condition.
Guest cleanup writes the retained scalar without rereading source or destination
operands. Locked transfer restrictions remain unchanged.

The producers are `finish_scalar` in `src/runtime/jit-memory.c` and
`final_store_exception` for memory destinations of MOVE SR/CCR, MOVES, PEA and
LINK in `trap.c`. These stores leave flags unchanged, so their offer preserves
live CCR, including callback changes, while the retained write data remains the
value captured before the callback. LINK also captures its decoded displacement:
the component fault leaves An/SP unfinished, then the mapped runner uses live SP
to finish the register assignments and clears the displacement before publishing
a retired record. No extension is fetched again. This LINK host-step field does
not change the native invocation packet. BSR/JSR producers additionally capture
the already decoded target; their completion is explicit, not inferred by the
common validator.

LINK saves its register source before decrementing SP, including the A7 alias.
PEA resolves the complete source EA before its stack update. Both are linear for
trace purposes: T1 applies, T0 alone does not. A separate resident supervisor
stack permits user-mode guest WB1/WB2 cleanup and RTE even when LINK leaves an
odd or wrapped user SP. Supervisor frame overlap remains an explicit audit item.

## Calls and pending odd references

Even-target BSR/JSR final writes retire at the target PC, including a target
that equals fallthrough. These calls always qualify for T0; CT eligibility uses
entry SR, while the saved live flags retain callback effects.

For an odd target, a data fault is handled first. The runner certifies unchanged
checkpointed state apart from SP minus four, then restores SP and publishes the
existing canonical scalar abort/replay record. Guest WB1/WB2 cleanup and RTE
return to the original call. Reexecution performs its push once at the same
address and reports the address error before trace. Prefix memory writes and
descriptor history are retained. Callback mutations deny rollback without
undoing their live state. A completed call is not retired to an odd PC, and no
private continuation bit or undefined effective-address field is used.

Instruction-local integer/FPU metadata separately retains speculative odd
branch references, including conditional fallthrough. Once instruction work
completes, dispatch creates a format-2 address error with source PC and an
A0-cleared reference. Forced and floating-point exceptions remain ahead of that
error; trace and interrupts follow it. FPU metadata is emitted directly from the
live decoded displacement in mapped execution. The untraced flat fast path
leaves before counter/condition effects when an odd reference requires its
checked exception path; only side-effect-free RAM could have been consumed.

## MOVES spaces and trace

MC68040UM table 3-2 converts SFC/DFC2/6 to operand FC1/5. Spaces 0/3/4/7
bypass translation and use TT=2 with their original TM. The SSW helper encodes
both fields and rejects contradictory alternate-space ATC faults. Retired
nonlocked alternate stores use one physical WB1, with no tail or pending FPU
exception. Locked transfers retain their ordinary-data-space restrictions.

MOVES is serializing for T0, including straight-line execution. Normal trace and
CT after retired stores use instruction-entry T1/T0, preserving callback changes
in the stacked live SR/CCR. Failed reads have no CT; guest repair and RTE restart
the original instruction, which traces only after successful execution.

The MOVES group contains 1,026,328 cases across both dispatch modes and both
bus bindings: 281,088 normal cases, 744,960 handler/writeback/read-restart cases
and 280 encoding/privilege/record guards. It covers all eight function codes,
nine alterable EA forms, Dn/An sources including EA-register aliases, all
sizes/lanes and aligned partial prefixes, both page sizes, translated
protection/missing pages and alternate physical faults. Guest cleanup extracts
TM from the actual SSW, sets/restores DFC, drains WB1/WB2 and executes RTE.
Captured data, CCR, next PC, address updates and indirect-pointer read counts
are checked, including live trace/CCR changes after operand capture. Normal
cases also verify load sign extension and preserved Dn upper bytes. Additional
active-stack EA cases remain part of the wider exception/addressing audit.

## RTE after a user-mode throwaway prefix

Format-1 processing commits its popped frame, SR and selected stack before
reading the next frame (MC68040UM section 8.4.2). A later user-data frame-read
fault now retains the live USP and receives an ordinary format-7 access frame.
Its exception entry selects ISP or MSP from the committed M bit. Operand/ATC
failures and dirty-victim or descriptor-history cache-push failures use the
same checked or emitted memory path. The recovery marker certifies frame
construction; it does not bypass the privilege check when RTE is executed
again in user mode.

The dedicated 7,232-case fixture covers both page sizes, every CCR value,
ISP/MSP exception entry, each initial frame-header field, partially read longs,
ATC and physical failures, and checked/native CPU and memory combinations.
Guest code repairs missing descriptors and flushes translation, or drains PD
writebacks with physical MOVES, before returning. Tests preserve the original
user frame and verify the subsequent RTE takes privilege vector 8 without
reading that frame. The existing stream fixture also exercises the previously
unsupported user-prefix cases through exception entry and guest RTE.

## Page-crossing replay

The shared checked/emitted operand path repeats the first-page portion of an
unaligned data word or longword before transferring its second-page portion. This
also applies to serialized mappings, transparent translation, disabled
translation and address wrap. The initial page extent is latched per operand;
callback changes cannot create an unbounded replay loop. The requirement for
two first-page accesses comes from MC68040UM section 4.1. Instruction-space
requests retain their logical stream behavior; they do not use data replay.
Physical aligned half-line instruction-prefetch sequencing remains a separate
hardware-modeling boundary (MC68040UM sections 7.3 and 7.4.2).

Successful reads assemble the replayed first-page data and the second-page data;
a changing device callback cannot accidentally publish the discarded initial
sample. A fault leaves the caller's read destination unchanged. `completed`
describes logical progress in the current pass, rather than cumulative bus
traffic. Writes from the initial pass remain externally visible even if replay
faults before completing the same bytes again. Format-7 records still retain
the original scalar request and full writeback value. The manual does not
specify every failed-replay subcycle ordering; these tests establish the
functional model's ordering, without new silicon measurements.

The dedicated matrix checks 5,060 cases with literal bus sequences, changing
read samples, faults at every transfer in either pass, second-page invalid and
protected descriptors, both page sizes, privilege levels and RMW intent. Raw
operand, naturally aligned bus, disabled-cache and enabled serialized-cache
bindings are covered. The standalone Windows comparison runs 10,120 checked and
emitted cases. The twenty-group Windows/Linux kernel runner executes the
native matrix with guards against host semantic helpers. The existing operand
suite adds first-pass/replay fault coverage and now contains 148,065 cases.

## Partial cached-read diagnostics

Physical cache reads now retain the successful operand prefix in `value` on
an operand, fill or victim-push failure. The MMU result combines that prefix
with its earlier completed chunks. Only bytes counted by `completed` belong
to the diagnostic value; incomplete internal fill bytes and poisoned failed
callback buffers are excluded. On a page-crossing replay, the diagnostic
prefix belongs to the current pass. The public read destination still commits
only when the whole operand succeeds. Write diagnostics retain the original
width-limited operand.

The independent 2,342-case matrix uses literal natural-beat widths, every
line offset and physical size, cache-hit prefixes followed by physical or
fill faults, and dirty-victim push failures. Both cache and logical-memory
interfaces are checked, including user/supervisor data and instruction
spaces where applicable. Four baseline checked/native runs on Windows and
Linux reject the missing prefix. The twenty-group emitted-kernel runner
executes the matrix with host semantic helpers forbidden; the reference
cache/MMU batch runs checked Windows and Linux ASan/UBSan cases. The existing
5,060-case page-crossing matrix also checks the precise diagnostic prefix at
failures in both passes.

## Transfer and frame contract

`Syn68kMMU040Memory.aligned_transfers = 1` selects naturally aligned uncached
byte/word/longword transfers. An odd longword uses byte/word/byte transfers;
a longword at lane two uses word/word. The callback must complete a transfer or
fail without publishing any of that transfer. Earlier successful bytes remain
visible. The default operand boundary still leaves decomposition to the embedding
cache/device implementation. This does not model cache line fills or timing.

The API access result distinguishes the original scalar `request` from the failed
subtransfer's `logical` address and counts completed bytes. Format 7 retains the
original scalar in FA and WB1A, the original SIZE, and its entire value in WB1D.
WB1 data follows memory lanes and can wrap around lane three. Software cleanup
unrotates and writes the full scalar, which repeats any successful prefix. An
optional following longword occupies WB2 and remains register aligned. Physical
errors clear ATC and MA even when earlier subcycles succeeded. Translation faults
continue to use WB2 and set MA for an already completed first-page prefix.

The runner independently validates the FPU's retirement offer. Contradictory
read/store records and bus-error/translation-fault combinations are rejected.
Pending floating-point exceptions retain CP and their operand EA; otherwise an
instruction-entry trace condition retains CT and the original instruction PC.
The guest handler completes writebacks before RTE dispatches the pending
exception or returns to the following instruction. Captured tail data survives
callback changes to live FPU registers, controls or saved frames.

These rules follow MC68040UM sections 7.3, 8.2.1, 8.4.6.2, 8.4.6.4, 8.4.6.5 and
8.4.6.7, including table 8-5. The primary reference is the
[M68040 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).

## Regression coverage

`bash syn68k/tests/check-core.sh rte` includes same-page and disjoint-page
physical errors after one, two and three completed bytes, both page sizes and
privilege levels, auto-update modes, FPU list/control/frame variants, captured
tails, pending trace and enabled floating-point exceptions. Real guest cleanup
instructions and RTE verify the frame, completed bytes and final CPU/FPU state.
The final-store matrix contains 139,776 cases across interpreter/native
instruction dispatch and checked/native bus bindings. Physical early-store
coverage adds 1,479,936 cases, including zero-byte faults at every lane and
one/two/three-byte prefixes. Numeric sources include finite, quiet-NaN and
masked signaling-NaN values; enabled signaling NaNs suppress stores before a
fault can occur. A focused 1,052-case guard group checks callback effects and
malformed offers. The combined stream matrix contains 8,107,168 cases;
control-flow/RTE coverage adds 101,224 cases. Malformed partial
writeback records cover both physical and ATC faults without changing CPU state
or accessing the bus on rejection. Within the total, a focused 2,508,032-case
scalar matrix covers all lanes, 37 operation forms, all CCRs, both page sizes and
privileges, physical faults and protected/missing pages. RMW missing-page faults
are injected after complete operand reads. The fixtures verify retained prefixes,
full WB1/WB2 cleanup, address updates, no repeated operand reads, and entry/live
SR trace combinations. Status-store physical callbacks additionally change
all five live CCR bits; cleanup preserves those changes without changing the
captured SR/CCR operand. Another 31,648 cases cover read-restart callback
certification and live-SP RTE recovery, as detailed in the MMU integration notes.

`bash syn68k/tests/check-core.sh aot-x64` also runs the physical matrix and
record guards through cold and warm persistent native code. The warm run must
restore cached code without new compilations. The combined replay/physical
writeback fixture has 3,618,920 cases per run; its warm run restores 1,262,191
entries with zero compilations. Native FPU helper-exclusion checks remain in
`bash syn68k/tests/check-core.sh fpu-native`.

The mapped unaligned locked fixture adds 36,097 cases across checked execution,
native checked memory and a native RAM window that excludes operand storage.
CAS.W/L and CAS2.W/L exercise every unaligned line offset, natural physical
subcycle failures in both initial and replayed first-page passes, disjoint
second-page protection/missing faults and 32-bit address wrap. Guest handlers drain WB1/WB2 plus an independent CAS2 tail,
restore DFC and registers, and return through deferred trace. Locked operand
reads remain counted after the fault, and any attempted cleanup reread fails.
The matrix includes 22 malformed-record checks with unchanged CPU/MMU/cache/RAM
state and no bus activity. The grouped MMU and cold/warm AOT runners include
this fixture. These are writeback recovery tests; they do not establish all
normal page-crossing bus behavior.

## Remaining work

Further active-stack/double-fault variants and compound descriptor-push
continuations after consumed MOVE16 source reads remain open. Ordinary MOVE16
line abort, PD cleanup, source-push recovery and retry now use the physical line
bus.
Compound locked cache-push faults after a partial operand remain outside the
certified continuation contract. General busy-frame, encoding,
special-value, privileged/MMU/cache and application compatibility audits are
still required; these writeback regressions do not establish complete 68040 or
paged Toolbox compatibility.
