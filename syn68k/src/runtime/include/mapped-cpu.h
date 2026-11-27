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
    SYN68K_MAPPED_BAD_REQUEST,
    SYN68K_MAPPED_STOPPED,
    SYN68K_MAPPED_HALTED
};

enum Syn68kMappedHalt { SYN68K_HALT_NONE, SYN68K_HALT_DESCRIPTOR_LOCKUP, SYN68K_HALT_DOUBLE_FAULT };

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
    /* STOP retains its following PC and suppresses instruction bus cycles.
       Eligible interrupts are reported without consuming their pending bit;
       accepted exception entry clears this latch. Reset owners clear it. */
    unsigned stopped;
    /* The selected branch word or its access fault survives budget stops. */
    unsigned prefetch_valid;
    Syn68kMMU040Access prefetch_access[2];
    /* Access/address-error entry remains active through its first opcode fetch. */
    uint32_t error_handler_pc;
    unsigned error_handler_pending;
    /* Sticky until the embedding performs a CPU reset and reinitializes this
       instance. IRQs and exception entry cannot release a hardware halt. */
    enum Syn68kMappedHalt halted;
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
   Successful FPU/MMU and integer instructions report trace using instruction-entry
   SR. T0 includes supported serializing forms
   and taken branches; FDBcc always qualifies. Odd instruction references use
   format 2 before trace or interrupts, including an untaken conditional target.
   Forced/floating-point exceptions retain priority and leave trace to their
   guest handler. Data faults defer trace until restart, or carry CT when a
   scalar store has retired. An odd call target uses certified SP rollback and
   ordinary guest writeback/RTE before restarting the call; no continuation bit
   is invented for its later address error.
   Untraced STOP advances PC, loads SR/banks and reports STOPPED without further
   instruction fetches. A newly eligible pending interrupt reports INTERRUPT
   instead, retaining the stopped latch until accepted exception entry. Traced
   STOP reports trace without stopping, irrespective of the newly loaded trace
   bits. Zero budget preserves a stopped CPU and still reports BUDGET.
   Descriptor lockup reports HALTED with its access snapshot instead of an
   exception to deliver. A halted CPU reports HALTED before checking interrupts
   or fetching instructions, with no bus effects. Zero budget still reports
   BUDGET. Reset owners must reinitialize this instance and architectural state;
   clearing a cache line or changing PC alone does not release the halt.
   Unsupported legitimate instructions stop explicitly instead of being simulated
   as successful NOPs.
   Invalid arguments return zero; a zero budget performs no bus transfers. */
int syn68k_mapped_cpu_run(Syn68kMappedCPU* cpu, unsigned budget, Syn68kMappedRun* result);

enum Syn68kMappedDeliveryStop {
    SYN68K_DELIVERY_COMPLETE,
    SYN68K_DELIVERY_ACCESS_FAULT,
    SYN68K_DELIVERY_DOUBLE_FAULT,
    SYN68K_DELIVERY_UNSUPPORTED,
    SYN68K_DELIVERY_BAD_REQUEST,
    SYN68K_DELIVERY_HALTED
};

typedef struct {
    enum Syn68kMappedDeliveryStop reason;
    unsigned stacking;
    Syn68kMMU040Access access;
} Syn68kMappedDelivery;

/* Enter an ordinary format-0/2/3 exception through the supervisor logical
   stack and vector buses. interrupt_level is zero for synchronous exceptions,
   or 1..7 for vector 24+level. Master IRQ entry preserves both stack frames.
   A runner-marked completed forced exception under entry T1 enters its own
   frame/vector first, then a format-2 trace frame before its handler executes.
   The trace frame retains the forced handler PC and original instruction EA;
   guest RTE returns through both frames. Access/address, illegal, privilege,
   format and FPU numeric/preinstruction exceptions do not use this marker.
   Access failures retain completed writes and report the failing transfer;
   descriptor lockup latches the CPU halt and reports HALTED. A double fault
   retains DOUBLE_FAULT and latches the halt as well. For an odd handler PC
   during access/address-error delivery, access describes the rejected prefetch
   without a completed bus cycle. An aligned handler's first opcode is fetched
   by the stream runner before it recognizes another interrupt. An access fault
   on that fetch reports HALTED with the failing access in the run record.
   Further exception entries
   report HALTED without touching state or buses; only embedding reset releases
   either halt. Format 7 accepts opcode-fetch faults
   and reads certified by the stream runner to have unchanged data/CCR/SR/FPU
   results apart from FPU non-null entry, restored speculative EA updates and
   no attempted stores. External operand/descriptor callback changes to An or
   FPU null state deny this rollback without discarding their effects. RTE's
   explicit restart at the live privileged SP is retained separately. Also accept a
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
   All legal MOVEM data-read and data-write modes carry CM/EA. RTE resumes
   past address calculation and repeats the operand list, including completed
   transfers. Store faults additionally retain their scalar in WB1/WB2 for guest
   cleanup. Predecrement replay restores its original address sequence and the
   architecture's stored base-register value. Restart opcode/mask/extension
   fetch faults retain CM/EA, including split long fetches.
   MOVE16 physical write faults retire captured postincrements and retain the
   whole memory-aligned line in PD0-3 with WB1/TT=1/SIZE=line. Entry T1 retains
   CT until guest cleanup/RTE. Certified ATC aborts retain original PC/registers,
   WB2 and PD0-3; the guest can retry or drain the line, advance PC/postincrements
   and emulate pending trace. Callback result changes deny ATC certification
   without rollback. The operand bus remains an ordered scalar-transfer boundary.
   Certified MOVE16 read aborts retain original PC/registers, line-sized normal
   read status and the original burst address, with no pending write or push
   data. Guest repair/RTE repeats the whole source line, including successful
   partial reads, then performs destination writes and postincrements once.
   Callback changes to certified state deny restart without rollback.
   MOVES supports ordinary and alternate-space scalar writebacks, preserving
   converted TM and alternate TT=2. Alternate spaces permit physical faults,
   without translation or additional tail writes. MOVES normal/deferred trace
   also uses instruction-entry T1/T0. Separate call/PEA/LINK stack decoders
   still require continuation work.
   Invalid requests and unsupported formats do not change CPU state or access
   the bus. */
int syn68k_mapped_exception_enter(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* exception, unsigned interrupt_level,
                                  Syn68kMappedDelivery* result);

#endif
