/*+===================================================================
  File:      integer.h

  Summary:   Declares integer support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_INTEGER_H
#define SYN68K_INTEGER_H

#include "mmu040-memory.h"

/* MOVE16 keeps memory-addressed push-data slots. This host-side offer does
   not change a native invocation packet. The mapped runner finalizes state:
   1 write candidate, 2 physical write retired, 3 retired with CT, 4 ATC write
   abort/retry, 5 read candidate, 6 certified read abort/retry, 7 cached-source
   descriptor-push candidate, 8 certified descriptor-push abort/retry.
   In state 8 PD belongs to the pushed descriptor line; WB2 marks the pending
   MOVE16 and must not be drained. Read offers carry no push data; successful
   partial reads are repeated after guest repair/RTE. */
typedef struct {
    uint32_t data[4], address, next, trace_pc;
    uint32_t source, destination;
    unsigned command, state;
} Syn68kMOVE16Writeback;

typedef struct {
    uint32_t next, exception_pc, fault_address;
    unsigned vector, format;
    Syn68kMMU040Access access;
    /* Set by the mapped runner after verifying unchanged data/CCR/status/FPU
     results apart from FPU non-null entry and restoring speculative EA updates,
     with no attempted stores. The RTE decoder also offers a mapped frame-read
     restart at the current privileged SP, retaining completed throwaway frames
     and callback effects. Other component stepping leaves it zero. */
    unsigned restartable_read;
    /* Physical bus error while RTE restores a validated format-7 continuation. */
    unsigned rte_internal_fault;
    /* Unretired push cleanup: 1 runner-certified result/EA rollback;
       2 RTE restarts at its live privileged SP, retaining completed frames
       and callback changes. Guest PD0-3 cleanup precedes reexecution. */
    unsigned cache_restart;
    /* A scalar decoder can offer its completed flags/next PC without changing
     component-step fault semantics. Only the stream runner retires it (2).
     0: absent, 1: candidate, 2: retired store, 3: retired store/trace pending,
     4: certified scalar store abort/replay with WB1/WB2 pending. FPU numeric
     output FPSR/FPIAR can be retained while source state is unchanged; an odd
     BSR/JSR target instead restarts after certified rollback of its SP update. */
    uint32_t store_next, store_trace_pc;
    unsigned store_ccr, store_state;
    /* 1: MOVEM read/write continuation, including restart fetch faults;
       2: successful RTE restored CM/EA. */
    uint32_t movem_ea;
    unsigned movem_state;
    /* Retired FPU store with CP pending; trace is left to its FPU handler. */
    uint32_t fpu_ea;
    unsigned fpu_vector;
    /* Optional pending write: 1 is the following FPU longword at request + 4;
     2 is CAS2's captured second operand at store_tail_address. */
    uint32_t store_tail;
    unsigned store_tail_valid;
    unsigned store_tail_fc; /* The following word can use a different data bank. */
    unsigned store_locked;  /* A captured TAS/CAS/CAS2 write retains LK. */
    uint32_t store_tail_address;
    /* Candidate-only LINK completion, captured from the decoded extension.
       The mapped runner applies its An/SP updates using the live SP after the
       callback, then clears this field before publishing a retired record.
       This host-step tail is not part of a native invocation packet. */
    uint32_t store_link_displacement;
    /* Instruction-local branch metadata, not an exception-frame encoding.
       Capture a conditional branch target even when untaken. Odd targets report
       an address error without performing an instruction bus cycle. */
    uint32_t prefetch_address;
    unsigned flow_changed;
    /* STOP requests suspension only after successful, untraced retirement. */
    unsigned stopped;
    unsigned interrupt_level; /* Interruptible cache instruction remains at its original PC. */
    Syn68kMOVE16Writeback move16;
    /* Runner-owned T1 latch for a completed forced exception. Delivery enters
       that exception first, then trace, before its handler executes. */
    unsigned forced_trace;
    uint32_t forced_trace_pc;
} Syn68kIntegerStep;

/* Internal MULU/MULS/DIVU/DIVS, MOVEM/MOVES/MOVEP, JSR/JMP/LEA/PEA and
   register, scalar memory, control flow/status and complex integer boundary.
   Explicit mapped contexts additionally connect the MMU control instructions;
   the flat backend still rejects MMU enables.
   A nonzero return identifies a handled instruction or a mapped fetch fault. Other
   opcodes return zero. PC must be word aligned. NULL memory selects flat
   accesses; a mapped caller owns exception delivery, cache coherence and restart
   handling. MOVEM retains completed transfers before a later fault. Access
   metadata describes the failing scalar, not the entire register list.
   No exception stack is constructed. State remains serialized like the CPU. */
int syn68k_integer_step(uint32_t pc, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result);
/* Same dispatch after an opcode fetched by the mapped stream romaw. */
int syn68k_integer_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result);
static inline int movem040_opcode(unsigned opcode)
{
    unsigned mode = opcode >> 3 & 7, reg = opcode & 7, load = !!(opcode & 0x400);
    return (opcode & 0xfb80) == 0x4880 && mode >= 2 && (load ? mode != 4 : mode != 3) &&
           (mode != 7 || reg <= (load ? 3u : 1u));
}

int syn68k_integer_resume_movem(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, uint32_t ea,
                                Syn68kIntegerStep* result);

/* Dispatcher-private operand kernels; opcode has already been fetched. The
   caller initializes result and owns mapped coherence and exception delivery. */
int syn68k_integer_operand_step(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result);

#endif
