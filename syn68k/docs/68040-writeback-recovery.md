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
a retired record. No extension is fetched again. This host-step field does not
change the native invocation packet. Call stack writes still need completion
protocols; the common validator does not infer completion for those instructions.

LINK saves its register source before decrementing SP, including the A7 alias.
PEA resolves the complete source EA before its stack update. Both are linear for
trace purposes: T1 applies, T0 alone does not. A separate resident supervisor
stack permits user-mode guest WB1/WB2 cleanup and RTE even when LINK leaves an
odd or wrapped user SP. Supervisor frame overlap remains an explicit audit item.

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
malformed offers. The combined stream matrix contains 6,440,676 cases;
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
writeback fixture has 2,785,674 cases per run; its warm run restores 1,261,837
entries with zero compilations. Native FPU helper-exclusion checks remain in
`bash syn68k/tests/check-core.sh fpu-native`.

## Remaining work

Integer stores without completed offers, nonindexed MOVEM read restart,
MOVEM/MOVE16 store continuations and
the constructed user-mode throwaway-RTE fault case also require further work.
Locked-store restrictions remain unchanged. General busy-frame, encoding,
special-value, privileged/MMU/cache and application compatibility audits are
still required; these writeback regressions do not establish complete 68040 or
paged Toolbox compatibility.
