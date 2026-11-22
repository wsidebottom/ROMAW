/*+===================================================================
  File:      mapped-cpu.h

  Summary:   Declares mapped cpu support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_MAPPED_CPU_H
#define SYN68K_MAPPED_CPU_H

#include "integer.h"

enum Syn68kMappedStop {
    SYN68K_MAPPED_BUDGET,
    SYN68K_MAPPED_EXCEPTION,
    SYN68K_MAPPED_INTERRUPT,
    SYN68K_MAPPED_UNSUPPORTED,
    SYN68K_MAPPED_BAD_REQUEST
};

typedef struct {
    /* Zero-initialize before setting pc/memory. CM state survives budget stops;
     do not redirect pc with movem_pending set without explicitly discarding it. */
    uint32_t pc;
    Syn68kMMU040Memory* memory;
    uint32_t movem_pc, movem_ea;
    unsigned movem_pending;
    /* Opt into available mapped native instruction bodies and native MMU
       transfers. Checked execution still covers unfinished instruction forms.
       AOT persistence uses SYN68K_AOT_CACHE, as with flat native execution. */
    unsigned native;
} Syn68kMappedCPU;

typedef struct {
    enum Syn68kMappedStop reason;
    unsigned completed, opcode, interrupt_level;
    Syn68kIntegerStep step;
} Syn68kMappedRun;

/* Serialized execution using the existing global CPUState and FPU state.
   Fetch each opcode once through the logical instruction bus; never use flat
   compiled blocks, exception stacks, return caches or raw guest host pointers.
   completed counts instructions that finish without a reported exception.
   Exceptions/interrupts stop before delivery. A caller owns exception frames,
   continuation/restart and hardware bus locking.
   Successful FPU/MMU instructions report T1/T0 trace using instruction-entry
   SR bits. T0 includes their pipeline-serializing forms and taken FBcc;
   FDBcc always qualifies. Forced or floating-point exceptions retain priority
   and leave trace handling to their guest handler. Other data faults defer
   tracing until restart, or carry CT when the FPU store has already retired.
   Unsupported legitimate instructions stop explicitly instead of being simulated
   as successful NOPs.
   Invalid arguments return zero; a zero budget performs no bus transfers. */
int syn68k_mapped_cpu_run(Syn68kMappedCPU* cpu, unsigned budget, Syn68kMappedRun* result);

enum Syn68kMappedDeliveryStop {
    SYN68K_DELIVERY_COMPLETE,
    SYN68K_DELIVERY_ACCESS_FAULT,
    SYN68K_DELIVERY_DOUBLE_FAULT,
    SYN68K_DELIVERY_UNSUPPORTED,
    SYN68K_DELIVERY_BAD_REQUEST
};

typedef struct {
    enum Syn68kMappedDeliveryStop reason;
    unsigned stacking;
    Syn68kMMU040Access access;
} Syn68kMappedDelivery;

/* Enter an ordinary format-0/2/3 exception through the supervisor logical
   stack and vector buses. interrupt_level is zero for synchronous exceptions,
   or 1..7 for vector 24+level. Master IRQ entry preserves both stack frames.
   Access failures retain completed writes and report the failing transfer;
   the embedding CPU must resolve delivery faults and halt on double faults
   before executing another instruction. Format 7 accepts opcode-fetch faults
   and reads certified by the stream runner to have unchanged data/CCR/SR/FPU
   results apart from FPU non-null entry, restored speculative EA updates and
   no attempted stores, or a
   retired scalar final store with one WB1/WB2 slot. The guest handler
   must complete that slot before RTE; it is never silently replayed by the CPU.
   RTE frame reads restart at the current privileged SP without repeating
   completed throwaway frames. RTE reads after a throwaway return to user mode
   remain unsupported. An FPU move-out can retire its final scalar store
   or its final two longwords, including unaligned stores, using WB1/WB2/WB3
   while retaining FPU/EA results without changing CCRs. FScc likewise retires
   its completed condition/EA state with one pending byte store. A partial ATC
   write fault retains the full operand in WB2; guest cleanup can repeat the
   completed prefix after repairing the second page. Physical FPU faults retain
   the full scalar in WB1 with ATC/MA clear, even after successful subcycles.
   Completed nonlocked integer candidates likewise permit unaligned and partial
   stores; their deferred CCR is committed once and CT uses instruction-entry
   SR, retaining callback changes in the stacked live SR. Operand reads and
   completed address updates are not repeated by cleanup.
   MOVE16 continuation/writeback formats remain unsupported.
   A pending post-instruction FPU exception carries CP and the original EA;
   RTE reports it after writeback cleanup, ahead of trace. The FPU handler
   owns the stacked trace condition and pending busy-frame recovery.
   FMOVEM can retire its final longword or its final two longwords
   after earlier register stores have completed, including partial physical
   faults. Earlier stores require the abort certificate described below.
   A pending tail retains its own data function code after callback changes.
   Control-register lists and FSAVE likewise retire their last or penultimate
   longword while retaining earlier stores and completed EA updates.
   Early control-list, FMOVEM, FSAVE and FMOVE.X faults can instead abort at
   the original PC with one WB1 (physical) or WB2 (ATC) pending. The runner
   certifies all general registers, stack banks, SR/CCR and FPU state, allowing
   the owned non-null entry transition, captured numeric FPSR/FPIAR and an
   exact auto-update EA change. It restores only that EA register. Callback
   changes to certified state, including reversal of non-null entry, deny replay.
   A partial ATC scalar must end at a page boundary and sets MA; physical
   prefixes keep ATC/MA clear. Cleanup completes the full scalar before RTE
   reexecutes the instruction, including earlier stores. FSAVE retains its
   pending frame until reexecution completes. No continuation bits are added.
   Traced aborts retain their SR with no CT; the reexecuted instruction reports
   trace after it finishes, including T0 on the serializing forms.
   Their pending tail value is sampled after the failed callback, matching the
   next normal transfer. FSAVE commits saved-frame cleanup only after validating
   the candidate; component stepping leaves it pending.
   Certified locked reads retain LK/size/FC in the access frame and restart
   only before any attempted store or committed result. Bus locking remains
   the embedding caller's responsibility.
   Final TAS/CAS writes and both CAS2 write positions can retire through WB1/WB2
   with LK retained. A CAS2 first-write fault captures its independently addressed
   second store in the next slot; guest cleanup does not repeat completed reads
   or stores. Unaligned locked stores remain unsupported.
   Indexed and PC-relative MOVEM data-read faults carry CM/EA;
   RTE resumes past address calculation for these architectural modes.
   Restart mask/extension fetch faults retain CM/EA, including split long fetches.
   Other MOVEM addressing modes and stores remain unsupported by this delivery path.
   Separate decoders without completed-store offers (including alternate stores
   and call/PEA/LINK stack writes) still require continuation work.
   Invalid requests and unsupported formats do not change CPU state or access
   the bus. */
int syn68k_mapped_exception_enter(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* exception, unsigned interrupt_level,
                                  Syn68kMappedDelivery* result);

#endif
