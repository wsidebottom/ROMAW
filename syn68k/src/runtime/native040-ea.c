/*+===================================================================
  File:      native040-ea.c

  Summary:   Instantiate a closed mapped effective-address root with invocation-local CPU state.

  Classes:   None.

  Functions: Build-time native040_ea and native040_ea_body roots.

  Origin:    Original ARDI Executor direct 68040 lowering using the maintained EA decoder.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
struct Syn68kNative040EA;
register struct Syn68kNative040EA* native_ea __asm__("r12");
#include "native040-ea.h"
#include "native040-memory.h"
#include "syn68k_private.h"
#include "blockinfo.h"

/* Headers must retain their ordinary global declarations. The maintained
   decoder below instead reads the current invocation and never a flat bus. */
#define cpu_state (*native_ea->cpu)
#define SYN68K_EA_MAPPED(context) 1
#include "ea.c"

__attribute__((used, noinline)) void native040_ea_body(void)
{
    Syn68kEAContext* context = &native_ea->context;
    native_ea->result = (Syn68kEA){.reg = -1};
    if (!native_ea->cpu || !context->memory) {
        context->error = 11;
        return;
    }
    Syn68kMMU040Memory* original = context->memory;
    Syn68kNative040MMU frame = {.memory = original, .ram = native_ea->ram};
    NativeMemory bound;
    context->memory = native040_bind_memory(&frame, &bound);
    native_ea->result =
        syn68k_effective_address(context, native_ea->code, native_ea->size, native_ea->writing, native_ea->registers);
    context->memory = original;
}

static inline uint32 integer_exception(Syn68kIntegerStep* step, const Syn68kEAContext* context, unsigned vector,
                                       uint32 pc, uint32 address, unsigned format)
{
    step->vector = vector;
    step->exception_pc = pc;
    step->fault_address = address;
    step->format = format;
    step->access = context->access;
    return pc;
}

#define write_checked_memory syn68k_ea_write_memory
#include "movem040.h"
#undef write_checked_memory
#include "integer-store040.h"
#include "moves040.h"

__attribute__((used, noinline)) void native040_integer_ea_body(void)
{
    Syn68kNative040IntegerEA* packet = (Syn68kNative040IntegerEA*)native_ea;
    Syn68kEAContext* context = &native_ea->context;
    Syn68kMMU040Memory* original = context->memory;
    Syn68kNative040MMU frame = {.memory = original, .ram = native_ea->ram};
    NativeMemory bound;
    context->memory = native040_bind_memory(&frame, &bound);
    context->pc = packet->pc + 2;
    if (packet->resuming)
        context->resume_ea = &packet->resume_address;
    packet->step.next = moves040_opcode(packet->opcode)
                            ? move_alternate(packet->pc, packet->opcode, context, &packet->step)
                            : move_multiple(packet->pc, packet->opcode, context, &packet->step);
    if (packet->resuming && packet->step.vector == 2) {
        packet->step.movem_state = 1;
        packet->step.movem_ea = packet->resume_address;
    }
    context->resume_ea = 0;
    context->memory = original;
}

#if defined(_WIN32)
#define ROOT_ENTER "push %r12\nmov %rcx,%r12\nsub $32,%rsp\n"
#define ROOT_LEAVE "add $32,%rsp\npop %r12\nret\n"
#else
#define ROOT_ENTER "push %r12\nmov %rdi,%r12\n"
#define ROOT_LEAVE "pop %r12\nret\n"
#endif
__asm__(".text\n.p2align 4\n.globl native040_ea\nnative040_ea:\n"
        "endbr64\n" ROOT_ENTER "call native040_ea_body\n" ROOT_LEAVE);
__asm__(".text\n.p2align 4\n.globl native040_integer_ea\nnative040_integer_ea:\n"
        "endbr64\n" ROOT_ENTER "call native040_integer_ea_body\n" ROOT_LEAVE);
