/*+===================================================================
  File:      jit-x64.h

  Summary:   Declares jit x64 support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef SYN68K_JIT_X64_H
#define SYN68K_JIT_X64_H
#include "syn68k_private.h"
#include "fpu.h"
#include "integer.h"
/* Checked data operations also serve interpreter clients with a data bus. */
int syn68k_checked_memory_opcode(unsigned opcode);
uint32 syn68k_checked_memory_run(uint32 pc, unsigned opcode);
#ifdef GENERATE_X64_CODE
extern int syn68k_x64_enabled;
int syn68k_x64_memory_opcode(unsigned opcode);
/* Return the bounded native instruction length, or zero for a checked tail. */
int syn68k_x64_fast_memory_opcode(unsigned opcode, uint32 pc);
/* Structural eligibility, independent of the current indexed extension. */
unsigned syn68k_x64_native_memory_words(unsigned opcode);
/* Bounded, non-trapping EA calculation. Never invokes guest callbacks. */
int syn68k_x64_indexed_address(unsigned code, uint32 extension_pc, uint32 result[2]);

typedef struct {
    uint32 source, destination, next_pc;
    void* host_destination;
} Syn68kX64Pair;

/* Prepare complex operands without traps or guest memory writes. Commit address
   updates only when the emitted body can finish without callbacks/faults. */
int syn68k_x64_prepare_pair(unsigned opcode, uint32 pc, Syn68kX64Pair* result);
uint32 syn68k_x64_memory_run(uint32 pc, unsigned opcode);
/* Ahead-compiled complex-integer kernels. Called only after a native tail;
   their checked writes may invalidate and release the originating block. */
int syn68k_x64_complex_opcode(unsigned opcode);
uint32 syn68k_x64_complex_run(uint32 pc, unsigned opcode);
#define SYN68K_X64_PAGE_SHIFT 12
#define SYN68K_X64_PAGE_COUNT (1U << (32 - SYN68K_X64_PAGE_SHIFT))
extern uint32 syn68k_x64_code_pages[SYN68K_X64_PAGE_COUNT];
extern uint32 syn68k_direct_memory_size;
void syn68k_x64_flag_liveness(const uint16* source, const int8* lengths, unsigned instructions, const uint8* faults,
                              uint8* masks);
void* syn68k_x64_compile(const uint16* source, const int8* lengths, unsigned instructions, unsigned* translated,
                         uint32 pc, int* terminal);
void syn68k_x64_release(void* code);
/* Execute a decoded mapped FPU instruction. Opcode consumption is
   the caller's responsibility; command words remain live mapped transfers.
   Zero means no CPU/bus effects and permits the checked interpreter path;
   negative means an invalid native outcome that must not be replayed. */
int syn68k_x64_mapped_fpu(uint32 pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kFPUStep* result);
/* Decoded flat counterpart; normal semantics execute through closed native bindings. */
int syn68k_x64_flat_fpu(uint32 pc, unsigned opcode, Syn68kFPUStep* result);
int syn68k_x64_mapped_mmu(uint32 pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result);
/* Bounded process cache; retirement is safe even from an active callback. */
void syn68k_x64_mapped_clear(void);
extern uint64 syn68k_x64_executions;

/* Immutable metadata shares the RX allocation; mutable retirement state lives
   only in active managed C frames. Keep the machine-code entry 16-byte aligned. */
typedef struct {
    uint32 managed, reserved[3];
} Syn68kX64CodeHeader;

extern void* syn68k_x64_managed_context;
uint32 syn68k_x64_execute_managed(void* code);

/* Keep native dispatch in the interpreter's existing Win64 call frame.
   An out-of-line O0 wrapper otherwise adds another call for every block. */
static __inline__ __attribute__((always_inline)) uint32 syn68k_x64_execute(void* code)
{
    ++syn68k_x64_executions;
    const Syn68kX64CodeHeader* header = (const void*)((const unsigned char*)code - sizeof(Syn68kX64CodeHeader));
    if (header->managed || syn68k_x64_managed_context)
        return syn68k_x64_execute_managed(code);
    return ((uint32(*)(CPUState*))code)(&cpu_state);
}
#endif
#endif
