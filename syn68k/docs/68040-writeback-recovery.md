# 68040 physical writeback recovery

The mapped runner can retire FPU stores whose last scalar transfer faults after
successful physical bus subcycles. This includes FMOVE, FMOVEM, control-register
lists and FSAVE, with an optional captured following longword. It preserves the
original scalar operand, commits the instruction's completed register/status
work once, and leaves memory cleanup to the guest access-error handler.

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

These rules follow MC68040UM sections 7.3, 8.4.6.2, 8.4.6.4, 8.4.6.5 and
8.4.6.7, including table 8-5. The primary reference is the
[M68040 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).

## Regression coverage

`bash syn68k/tests/check-core.sh rte` includes same-page and disjoint-page
physical errors after one, two and three completed bytes, both page sizes and
privilege levels, auto-update modes, FPU list/control/frame variants, captured
tails, pending trace and enabled floating-point exceptions. Real guest cleanup
instructions and RTE verify the frame, completed bytes and final CPU/FPU state.
The focused matrix contains 139,776 cases across interpreter/native instruction
dispatch and checked/native bus bindings. The combined stream matrix contains
1,566,208 cases; control-flow/RTE coverage adds 101,224 cases. Malformed partial
writeback records cover both physical and ATC faults without changing CPU state
or accessing the bus on rejection.

`bash syn68k/tests/check-core.sh aot-x64` also runs the physical matrix and
record guards through cold and warm persistent native code. The warm run must
restore cached code without new compilations. The combined replay/physical
writeback fixture has 262,422 cases per run; its warm run restores 368,827
entries with zero compilations. Native FPU helper-exclusion checks
remain in `bash syn68k/tests/check-core.sh fpu-native`.

## Remaining work

Early unfinished FPU stores still reject physical bus errors for abort/replay;
only their certified ATC replay path is implemented. Unaligned integer-store
retirement, nonindexed MOVEM read restart, MOVEM/MOVE16 store continuations and
the constructed user-mode throwaway-RTE fault case also require further work.
Locked-store restrictions remain unchanged. General busy-frame, encoding,
special-value, privileged/MMU/cache and application compatibility audits are
still required; these writeback regressions do not establish complete 68040 or
paged Toolbox compatibility.
