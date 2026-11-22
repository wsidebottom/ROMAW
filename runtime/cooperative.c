/*+===================================================================
  File:      cooperative.c

  Summary:   Implements cooperative support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: ROMlib_exchange_guest_context

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "rsys/cooperative.h"
#include "rsys/float.h"
#include "rsys/blockinterrupts.h"

/* All on-wire integers are big endian. The FPU tail is opaque, private to
   this running ROMAW, and contains no host pointers. */
enum {
    context_magic = 0,
    context_version = 4,
    context_pc = 8,
    context_sr = 12,
    context_registers = 16,
    context_usp = 80,
    context_msp = 84,
    context_isp = 88,
    context_stack_low = 92,
    context_stack_high = 96,
    context_fpu = 104
};

_Static_assert(context_fpu + sizeof(Syn68kFPUState) <= ROMAW_CONTEXT_SIZE,
               "Guest context must hold the architectural FPU state");

syn68k_addr_t ROMlib_exchange_guest_context(syn68k_addr_t pc, uint16 status)
{
    uint32 save, load;
    int template;
    uint8 restored[ROMAW_CONTEXT_SIZE];
    uint32 next = pc + 2;

    /* Called through a normal C ABI stub: return address, save, load. */
    if (!syn68k_memory_span(EM_A7, 12))
        goto invalid;
    save = READUL(EM_A7 + 4);
    load = READUL(EM_A7 + 8);
    if (!save && (load == ROMAW_CONTEXT_BEGIN_SWITCH || load == ROMAW_CONTEXT_END_SWITCH)) {
        if (load == ROMAW_CONTEXT_BEGIN_SWITCH) {
            if (ROMlib_cooperative_critical)
                goto invalid;
            if (ROMlib_cooperative_depth != emulation_depth || ROMlib_cooperative_interpreter != cpu_state.setjmp_buf) {
                EM_D0 = 1;
                return next;
            }
            ROMlib_cooperative_critical = 1u + block_virtual_ints();
        }
        else {
            if (!ROMlib_cooperative_critical)
                goto invalid;
            restore_virtual_ints(ROMlib_cooperative_critical - 1u);
            ROMlib_cooperative_critical = 0;
        }
        EM_D0 = 0;
        return next;
    }
    template = load == ROMAW_CONTEXT_TEMPLATE;
    if (template) {
        if (!save)
            goto invalid;
        load = 0;
    }
    if ((save && ((save & 1) || !syn68k_memory_span(save, ROMAW_CONTEXT_SIZE))) ||
        (load && ((load & 1) || !syn68k_memory_span(load, ROMAW_CONTEXT_SIZE))))
        goto invalid;
    if (save && load && save != load && (save < load ? load - save : save - load) < ROMAW_CONTEXT_SIZE)
        goto invalid;
    if (load && load != save) {
        if (READUL(load + context_magic) != ROMAW_CONTEXT_MAGIC ||
            READUL(load + context_version) != ROMAW_CONTEXT_VERSION)
            goto invalid;
        uint32 target = READUL(load + context_pc);
        uint32 stack = READUL(load + context_registers + 15 * 4);
        if (!target || !stack || (target & 1) || (stack & 1) || !syn68k_memory_span(target, 2) ||
            !syn68k_memory_span(stack, 4))
            goto invalid;
        uint32 low = READUL(load + context_stack_low);
        uint32 high = READUL(load + context_stack_high);
        if ((low || high) && (low >= high || !syn68k_memory_span(low, high - low) || stack < low || stack > high - 4))
            goto invalid;
        /* Validate before changing either context, and snapshot before saving. */
        memcpy(restored, SYN68K_TO_US(load), sizeof restored);
    }
    /* A nested Toolbox callback owns a live native interpreter frame. It must
     finish before another guest continuation can use that frame's exit path. */
    if (!template && !ROMlib_cooperative_depth) {
        ROMlib_cooperative_depth = emulation_depth;
        ROMlib_cooperative_interpreter = cpu_state.setjmp_buf;
    }
    else if (!template &&
             (ROMlib_cooperative_depth != emulation_depth || ROMlib_cooperative_interpreter != cpu_state.setjmp_buf)) {
        EM_D0 = 1;
        return next;
    }
    if (ROMlib_cooperative_critical)
        status = (status & ~0x700u) | (((ROMlib_cooperative_critical - 1u) & 0xeu) << 7);
    EM_D0 = 0;
    if (save) {
        WRITEUL(save + context_magic, ROMAW_CONTEXT_MAGIC);
        WRITEUL(save + context_version, ROMAW_CONTEXT_VERSION);
        WRITEUL(save + context_pc, template ? 0 : next);
        WRITEUL(save + context_sr, status);
        for (unsigned i = 0; i < 16; ++i)
            WRITEUL(save + context_registers + 4 * i, cpu_state.regs[i].ul.n);
        WRITEUL(save + context_usp, cpu_state.usp);
        WRITEUL(save + context_msp, cpu_state.msp);
        WRITEUL(save + context_isp, cpu_state.isp);
        WRITEUL(save + context_stack_low, ROMlib_cooperative_stack_low);
        WRITEUL(save + context_stack_high, ROMlib_cooperative_stack_high);
        memcpy(SYN68K_TO_US(save + context_fpu), &cpu_state.fpu, sizeof cpu_state.fpu);
    }
    if (load && load != save) {
        /* Restoring architecture must not restore host pointers, interrupt
         queues, translated-code caches, or a native interpreter jump buffer. */
        uint8* p = restored;
        next = (uint32)p[8] << 24 | (uint32)p[9] << 16 | (uint32)p[10] << 8 | p[11];
        for (unsigned i = 0; i < 16; ++i) {
            unsigned offset = context_registers + 4 * i;
            cpu_state.regs[i].ul.n =
                (uint32)p[offset] << 24 | (uint32)p[offset + 1] << 16 | (uint32)p[offset + 2] << 8 | p[offset + 3];
        }
        status = (uint16)p[14] << 8 | p[15];
        cpu_state.sr = status & 0xffe0;
        cpu_state.ccx = status & 16;
        cpu_state.ccn = status & 8;
        cpu_state.ccnz = !(status & 4);
        cpu_state.ccv = status & 2;
        cpu_state.ccc = status & 1;
        cpu_state.usp = (uint32)p[80] << 24 | (uint32)p[81] << 16 | (uint32)p[82] << 8 | p[83];
        cpu_state.msp = (uint32)p[84] << 24 | (uint32)p[85] << 16 | (uint32)p[86] << 8 | p[87];
        cpu_state.isp = (uint32)p[88] << 24 | (uint32)p[89] << 16 | (uint32)p[90] << 8 | p[91];
        ROMlib_cooperative_stack_low = (uint32)p[92] << 24 | (uint32)p[93] << 16 | (uint32)p[94] << 8 | p[95];
        ROMlib_cooperative_stack_high = (uint32)p[96] << 24 | (uint32)p[97] << 16 | (uint32)p[98] << 8 | p[99];
        memcpy(&cpu_state.fpu, p + context_fpu, sizeof cpu_state.fpu);
        memset(cpu_state.jsr_stack, 0, sizeof cpu_state.jsr_stack);
        cpu_state.jsr_stack_byte_index = 0;
    }
    if (load && load != save && ROMlib_cooperative_critical) {
        /* The incoming SR owns its interrupt priority. Release only the host
         gate, then recheck interrupts after the complete guest transaction. */
        unsigned restored_mask = (cpu_state.sr >> 7) & 0xe;
        restore_virtual_ints(((ROMlib_cooperative_critical - 1u) & 1u) | restored_mask);
        ROMlib_cooperative_critical = 0;
    }
    return next;

invalid:
    EM_D0 = (uint32)-50;
    return next;
}
