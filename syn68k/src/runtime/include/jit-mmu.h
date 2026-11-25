/*+===================================================================
  File:      jit-mmu.h

  Summary:   Emit mapped PTEST, PFLUSH, MOVEC and STOP bodies using closed native roots.

  Classes:   None.

  Functions: Private opcode eligibility and emitter definitions included by jit-x64.c.

  Origin:    Original ARDI Executor direct 68040 MMU instruction lowering.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#define MMU_BYTES(out, ...)                                    \
    do {                                                       \
        const unsigned char data[] = {__VA_ARGS__};            \
        memcpy((out)->bytes + (out)->size, data, sizeof data); \
        (out)->size += sizeof data;                            \
    } while (0)

static int mapped_mmu_opcode(unsigned opcode)
{
    return opcode <= 65535 && ((opcode & 0xfff0) == 0xf500 || opcode == 0xf510 || opcode == 0xf518 ||
                               (opcode & 0xffd8) == 0xf548 || (opcode & 0xfffe) == 0x4e7a || opcode == 0x4e72);
}

static int mapped_mmu(emitter* out, unsigned opcode, uint32 pc)
{
    if (!mapped_mmu_opcode(opcode))
        return 0;
    if (out->size + 1024 > sizeof out->bytes)
        return -1;
    unsigned failures[8], count = 0;
    out->managed = out->dynamic = 1;
    ++out->guards;
    fpu_cpu(out, 0x0fb7, 0, offsetof(CPUState, sr));
    byte(out, 0xa9);
    word32(out, SR_SUPERVISOR_BIT);
    unsigned privilege = jump(out, 0x84);
    unsigned illegal = 0, skipped = 0;
    int movec = (opcode & 0xfffe) == 0x4e7a;
    int stop = opcode == 0x4e72;
    if (movec || stop) {
        mapped_fetch_command(out, pc, failures, &count);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, control.command), 0);
        fpu_call_packet(out,
                        stop         ? NATIVE040_STOP
                        : opcode & 1 ? NATIVE040_MOVEC_WRITE
                                     : NATIVE040_MOVEC_READ,
                        offsetof(FPUInvocation, control), failures, &count);
        fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, control.ok), 0);
        byte(out, 0);
        illegal = jump(out, 0x84);
    }
    else {
        pointer_r8(out, RELOC_FPU_FRAME);
        fpu_cpu(out, 0x8b, 0, offsetof(CPUState, dfc));
        MMU_BYTES(out, 0x83, 0xe0, 7);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.fc), 0);
        MMU_BYTES(out, 0x89, 0xc2, 0xc1, 0xea, 2);
        fpu_packet(out, 0x89, 2, offsetof(FPUInvocation, memory.supervisor), 0);
        fpu_cpu(out, 0x8b, 2, reg_offset(8 + (opcode & 7)));
        fpu_packet(out, 0x89, 2, offsetof(FPUInvocation, memory.address), 0);
        if ((opcode & 0xffd8) == 0xf548) {
            fpu_constant(out, offsetof(FPUInvocation, memory.writing), !(opcode & 0x20), 0);
            fpu_call_packet(out, NATIVE040_PTEST, offsetof(FPUInvocation, memory), failures, &count);
            fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_ACCESS, 0);
            fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.cache_exception), 0);
            byte(out, 0);
            failures[count++] = jump(out, 0x85);
        }
        else {
            fpu_constant(out, offsetof(FPUInvocation, memory.all), !!(opcode & 16), 0);
            fpu_constant(out, offsetof(FPUInvocation, memory.global), !!(opcode & 8), 0);
            if (!(opcode & 16)) {
                /* Undefined nonordinary DFC address flushes select no entries,
                   matching the checked boundary's documented choice. */
                MMU_BYTES(out, 0x83, 0xe0, 3, 0xff, 0xc8, 0x83, 0xf8, 1);
                skipped = jump(out, 0x87);
            }
            fpu_call_packet(out, NATIVE040_FLUSH, offsetof(FPUInvocation, memory), failures, &count);
        }
    }
    if (skipped)
        patch(out, skipped, out->size);
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_DONE, 0);
    instruction_end(out, pc, movec || stop ? 4 : 2);
    byte(out, 0xc3);
    for (unsigned i = 0; i < count; ++i)
        patch(out, failures[i], out->size);
    instruction_end(out, pc, 0);
    byte(out, 0xc3);
    patch(out, privilege, out->size);
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_constant(out, offsetof(FPUInvocation, exit), NATIVE_EXIT_PRIVILEGE, 0);
    instruction_end(out, pc, 0);
    byte(out, 0xc3);
    if (illegal) {
        patch(out, illegal, out->size);
        pointer_r8(out, RELOC_FPU_FRAME);
        fpu_constant(out, offsetof(FPUInvocation, exit), NATIVE_EXIT_ILLEGAL, 0);
        instruction_end(out, pc, 0);
        byte(out, 0xc3);
    }
    return 1;
}

#undef MMU_BYTES
