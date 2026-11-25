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
   1 candidate, 2 physical write retired, 3 retired with CT, 4 ATC abort/retry. */
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
       Capture an odd speculative branch target even when the branch is untaken;
       no instruction prefetch bus cycle is performed at that address. */
    uint32_t prefetch_address;
    unsigned flow_changed;
    /* STOP requests suspension only after successful, untraced retirement. */
    unsigned stopped;
    Syn68kMOVE16Writeback move16;
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
int syn68k_integer_resume_movem(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, uint32_t ea,
                                Syn68kIntegerStep* result);

/* Dispatcher-private operand kernels; opcode has already been fetched. The
   caller initializes result and owns mapped coherence and exception delivery. */
int syn68k_integer_operand_step(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result);

#endif
