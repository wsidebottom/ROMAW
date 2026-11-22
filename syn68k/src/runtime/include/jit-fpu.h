/*+===================================================================
  File:      jit-fpu.h

  Summary:   Emit native FPU arithmetic, control transfers and predicates with checked exceptional exits.

  Classes:   None.

  Functions: Private emitter definitions included by jit-x64.c.

  Origin:    Original ARDI Executor direct 68040 FPU lowering.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private to jit-x64.c; CPUState remains in RCX outside a closed root call. */
_Static_assert(sizeof(Syn68kFP80) == 16 && offsetof(Syn68kFP80, exponent) == 0 &&
                   offsetof(Syn68kFP80, significand) == 8,
               "native indexed FPR layout");
_Static_assert(offsetof(Syn68kNative040Float, high) == 0 && offsetof(Syn68kNative040Float, low) == 8,
               "native condition-result layout");
_Static_assert(offsetof(Syn68kNative040FPU, tininess) == offsetof(Syn68kNative040FPU, rounding) + 3,
               "native packed floating control word");
_Static_assert(offsetof(FPUInvocation, arithmetic) == 0 && NATIVE040_FCMP == 10 && NATIVE040_FTST == 11,
               "native arithmetic invocation and root classes");

#define FP_BYTES(out, ...)                                     \
    do {                                                       \
        const unsigned char data[] = {__VA_ARGS__};            \
        memcpy((out)->bytes + (out)->size, data, sizeof data); \
        (out)->size += sizeof data;                            \
    } while (0)

static unsigned fpu_jump(emitter* out)
{
    byte(out, 0xe9);
    unsigned location = out->size;
    word32(out, 0);
    return location;
}

static void fpu_packet(emitter* out, unsigned op, unsigned reg, unsigned offset, int wide)
{
    byte(out, wide ? 0x49 : 0x41); // R8 carries the invocation.
    if (op > 255)
        byte(out, op >> 8);
    byte(out, op & 255);
    byte(out, 0x80 | reg << 3);
    word32(out, offset);
}

static void fpu_constant(emitter* out, unsigned offset, unsigned value, int small)
{
    fpu_packet(out, small ? 0xc6 : 0xc7, 0, offset, 0);
    if (small)
        byte(out, value);
    else
        word32(out, value);
}

static void fpu_cpu(emitter* out, unsigned op, unsigned reg, unsigned offset)
{
    if (op > 255)
        byte(out, op >> 8);
    byte(out, op & 255);
    byte(out, 0x81 | reg << 3);
    word32(out, offset);
}

/* Copy an indexed FPR to the private arithmetic packet, retaining its exact
   80-bit encoding. Register-source operands have no EA update or callback. */
static void fpu_operand(emitter* out, int source)
{
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0xc1, 0xea, source ? 6 : 3, 0x83, 0xe2, 0x70, 0x4c, 0x8d, 0x89);
    word32(out, offsetof(CPUState, fpu.regs));
    FP_BYTES(out, 0x49, 0x01, 0xd1, 0x41, 0x0f, 0xb7, 0x01, 0x49, 0x8b, 0x51, 8);
    unsigned offset = source ? offsetof(FPUInvocation, arithmetic.source) : offsetof(FPUInvocation, arithmetic.target);
    byte(out, 0x66);
    fpu_packet(out, 0x89, 0, offset + offsetof(Syn68kNative040Float, high), 0);
    fpu_packet(out, 0x89, 2, offset + offsetof(Syn68kNative040Float, low), 1);
}

/* EAX/EDX contain the exponent/significand just copied to the packet. */
static void fpu_normal_guard(emitter* out, unsigned failures[], unsigned* count)
{
    byte(out, 0x25);
    word32(out, 0x7fff);
    byte(out, 0x3d);
    word32(out, 0x7fff);
    failures[(*count)++] = jump(out, 0x84);
    FP_BYTES(out, 0x48, 0x85, 0xd2);
    unsigned zero = jump(out, 0x84);
    failures[(*count)++] = jump(out, 0x89); // Nonzero unnormal/denormal operand.
    unsigned normal = fpu_jump(out);
    patch(out, zero, out->size);
    FP_BYTES(out, 0x85, 0xc0);
    failures[(*count)++] = jump(out, 0x85);
    patch(out, normal, out->size);
}

static void fpu_rounding(emitter* out, int mapped)
{
    if (mapped)
        fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, entry.fpcr), 0);
    else
        fpu_cpu(out, 0x8b, 0, offsetof(CPUState, fpu.fpcr));
    FP_BYTES(out, 0x89, 0xc2, 0xc1, 0xea, 4, 0x83, 0xe2, 3, 0xc1, 0xe8, 6, 0x83, 0xe0, 3, 0x83, 0xf8, 1);
    unsigned single = jump(out, 0x84);
    FP_BYTES(out, 0x83, 0xf8, 2);
    unsigned double_precision = jump(out, 0x84);
    byte(out, 0xb8);
    word32(out, 80 << 8);
    unsigned extended = fpu_jump(out);
    patch(out, single, out->size);
    byte(out, 0xb8);
    word32(out, 32 << 8);
    unsigned selected = fpu_jump(out);
    patch(out, double_precision, out->size);
    byte(out, 0xb8);
    word32(out, 64 << 8);
    patch(out, extended, out->size);
    patch(out, selected, out->size);
    FP_BYTES(out, 0x09, 0xd0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.rounding), 0); // Also clear flags/tininess.
}

static void fpu_result_guard(emitter* out, unsigned failures[], unsigned* count)
{
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, arithmetic.status), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x0fb6, 0, offsetof(FPUInvocation, arithmetic.flags), 0);
    byte(out, 0xa9);
    word32(out, ~0x20u); // Only masked inexact is an ordinary result.
    failures[(*count)++] = jump(out, 0x85);
    FP_BYTES(out, 0x85, 0xc0);
    unsigned exact = jump(out, 0x84);
    fpu_cpu(out, 0xf7, 0, offsetof(CPUState, fpu.fpcr));
    word32(out, 0x200);
    failures[(*count)++] = jump(out, 0x85);
    patch(out, exact, out->size);
}

/* Dn transfers select one control register; An transfers permit FPIAR only.
   Return the successful completion jump, patched after the arithmetic body. */
static unsigned fpu_control_register(emitter* out, unsigned opcode, uint32 pc, unsigned failures[], unsigned* count)
{
    unsigned reg = opcode & 63;
    if (reg >= 16)
        return 0;
    FP_BYTES(out, 0x89, 0xc2, 0x81, 0xe2);
    word32(out, 0xc000);
    FP_BYTES(out, 0x81, 0xfa);
    word32(out, 0x8000);
    unsigned arithmetic = jump(out, 0x85);
    byte(out, 0xa9);
    word32(out, 0x03ff);
    failures[(*count)++] = jump(out, 0x85);
    const unsigned offsets[] = {offsetof(CPUState, fpu.fpcr), offsetof(CPUState, fpu.fpsr),
                                offsetof(CPUState, fpu.fpiar)};
    const uint32 masks[] = {0x0000fff0, 0x0ffffff8, UINT32_MAX};
    unsigned done[3], done_count = 0;
    unsigned general = offsetof(CPUState, regs) + reg * sizeof cpu_state.regs[0];
    for (unsigned i = reg < 8 ? 0 : 2; i < 3; ++i) {
        FP_BYTES(out, 0x89, 0xc2, 0x81, 0xe2);
        word32(out, 0x1c00);
        FP_BYTES(out, 0x81, 0xfa);
        word32(out, (4 >> i) << 10);
        unsigned other = jump(out, 0x85);
        byte(out, 0xa9);
        word32(out, 0x2000);
        unsigned load_control = jump(out, 0x84);
        fpu_cpu(out, 0x8b, 2, offsets[i]);
        fpu_cpu(out, 0x89, 2, general);
        unsigned stored = fpu_jump(out);
        patch(out, load_control, out->size);
        fpu_cpu(out, 0x8b, 2, general);
        if (i != 2) {
            FP_BYTES(out, 0x81, 0xe2);
            word32(out, masks[i]);
        }
        fpu_cpu(out, 0x89, 2, offsets[i]);
        patch(out, stored, out->size);
        done[done_count++] = fpu_jump(out);
        patch(out, other, out->size);
    }
    failures[(*count)++] = fpu_jump(out); // Empty/multiple mask or disallowed An control.
    for (unsigned i = 0; i < done_count; ++i)
        patch(out, done[i], out->size);
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
    word32(out, 0);
    instruction_end(out, pc, 4);
    unsigned completed = fpu_jump(out);
    patch(out, arithmetic, out->size);
    return completed;
}

/* UINT32_MAX selects the arithmetic root stored in the invocation. */
static void fpu_call_packet(emitter* out, unsigned root, unsigned offset, unsigned failures[], unsigned* count)
{
    if (root == UINT32_MAX || root == UINT32_MAX - 1)
        fpu_packet(out, 0x8b, 2,
                   root == UINT32_MAX ? offsetof(FPUInvocation, operation) : offsetof(FPUInvocation, conversion), 0);
    else {
        byte(out, 0xba);
        word32(out, root);
    }
    pointer_r8(out, RELOC_FPU_ROOTS);
    FP_BYTES(out, 0x49, 0x8b, 0x04, 0xd0, 0x48, 0x85, 0xc0);
    failures[(*count)++] = jump(out, 0x84);
    FP_BYTES(out, 0x51, 0x48, 0x83, 0xec, 32, 0x48, 0xb9);
    pointer_value(out, RELOC_FPU_FRAME);
    if (offset) {
        FP_BYTES(out, 0x48, 0x81, 0xc1);
        word32(out, offset);
    }
    FP_BYTES(out, 0xff, 0xd0, 0x48, 0x83, 0xc4, 32, 0x59);
    pointer_r8(out, RELOC_FPU_FRAME);
}

static void fpu_call_root(emitter* out, unsigned root, unsigned failures[], unsigned* count)
{
    fpu_call_packet(out, root, 0, failures, count);
}

/* Class-2 Dn operands permit integer long/word/byte and IEEE single input.
   Conversion has no guest effects, so exceptional values can exit unchanged. */
static void fpu_data_register_source(emitter* out, unsigned reg, unsigned failures[], unsigned* count)
{
    const unsigned formats[] = {0, 1, 4, 6};
    unsigned matches[4];
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0x81, 0xe2);
    word32(out, 0x1c00);
    for (unsigned i = 0; i < 4; ++i) {
        FP_BYTES(out, 0x81, 0xfa);
        word32(out, formats[i] << 10);
        unsigned other = jump(out, 0x85);
        fpu_cpu(out, 0x8b, 0, offsetof(CPUState, regs) + reg * sizeof cpu_state.regs[0]);
        if (formats[i] == 4 || formats[i] == 6)
            FP_BYTES(out, 0x0f, formats[i] == 4 ? 0xbf : 0xbe, 0xc0);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.bits), 1);
        fpu_call_root(out, formats[i] == 1 ? NATIVE040_FROM_F32 : NATIVE040_FROM_I32, failures, count);
        matches[i] = fpu_jump(out);
        patch(out, other, out->size);
    }
    failures[(*count)++] = fpu_jump(out);
    for (unsigned i = 0; i < 4; ++i)
        patch(out, matches[i], out->size);
    fpu_packet(out, 0x0fb7, 0, offsetof(FPUInvocation, arithmetic.result.high), 0);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, arithmetic.result.low), 1);
    byte(out, 0x66);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.source.high), 0);
    fpu_packet(out, 0x89, 2, offsetof(FPUInvocation, arithmetic.source.low), 1);
}

static unsigned fpu_data_register_destination(emitter* out, unsigned opcode, uint32 pc, int mapped, unsigned failures[],
                                              unsigned* count)
{
    unsigned reg = opcode & 63;
    if (reg >= 8)
        return 0;
    FP_BYTES(out, 0x89, 0xc2, 0x81, 0xe2);
    word32(out, 0xe000);
    FP_BYTES(out, 0x81, 0xfa);
    word32(out, 0x6000);
    unsigned arithmetic = jump(out, 0x85);
    byte(out, 0xa9);
    word32(out, 127);
    failures[(*count)++] = jump(out, 0x85);
    fpu_rounding(out, mapped);
    const unsigned formats[] = {0, 1, 4, 6};
    const unsigned roots[] = {NATIVE040_TO_I32, NATIVE040_TO_F32, NATIVE040_TO_I16, NATIVE040_TO_I8};
    unsigned matches[4];
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0x81, 0xe2);
    word32(out, 0x1c00);
    for (unsigned i = 0; i < 4; ++i) {
        FP_BYTES(out, 0x81, 0xfa);
        word32(out, formats[i] << 10);
        unsigned other = jump(out, 0x85);
        fpu_constant(out, offsetof(FPUInvocation, operation), roots[i], 0);
        matches[i] = fpu_jump(out);
        patch(out, other, out->size);
    }
    failures[(*count)++] = fpu_jump(out);
    for (unsigned i = 0; i < 4; ++i)
        patch(out, matches[i], out->size);
    fpu_operand(out, 0); // The move-out source uses command bits 7..9.
    byte(out, 0x66);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.source.high), 0);
    fpu_packet(out, 0x89, 2, offsetof(FPUInvocation, arithmetic.source.low), 1);
    fpu_normal_guard(out, failures, count);
    fpu_call_root(out, UINT32_MAX, failures, count);
    fpu_result_guard(out, failures, count);

    // Move-out updates status/accrued flags without changing floating CCs.
    fpu_cpu(out, 0x8b, 2, offsetof(CPUState, fpu.fpsr));
    FP_BYTES(out, 0x81, 0xe2);
    word32(out, 0xffff00ff);
    FP_BYTES(out, 0x85, 0xc0);
    unsigned exact = jump(out, 0x84);
    FP_BYTES(out, 0x81, 0xca);
    word32(out, 0x208);
    patch(out, exact, out->size);
    fpu_cpu(out, 0x89, 2, offsetof(CPUState, fpu.fpsr));
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.fpiar));
    word32(out, pc);
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
    word32(out, 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, arithmetic.bits), 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, operation), 0);
    byte(out, NATIVE040_TO_I8);
    unsigned byte_store = jump(out, 0x84);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, operation), 0);
    byte(out, NATIVE040_TO_I16);
    unsigned word_store = jump(out, 0x84);
    unsigned offset = offsetof(CPUState, regs) + reg * sizeof cpu_state.regs[0];
    fpu_cpu(out, 0x89, 0, offset);
    unsigned stored_long = fpu_jump(out);
    patch(out, byte_store, out->size);
    fpu_cpu(out, 0x88, 0, offset);
    unsigned stored_byte = fpu_jump(out);
    patch(out, word_store, out->size);
    byte(out, 0x66);
    fpu_cpu(out, 0x89, 0, offset);
    patch(out, stored_long, out->size);
    patch(out, stored_byte, out->size);
    instruction_end(out, pc, 4);
    unsigned completed = fpu_jump(out);
    patch(out, arithmetic, out->size);
    return completed;
}

static void mapped_fetch_command(emitter* out, uint32 pc, unsigned failures[], unsigned* count)
{
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_constant(out, offsetof(FPUInvocation, memory.address), pc + 2, 0);
    fpu_constant(out, offsetof(FPUInvocation, memory.size), 2, 0);
    fpu_cpu(out, 0x0fb7, 0, offsetof(CPUState, sr));
    FP_BYTES(out, 0xc1, 0xe8, 11, 0x83, 0xe0, 4, 0x83, 0xc8, 2);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.fc), 0);
    fpu_call_packet(out, NATIVE040_READ, offsetof(FPUInvocation, memory), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_ACCESS, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.translation.fault), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.bus_error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_COMMAND, 0);
}

/* Each operand transfer uses the original EA and an invocation-local bus.
   Missing roots after an EA side effect cannot restart the semantic decoder. */
static void fpu_mapped_transfer(emitter* out, unsigned offset, unsigned size, int writing, unsigned failures[],
                                unsigned* count)
{
    fpu_cpu(out, 0x0fb7, 0, offsetof(CPUState, sr));
    FP_BYTES(out, 0xc1, 0xe8, 11, 0x83, 0xe0, 4, 0x83, 0xc8, 1);
    fpu_packet(out, 0x03, 0, offsetof(FPUInvocation, ea.result.instruction), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.fc), 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.result.address), 0);
    if (offset) {
        byte(out, 0x05);
        word32(out, offset);
    }
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.address), 0);
    if (size)
        fpu_constant(out, offsetof(FPUInvocation, memory.size), size, 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_call_packet(out, writing ? NATIVE040_WRITE : NATIVE040_READ, offsetof(FPUInvocation, memory), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_ACCESS, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.translation.fault), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.bus_error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, memory.value), 0);
}

static void fpu_mapped_read(emitter* out, unsigned offset, unsigned size, unsigned failures[], unsigned* count)
{
    fpu_mapped_transfer(out, offset, size, 0, failures, count);
}

/* Mapped buses defer validation to each transfer. The same emitted body
   preflights complete flat stores before their first scalar is published. */
static void fpu_mapped_preflight(emitter* out, unsigned failures[], unsigned* count)
{
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.result.address), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.address), 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.size), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.size), 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_call_packet(out, NATIVE040_SPAN, offsetof(FPUInvocation, memory), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_ACCESS, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.translation.fault), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.bus_error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
}

/* Control lists transfer in FPCR/FPSR/FPIAR order. Each scalar observes live
   control registers and privilege after the preceding callback. */
static unsigned fpu_mapped_control_memory(emitter* out, unsigned ea, uint32 pc, unsigned failures[], unsigned* count)
{
    if (ea < 16)
        return 0;
    FP_BYTES(out, 0x89, 0xc2, 0x81, 0xe2);
    word32(out, 0xc000);
    FP_BYTES(out, 0x81, 0xfa);
    word32(out, 0x8000);
    unsigned arithmetic = jump(out, 0x85);
    byte(out, 0xa9);
    word32(out, 0x03ff);
    failures[(*count)++] = jump(out, 0x85);
    byte(out, 0xa9);
    word32(out, 0x1c00);
    failures[(*count)++] = jump(out, 0x84);
    if (ea > 60)
        failures[(*count)++] = fpu_jump(out);
    else if (ea >= 58) {
        byte(out, 0xa9);
        word32(out, 0x2000);
        failures[(*count)++] = jump(out, 0x85); // PC-relative/immediate destinations are reserved.
    }
    FP_BYTES(out, 0xc1, 0xe8, 10, 0x83, 0xe0, 1);
    for (unsigned bit = 11; bit <= 12; ++bit) {
        fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, command), 0);
        FP_BYTES(out, 0xc1, 0xea, bit, 0x83, 0xe2, 1, 0x01, 0xd0);
    }
    FP_BYTES(out, 0xc1, 0xe0, 2);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, ea.size), 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0xc1, 0xe8, 13, 0x83, 0xe0, 1);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, ea.writing), 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.code), ea, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.registers), 2, 0);
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
    word32(out, 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_call_packet(out, NATIVE040_EA, offsetof(FPUInvocation, ea), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_EA, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.context.error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.context.pc), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, next), 0);
    const unsigned registers[] = {offsetof(CPUState, fpu.fpcr), offsetof(CPUState, fpu.fpsr),
                                  offsetof(CPUState, fpu.fpiar)};
    const uint32 masks[] = {0xfff0, 0x0ffffff8, UINT32_MAX};
    for (unsigned i = 0; i < 3; ++i) {
        fpu_packet(out, 0xf7, 0, offsetof(FPUInvocation, command), 0);
        word32(out, 0x1000 >> i);
        unsigned skipped = jump(out, 0x84);
        fpu_packet(out, 0xf7, 0, offsetof(FPUInvocation, command), 0);
        word32(out, 0x2000);
        unsigned reading = jump(out, 0x84);
        fpu_constant(out, offsetof(FPUInvocation, operation), i, 0);
        fpu_cpu(out, 0x8b, 0, registers[i]);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
        fpu_mapped_transfer(out, 0, 4, 1, failures, count);
        unsigned written = fpu_jump(out);
        patch(out, reading, out->size);
        fpu_mapped_read(out, 0, 4, failures, count);
        if (masks[i] != UINT32_MAX) {
            byte(out, 0x25);
            word32(out, masks[i]);
        }
        fpu_cpu(out, 0x89, 0, registers[i]);
        patch(out, written, out->size);
        fpu_packet(out, 0x83, 0, offsetof(FPUInvocation, ea.result.address), 0);
        byte(out, 4);
        patch(out, skipped, out->size);
    }
    instruction_end(out, pc, 4);
    unsigned completed = fpu_jump(out);
    patch(out, arithmetic, out->size);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, command), 0);
    return completed;
}

/* Register values are captured before EA callbacks, as in the hardware
   move-out pipeline. Only conversion exceptions leave through the captured
   destination helper; ordinary stores retain their per-transfer effects. */
static unsigned fpu_mapped_destination(emitter* out, unsigned ea, uint32 pc, unsigned failures[], unsigned* count)
{
    if (ea < 16)
        return 0;
    FP_BYTES(out, 0x89, 0xc2, 0x81, 0xe2);
    word32(out, 0xe000);
    FP_BYTES(out, 0x81, 0xfa);
    word32(out, 0x6000);
    unsigned arithmetic = jump(out, 0x85);
    byte(out, 0xa9);
    word32(out, 127);
    failures[(*count)++] = jump(out, 0x85);
    const unsigned formats[] = {0, 1, 2, 4, 5, 6};
    const unsigned sizes[] = {4, 4, 12, 2, 8, 1};
    const unsigned roots[] = {NATIVE040_TO_I32, NATIVE040_TO_F32, NATIVE040_COUNT,
                              NATIVE040_TO_I16, NATIVE040_TO_F64, NATIVE040_TO_I8};
    unsigned matches[6];
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0x81, 0xe2);
    word32(out, 0x1c00);
    for (unsigned i = 0; i < 6; ++i) {
        FP_BYTES(out, 0x81, 0xfa);
        word32(out, formats[i] << 10);
        unsigned other = jump(out, 0x85);
        fpu_constant(out, offsetof(FPUInvocation, ea.size), sizes[i], 0);
        fpu_constant(out, offsetof(FPUInvocation, memory.size), sizes[i] > 4 ? 4 : sizes[i], 0);
        fpu_constant(out, offsetof(FPUInvocation, operation), roots[i], 0);
        matches[i] = fpu_jump(out);
        patch(out, other, out->size);
    }
    failures[(*count)++] = fpu_jump(out);
    for (unsigned i = 0; i < 6; ++i)
        patch(out, matches[i], out->size);
    fpu_operand(out, 0);
    byte(out, 0x66);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.source.high), 0);
    fpu_packet(out, 0x89, 2, offsetof(FPUInvocation, arithmetic.source.low), 1);
    fpu_normal_guard(out, failures, count); // Before EA: the checked command still owns all effects.
    fpu_rounding(out, 1);
    fpu_constant(out, offsetof(FPUInvocation, ea.code), ea, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.writing), 1, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.registers), 1, 0);
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
    word32(out, 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_call_packet(out, NATIVE040_EA, offsetof(FPUInvocation, ea), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_EA, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.context.error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.context.pc), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, next), 0);
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.fpiar));
    word32(out, pc);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_DESTINATION, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 12);
    unsigned raw_extended = jump(out, 0x84);
    fpu_call_root(out, UINT32_MAX, failures, count);
    patch(out, raw_extended, out->size);
    fpu_result_guard(out, failures, count);
    fpu_cpu(out, 0x8b, 2, offsetof(CPUState, fpu.fpsr));
    FP_BYTES(out, 0x81, 0xe2);
    word32(out, 0xffff00ff);
    FP_BYTES(out, 0x85, 0xc0);
    unsigned exact = jump(out, 0x84);
    FP_BYTES(out, 0x81, 0xca);
    word32(out, 0x208);
    patch(out, exact, out->size);
    fpu_cpu(out, 0x89, 2, offsetof(CPUState, fpu.fpsr));
    fpu_packet(out, 0x89, 2, offsetof(FPUInvocation, exception.replay_fpsr), 0);
    fpu_mapped_preflight(out, failures, count);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 12);
    unsigned extended_store = jump(out, 0x84);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 8);
    unsigned double_store = jump(out, 0x84);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, arithmetic.bits), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_mapped_transfer(out, 0, 0, 1, failures, count);
    unsigned scalar_done = fpu_jump(out);
    patch(out, double_store, out->size);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, arithmetic.bits), 1);
    FP_BYTES(out, 0x48, 0xc1, 0xe8, 32);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_mapped_transfer(out, 0, 4, 1, failures, count);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, arithmetic.bits), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_mapped_transfer(out, 4, 4, 1, failures, count);
    unsigned double_done = fpu_jump(out);
    patch(out, extended_store, out->size);
    fpu_packet(out, 0x0fb7, 0, offsetof(FPUInvocation, arithmetic.source.high), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_mapped_transfer(out, 0, 2, 1, failures, count);
    fpu_constant(out, offsetof(FPUInvocation, memory.value), 0, 0);
    fpu_mapped_transfer(out, 2, 2, 1, failures, count);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, arithmetic.source.low), 1);
    FP_BYTES(out, 0x48, 0xc1, 0xe8, 32);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_mapped_transfer(out, 4, 4, 1, failures, count);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, arithmetic.source.low), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_mapped_transfer(out, 8, 4, 1, failures, count);
    patch(out, scalar_done, out->size);
    patch(out, double_done, out->size);
    instruction_end(out, pc, 4);
    unsigned completed = fpu_jump(out);
    patch(out, arithmetic, out->size);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, command), 0);
    return completed;
}

/* The list mask is captured before EA callbacks. Raw transfer roots have
   no opcode/EA decoder and preserve all 80-bit register encodings. */
static unsigned fpu_mapped_register_list(emitter* out, unsigned ea, uint32 pc, unsigned failures[], unsigned* count)
{
    if (ea < 16)
        return 0;
    _Static_assert(sizeof cpu_state.regs[0] == 4, "native indexed data-register stride");
    FP_BYTES(out, 0x89, 0xc2, 0x81, 0xe2);
    word32(out, 0xc000);
    FP_BYTES(out, 0x81, 0xfa);
    word32(out, 0xc000);
    unsigned arithmetic = jump(out, 0x85);
    byte(out, 0xa9);
    word32(out, 0x0700);
    failures[(*count)++] = jump(out, 0x85);
    byte(out, 0xa9);
    word32(out, 0x0800);
    unsigned static_list = jump(out, 0x84);
    byte(out, 0xa9);
    word32(out, 0x008f);
    failures[(*count)++] = jump(out, 0x85);
    patch(out, static_list, out->size);
    byte(out, 0xa9);
    word32(out, 0x1000);
    failures[(*count)++] = jump(out, ea >> 3 == 4 ? 0x85 : 0x84);
    if (ea >> 3 == 4 || ea >> 3 == 3) {
        byte(out, 0xa9);
        word32(out, 0x2000);
        failures[(*count)++] = jump(out, ea >> 3 == 4 ? 0x84 : 0x85);
    }
    byte(out, 0xa9);
    word32(out, 0x0800);
    unsigned immediate_mask = jump(out, 0x84);
    FP_BYTES(out, 0x89, 0xc2, 0xc1, 0xea, 4, 0x83, 0xe2, 7, 0x8b, 0x84, 0x91);
    word32(out, offsetof(CPUState, regs));
    patch(out, immediate_mask, out->size);
    byte(out, 0x25);
    word32(out, 255);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, registers.mask), 0);
    // Eight-bit population count; POPCNT is not a baseline Win64 instruction.
    FP_BYTES(out, 0x89, 0xc2, 0xd1, 0xea, 0x83, 0xe2, 0x55, 0x29, 0xd0, 0x89, 0xc2, 0x83, 0xe0, 0x33, 0xc1, 0xea, 2,
             0x83, 0xe2, 0x33, 0x01, 0xd0, 0x89, 0xc2, 0xc1, 0xea, 4, 0x01, 0xd0, 0x83, 0xe0, 15, 0x6b, 0xc0, 12);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, ea.size), 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0xc1, 0xe8, 13, 0x83, 0xe0, 1);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, ea.writing), 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.code), ea, 0);
    fpu_constant(out, offsetof(FPUInvocation, registers.reverse), ea >> 3 == 4, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.registers), 0, 0);
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
    word32(out, 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_call_packet(out, NATIVE040_EA, offsetof(FPUInvocation, ea), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_EA, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.context.error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.context.pc), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, next), 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.result.address), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, registers.address), 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_packet(out, 0xf7, 0, offsetof(FPUInvocation, command), 0);
    word32(out, 0x2000);
    unsigned reading = jump(out, 0x84);
    fpu_call_packet(out, NATIVE040_STORE_FPRS, offsetof(FPUInvocation, registers), failures, count);
    unsigned written = fpu_jump(out);
    patch(out, reading, out->size);
    fpu_call_packet(out, NATIVE040_LOAD_FPRS, offsetof(FPUInvocation, registers), failures, count);
    patch(out, written, out->size);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_REGISTERS, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, registers.ok), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x84);
    instruction_end(out, pc, 4);
    unsigned completed = fpu_jump(out);
    patch(out, arithmetic, out->size);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, command), 0);
    return completed;
}

/* Validated class-2 arithmetic only. The closed EA root consumes live
   extensions and commits automatic addressing once. Complete source capture
   precedes every arithmetic exceptional exit. */
static void fpu_mapped_source(emitter* out, unsigned ea, unsigned failures[], unsigned* count)
{
    const unsigned formats[] = {0, 1, 2, 4, 5, 6};
    const unsigned sizes[] = {4, 4, 12, 2, 8, 1};
    const unsigned roots[] = {NATIVE040_FROM_I32, NATIVE040_FROM_F32, NATIVE040_COUNT,
                              NATIVE040_FROM_I32, NATIVE040_FROM_F64, NATIVE040_FROM_I32};
    unsigned matches[6];
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0x81, 0xe2);
    word32(out, 0x1c00);
    for (unsigned i = 0; i < 6; ++i) {
        FP_BYTES(out, 0x81, 0xfa);
        word32(out, formats[i] << 10);
        unsigned other = jump(out, 0x85);
        fpu_constant(out, offsetof(FPUInvocation, ea.size), sizes[i], 0);
        fpu_constant(out, offsetof(FPUInvocation, memory.size), sizes[i] > 4 ? 4 : sizes[i], 0);
        fpu_constant(out, offsetof(FPUInvocation, conversion), roots[i], 0);
        matches[i] = fpu_jump(out);
        patch(out, other, out->size);
    }
    failures[(*count)++] = fpu_jump(out); // Packed/reserved formats have consumed no EA.
    for (unsigned i = 0; i < 6; ++i)
        patch(out, matches[i], out->size);
    fpu_constant(out, offsetof(FPUInvocation, ea.code), ea, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.registers), 1, 0);
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
    word32(out, 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_call_packet(out, NATIVE040_EA, offsetof(FPUInvocation, ea), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_EA, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.context.error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.context.pc), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, next), 0);
    fpu_mapped_read(out, 0, 0, failures, count);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.bits), 1);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 8);
    unsigned scalar = jump(out, 0x82);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 12);
    unsigned double_input = jump(out, 0x85);
    FP_BYTES(out, 0xc1, 0xe8, 16, 0x66);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.source.high), 0);
    patch(out, double_input, out->size);
    fpu_mapped_read(out, 4, 4, failures, count);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 12);
    unsigned extended_input = jump(out, 0x84);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, arithmetic.bits), 1);
    FP_BYTES(out, 0x48, 0xc1, 0xe2, 32, 0x48, 0x09, 0xd0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.bits), 1);
    unsigned converted = fpu_jump(out);
    patch(out, extended_input, out->size);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.bits), 1);
    fpu_mapped_read(out, 8, 4, failures, count);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, arithmetic.bits), 1);
    FP_BYTES(out, 0x48, 0xc1, 0xe2, 32, 0x48, 0x09, 0xd0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.source.low), 1);
    unsigned ready = fpu_jump(out);
    patch(out, scalar, out->size);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 1);
    unsigned not_byte = jump(out, 0x85);
    FP_BYTES(out, 0x0f, 0xbe, 0xc0);
    patch(out, not_byte, out->size);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.size), 0);
    byte(out, 2);
    unsigned not_word = jump(out, 0x85);
    FP_BYTES(out, 0x0f, 0xbf, 0xc0);
    patch(out, not_word, out->size);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.bits), 1);
    patch(out, converted, out->size);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
    fpu_call_root(out, UINT32_MAX - 1, failures, count);
    fpu_packet(out, 0x0fb7, 0, offsetof(FPUInvocation, arithmetic.result.high), 0);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, arithmetic.result.low), 1);
    byte(out, 0x66);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, arithmetic.source.high), 0);
    fpu_packet(out, 0x89, 2, offsetof(FPUInvocation, arithmetic.source.low), 1);
    patch(out, ready, out->size);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_CAPTURED, 0);
    const unsigned tags[] = {offsetof(FPUInvocation, arithmetic.unsupported),
                             offsetof(FPUInvocation, arithmetic.signaling),
                             offsetof(FPUInvocation, arithmetic.source_tag)};
    for (unsigned i = 0; i < 3; ++i) {
        fpu_packet(out, 0x83, 7, tags[i], 0);
        byte(out, 0);
        failures[(*count)++] = jump(out, 0x85);
    }
    fpu_packet(out, 0x0fb7, 0, offsetof(FPUInvocation, arithmetic.source.high), 0);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, arithmetic.source.low), 1);
}

static int fpu_register_mode(emitter* out, unsigned opcode, uint32 pc, int mapped)
{
    if ((opcode & 0xffc0) != 0xf200)
        return 0;
    if (out->size + (mapped ? 8192 : 4096) > sizeof out->bytes)
        return -1; // End the prefix before the FPU instruction rather than overflow it.
    unsigned previous_selector = out->decoder_selector;
    out->decoder_selector = 11;
    unsigned failures[128], count = 0;
    ++out->guards;
    out->dynamic = 1;
    /* Pending FSAVE state requires architectural exception/replay handling. */
    fpu_cpu(out, 0x83, 7, offsetof(CPUState, fpu.frame_size));
    byte(out, 0);
    failures[count++] = jump(out, 0x85);
    if (mapped)
        mapped_fetch_command(out, pc, failures, &count);
    else {
        byte(out, 0xba);
        word32(out, pc + 2);
        memory_guard(out, 2, pc, 1);
        memory_load(out, 1, 0);
    }
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, command), 0);
    unsigned memory_control_completed = mapped ? fpu_mapped_control_memory(out, opcode & 63, pc, failures, &count) : 0;
    unsigned memory_destination_completed = mapped ? fpu_mapped_destination(out, opcode & 63, pc, failures, &count) : 0;
    unsigned list_completed = mapped ? fpu_mapped_register_list(out, opcode & 63, pc, failures, &count) : 0;
    unsigned control_completed = fpu_control_register(out, opcode, pc, failures, &count);
    unsigned destination_completed = fpu_data_register_destination(out, opcode, pc, mapped, failures, &count);
    unsigned ea = opcode & 63;
    if (ea >= 8 && (!mapped || ea < 16))
        failures[count++] = fpu_jump(out); // An inputs and flat memory use the checked path.
    byte(out, 0xa9);
    word32(out, 0xa000); // Register arithmetic or a class-2 scalar input.
    failures[count++] = jump(out, 0x85);
    if (ea) {
        byte(out, 0xa9);
        word32(out, 0x4000);
        failures[count++] = jump(out, 0x84); // Register arithmetic requires an empty EA field.
    }

    fpu_rounding(out, mapped);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0x83, 0xe0, 127);
    static const unsigned char operations[][3] = {
        {0x22, NATIVE040_FADD, 0},    {0x28, NATIVE040_FSUB, 0},   {0x23, NATIVE040_FMUL, 0},
        {0x20, NATIVE040_FDIV, 0},    {4, NATIVE040_FSQRT, 0},     {0, NATIVE040_FMOVE, 80},
        {0x18, NATIVE040_FABS, 0},    {0x1a, NATIVE040_FNEG, 0},   {0x24, NATIVE040_FSGLDIV, 0},
        {0x27, NATIVE040_FSGLMUL, 0}, {0x38, NATIVE040_FCMP, 0},   {0x3a, NATIVE040_FTST, 0},
        {0x40, NATIVE040_FMOVE, 32},  {0x44, NATIVE040_FMOVE, 64}, {0x41, NATIVE040_FSQRT, 32},
        {0x45, NATIVE040_FSQRT, 64},  {0x58, NATIVE040_FABS, 32},  {0x5c, NATIVE040_FABS, 64},
        {0x5a, NATIVE040_FNEG, 32},   {0x5e, NATIVE040_FNEG, 64},  {0x60, NATIVE040_FDIV, 32},
        {0x64, NATIVE040_FDIV, 64},   {0x62, NATIVE040_FADD, 32},  {0x66, NATIVE040_FADD, 64},
        {0x63, NATIVE040_FMUL, 32},   {0x67, NATIVE040_FMUL, 64},  {0x68, NATIVE040_FSUB, 32},
        {0x6c, NATIVE040_FSUB, 64}};
    unsigned matches[sizeof operations / sizeof *operations];
    for (unsigned i = 0; i < sizeof operations / sizeof *operations; ++i) {
        FP_BYTES(out, 0x83, 0xf8, operations[i][0]);
        unsigned other = jump(out, 0x85);
        fpu_constant(out, offsetof(FPUInvocation, operation), operations[i][1], 0);
        if (operations[i][2])
            fpu_constant(out, offsetof(FPUInvocation, arithmetic.precision), operations[i][2], 1);
        matches[i] = fpu_jump(out);
        patch(out, other, out->size);
    }
    failures[count++] = fpu_jump(out);
    for (unsigned i = 0; i < sizeof matches / sizeof *matches; ++i)
        patch(out, matches[i], out->size);
    fpu_packet(out, 0xf7, 0, offsetof(FPUInvocation, command), 0);
    word32(out, 0x4000);
    unsigned fp_source = jump(out, 0x84);
    if (mapped && ea >= 16)
        fpu_mapped_source(out, ea, failures, &count);
    else
        fpu_data_register_source(out, ea & 7, failures, &count);
    unsigned source_ready = fpu_jump(out);
    patch(out, fp_source, out->size);
    fpu_operand(out, 1);
    patch(out, source_ready, out->size);
    fpu_normal_guard(out, failures, &count);
    fpu_operand(out, 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, operation), 0);
    FP_BYTES(out, 0x83, 0xe8, 4, 0x83, 0xf8, 3);
    unsigned unary = jump(out, 0x86);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, operation), 0);
    byte(out, NATIVE040_FTST);
    unsigned test_only = jump(out, 0x84);
    // Reload the target encoding after classifying the operation.
    fpu_packet(out, 0x0fb7, 0, offsetof(FPUInvocation, arithmetic.target.high), 0);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, arithmetic.target.low), 1);
    fpu_normal_guard(out, failures, &count);
    patch(out, unary, out->size);
    patch(out, test_only, out->size);

    fpu_call_root(out, UINT32_MAX, failures, &count);
    fpu_result_guard(out, failures, &count);

    // Commit CC/status from the unscaled condition result, or the actual zero.
    FP_BYTES(out, 0x4d, 0x8d, 0x88);
    word32(out, offsetof(FPUInvocation, arithmetic.condition));
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, arithmetic.finite), 0);
    byte(out, 0);
    unsigned finite = jump(out, 0x85);
    FP_BYTES(out, 0x4d, 0x8d, 0x88);
    word32(out, offsetof(FPUInvocation, arithmetic.result));
    patch(out, finite, out->size);
    FP_BYTES(out, 0x41, 0x0f, 0xb7, 0x01, 0x89, 0xc2, 0x25);
    word32(out, 0x8000);
    FP_BYTES(out, 0xc1, 0xe0, 12, 0x81, 0xe2);
    word32(out, 0x7fff);
    unsigned nonzero_high = jump(out, 0x85);
    FP_BYTES(out, 0x49, 0x83, 0x79, 8, 0);
    unsigned nonzero_low = jump(out, 0x85);
    byte(out, 0x0d);
    word32(out, 0x04000000);
    patch(out, nonzero_high, out->size);
    patch(out, nonzero_low, out->size);
    fpu_cpu(out, 0x8b, 2, offsetof(CPUState, fpu.fpsr));
    FP_BYTES(out, 0x81, 0xe2);
    word32(out, 0x00ff00ff);
    FP_BYTES(out, 0x09, 0xc2);
    fpu_packet(out, 0x80, 7, offsetof(FPUInvocation, arithmetic.flags), 0);
    byte(out, 0);
    unsigned no_inexact = jump(out, 0x84);
    FP_BYTES(out, 0x81, 0xca);
    word32(out, 0x208);
    patch(out, no_inexact, out->size);
    fpu_cpu(out, 0x89, 2, offsetof(CPUState, fpu.fpsr));
    fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.fpiar));
    word32(out, pc);
    // Memory sources cleared null before EA; callbacks may have changed it since.
    if (!mapped || ea < 16) {
        fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
        word32(out, 0);
    }
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, operation), 0);
    byte(out, NATIVE040_FCMP);
    unsigned no_result = jump(out, 0x83);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, command), 0);
    FP_BYTES(out, 0xc1, 0xea, 3, 0x83, 0xe2, 0x70, 0x4c, 0x8d, 0x89);
    word32(out, offsetof(CPUState, fpu.regs));
    FP_BYTES(out, 0x49, 0x01, 0xd1);
    fpu_packet(out, 0x0fb7, 0, offsetof(FPUInvocation, arithmetic.result.high), 0);
    fpu_packet(out, 0x8b, 2, offsetof(FPUInvocation, arithmetic.result.low), 1);
    FP_BYTES(out, 0x66, 0x41, 0x89, 0x01, 0x49, 0x89, 0x51, 8);
    patch(out, no_result, out->size);
    instruction_end(out, pc, 4);
    unsigned completed = fpu_jump(out);
    for (unsigned i = 0; i < count; ++i)
        patch(out, failures[i], out->size);
    if (mapped) {
        instruction_end(out, pc, 0);
        byte(out, 0xc3);
    }
    else if ((opcode & 63) >= 16) {
        // A class-0 register operation with a memory EA is reserved. Retain
        // its checked guard; valid memory classes enter the native bus body.
        fpu_packet(out, 0xf7, 0, offsetof(FPUInvocation, command), 0);
        word32(out, 0xe000);
        unsigned reserved_register = jump(out, 0x84);
        flat_fpu_tail(out, pc, opcode);
        patch(out, reserved_register, out->size);
        decoder_tail(out, pc, 11);
    }
    else
        decoder_tail(out, pc, 11);
    patch(out, completed, out->size);
    if (memory_control_completed)
        patch(out, memory_control_completed, out->size);
    if (memory_destination_completed)
        patch(out, memory_destination_completed, out->size);
    if (list_completed)
        patch(out, list_completed, out->size);
    if (control_completed)
        patch(out, control_completed, out->size);
    if (destination_completed)
        patch(out, destination_completed, out->size);
    if (mapped) {
        pointer_r8(out, RELOC_FPU_FRAME);
        fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_DONE, 0);
        fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, next), 0);
        out->managed = 1;
    }
    out->decoder_selector = previous_selector;
    return 1;
}

static int fpu_register(emitter* out, unsigned opcode, uint32 pc)
{
    return fpu_register_mode(out, opcode, pc, 0);
}

/* The command and ignored operands are fetched as words so ordinary flat RAM
   crossing a page boundary remains native. Failed guards retain FPU priority. */
static void fpu_fetch_value(emitter* out, uint32 pc, uint32 address, unsigned bytes, unsigned offset)
{
    byte(out, 0xba);
    word32(out, address);
    memory_guard(out, 2, pc, 1);
    memory_load(out, 1, 0);
    if (bytes == 4)
        FP_BYTES(out, 0xc1, 0xe0, 16);
    else
        FP_BYTES(out, 0x0f, 0xbf, 0xc0);
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_packet(out, 0x89, 0, offset, 0);
    if (bytes == 4) {
        byte(out, 0xba);
        word32(out, address + 2);
        memory_guard(out, 2, pc, 1);
        memory_load(out, 1, 0);
        pointer_r8(out, RELOC_FPU_FRAME);
        fpu_packet(out, 0x0b, 0, offset, 0);
        fpu_packet(out, 0x89, 0, offset, 0);
    }
}

/* R8 is the invocation base. An enabled BSUN exits before changing FPSR;
   the checked decoder owns the exceptional frame and instruction priority. */
static void fpu_signal_condition(emitter* out, unsigned code, unsigned failures[], unsigned* count)
{
    unsigned quiet = 0;
    if (code == UINT32_MAX) {
        fpu_packet(out, 0xf7, 0, offsetof(FPUInvocation, command), 0);
        word32(out, 16);
        quiet = jump(out, 0x84);
    }
    else if (!(code & 16))
        return;
    fpu_cpu(out, 0xf7, 0, offsetof(CPUState, fpu.fpsr));
    word32(out, 0x01000000);
    unsigned ordered = jump(out, 0x84);
    fpu_cpu(out, 0xf7, 0, offsetof(CPUState, fpu.fpcr));
    word32(out, 0x8000);
    failures[(*count)++] = jump(out, 0x85);
    fpu_cpu(out, 0x81, 1, offsetof(CPUState, fpu.fpsr)); // OR FPSR,BSUN/accrued IOP.
    word32(out, 0x8080);
    patch(out, ordered, out->size);
    if (quiet)
        patch(out, quiet, out->size);
}

static void fpu_condition_value(emitter* out, unsigned code)
{
    fpu_cpu(out, 0x8b, 2, offsetof(CPUState, fpu.fpsr));
    // Four N/Z states per predicate. I is irrelevant; NAN chooses the table.
    FP_BYTES(out, 0x49, 0xb9);
    word32(out, 0xf5e4b1a0);
    word32(out, 0xf5e4b1a0);
    FP_BYTES(out, 0xf7, 0xc2);
    word32(out, 0x01000000);
    unsigned ordered = jump(out, 0x84);
    FP_BYTES(out, 0x49, 0xb9);
    word32(out, 0x00a0a0a0);
    word32(out, 0xf5ffffff);
    patch(out, ordered, out->size);
    FP_BYTES(out, 0xc1, 0xea, 26, 0x83, 0xe2, 3);
    if (code == UINT32_MAX) {
        fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, command), 0);
        FP_BYTES(out, 0x83, 0xe0, 15, 0xc1, 0xe0, 2, 0x09, 0xc2);
    }
    else {
        FP_BYTES(out, 0x83, 0xca, (code & 15) << 2);
    }
    FP_BYTES(out, 0x49, 0x0f, 0xa3, 0xd1, 0x0f, 0x92, 0xc0, 0x0f, 0xb6, 0xc0); // BT; SETC AL.
}

static int mapped_conditional_opcode(unsigned opcode)
{
    return (opcode & 0xffc0) == 0xf240 || (opcode & 0xff80) == 0xf280;
}

static void fpu_predicate_exception(emitter* out, uint32 pc, unsigned vector, uint32 exception_pc, unsigned format,
                                    unsigned failures[], unsigned* count)
{
    if (vector == 11) {
        fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, original_null), 0);
        fpu_cpu(out, 0x89, 0, offsetof(CPUState, fpu.null_state));
    }
    fpu_constant(out, offsetof(FPUInvocation, exception.next), pc, 0);
    fpu_constant(out, offsetof(FPUInvocation, exception.exception_pc), exception_pc, 0);
    fpu_constant(out, offsetof(FPUInvocation, exception.vector), vector, 0);
    fpu_constant(out, offsetof(FPUInvocation, exception.format), format, 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_EXCEPTION, 0);
    failures[(*count)++] = fpu_jump(out);
}

static void fpu_predicate_fetch(emitter* out, uint32 address, unsigned size, unsigned failures[], unsigned* count)
{
    fpu_constant(out, offsetof(FPUInvocation, memory.address), address, 0);
    fpu_constant(out, offsetof(FPUInvocation, memory.size), size, 0);
    fpu_cpu(out, 0x0fb7, 0, offsetof(CPUState, sr));
    FP_BYTES(out, 0xc1, 0xe8, 11, 0x83, 0xe0, 4, 0x83, 0xc8, 2);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.fc), 0);
    fpu_call_packet(out, NATIVE040_READ, offsetof(FPUInvocation, memory), failures, count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_ACCESS, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.translation.fault), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, memory.access.bus_error), 0);
    byte(out, 0);
    failures[(*count)++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, memory.value), 0);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
}

static void fpu_mapped_signal_condition(emitter* out, uint32 pc, unsigned code, unsigned failures[], unsigned* count)
{
    unsigned quiet = 0;
    if (code == UINT32_MAX) {
        fpu_packet(out, 0xf7, 0, offsetof(FPUInvocation, command), 0);
        word32(out, 16);
        quiet = jump(out, 0x84);
    }
    else if (!(code & 16))
        return;
    fpu_cpu(out, 0xf7, 0, offsetof(CPUState, fpu.fpsr));
    word32(out, 0x01000000);
    unsigned ordered = jump(out, 0x84);
    fpu_cpu(out, 0x81, 1, offsetof(CPUState, fpu.fpsr));
    word32(out, 0x8080); // BSUN and accrued IOP precede an enabled exception.
    fpu_cpu(out, 0xf7, 0, offsetof(CPUState, fpu.fpcr));
    word32(out, 0x8000);
    unsigned masked = jump(out, 0x84);
    fpu_predicate_exception(out, pc, 48, pc, 0, failures, count);
    patch(out, masked, out->size);
    patch(out, ordered, out->size);
    if (quiet)
        patch(out, quiet, out->size);
}

static int fpu_mapped_conditional(emitter* out, unsigned opcode, uint32 pc)
{
    if (!mapped_conditional_opcode(opcode))
        return 0;
    if (out->size + 3072 > sizeof out->bytes)
        return -1;
    unsigned previous_selector = out->decoder_selector;
    out->decoder_selector = 11;
    out->managed = out->dynamic = 1;
    ++out->guards;
    unsigned failures[32], count = 0, completed = 0;
    int branch = (opcode & 0xff80) == 0xf280;
    unsigned ea = opcode & 63;
    fpu_cpu(out, 0x83, 7, offsetof(CPUState, fpu.frame_size));
    byte(out, 0);
    failures[count++] = jump(out, 0x85);
    pointer_r8(out, RELOC_FPU_FRAME);
    if (branch && (opcode & 0x20)) {
        fpu_predicate_exception(out, pc, 11, pc, 0, failures, &count);
        goto finish;
    }
    unsigned extra = branch               ? (opcode & 0x40 ? 4 : 2)
                     : ea >= 0x3a         ? (ea == 0x3a   ? 2
                                             : ea == 0x3b ? 4
                                                          : 0)
                     : ea >= 8 && ea < 16 ? 2
                                          : 0;
    unsigned next = branch ? 2 + extra : 4 + extra;
    unsigned code = branch ? opcode & 31 : UINT32_MAX;
    if (!branch) {
        mapped_fetch_command(out, pc, failures, &count);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, command), 0);
        FP_BYTES(out, 0x83, 0xf8, 31);
        unsigned valid = jump(out, 0x86);
        fpu_predicate_exception(out, pc, 11, pc, 0, failures, &count);
        patch(out, valid, out->size);
        if (ea > 0x3c) {
            fpu_predicate_exception(out, pc, 11, pc, 0, failures, &count);
            goto finish;
        }
    }
    if (extra) {
        fpu_predicate_fetch(out, pc + (branch ? 2 : 4), extra, failures, &count);
        if (extra == 2)
            FP_BYTES(out, 0x0f, 0xbf, 0xc0);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, displacement), 0);
    }
    fpu_constant(out, offsetof(FPUInvocation, next), pc + next, 0);
    fpu_mapped_signal_condition(out, pc, code, failures, &count);
    fpu_condition_value(out, code);
    if (branch || (ea >= 8 && ea < 16)) {
        FP_BYTES(out, 0x85, 0xc0);
        unsigned finished = jump(out, branch ? 0x84 : 0x85);
        unsigned exhausted = 0;
        if (!branch) {
            unsigned offset = offsetof(CPUState, regs) + (opcode & 7) * sizeof cpu_state.regs[0];
            fpu_cpu(out, 0x8b, 0, offset);
            FP_BYTES(out, 0x66, 0x83, 0xe8, 1);
            fpu_cpu(out, 0x89, 0, offset);
            FP_BYTES(out, 0x66, 0x83, 0xf8, 0xff);
            exhausted = jump(out, 0x84);
        }
        fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, displacement), 0);
        byte(out, 0x05);
        word32(out, pc + (branch ? 2 : 4));
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, next), 0);
        if (branch)
            fpu_constant(out, offsetof(FPUInvocation, exception.trace_flow), 1, 0);
        patch(out, finished, out->size);
        if (exhausted)
            patch(out, exhausted, out->size);
    }
    else if (ea >= 0x3a) {
        FP_BYTES(out, 0x85, 0xc0);
        unsigned not_taken = jump(out, 0x84);
        fpu_predicate_exception(out, pc, 7, pc + next, 2, failures, &count);
        patch(out, not_taken, out->size);
    }
    else {
        FP_BYTES(out, 0xf6, 0xd8); // Boolean -> byte $00/$FF.
        if (ea < 8)
            fpu_cpu(out, 0x88, 0, offsetof(CPUState, regs) + ea * sizeof cpu_state.regs[0]);
        else {
            fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
            fpu_constant(out, offsetof(FPUInvocation, ea.code), ea, 0);
            fpu_constant(out, offsetof(FPUInvocation, ea.size), 1, 0);
            fpu_constant(out, offsetof(FPUInvocation, ea.writing), 1, 0);
            fpu_constant(out, offsetof(FPUInvocation, ea.registers), 1, 0);
            fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_INCOMPLETE, 0);
            fpu_call_packet(out, NATIVE040_EA, offsetof(FPUInvocation, ea), failures, &count);
            fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_EA, 0);
            fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.context.error), 0);
            byte(out, 0);
            failures[count++] = jump(out, 0x85);
            fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.context.pc), 0);
            fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, next), 0);
            fpu_mapped_transfer(out, 0, 1, 1, failures, &count);
        }
    }
    completed = fpu_jump(out);
finish:
    for (unsigned i = 0; i < count; ++i)
        patch(out, failures[i], out->size);
    instruction_end(out, pc, 0);
    byte(out, 0xc3);
    if (completed)
        patch(out, completed, out->size);
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_DONE, 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, next), 0);
    out->decoder_selector = previous_selector;
    return 1;
}

/* Frame instructions operate even when an exceptional frame is pending. */
static int mapped_frame_opcode(unsigned opcode)
{
    return (opcode & ~63u) == 0xf300 || (opcode & ~63u) == 0xf340;
}

static int fpu_mapped_frame(emitter* out, unsigned opcode, uint32 pc)
{
    if (!mapped_frame_opcode(opcode))
        return 0;
    if (out->size + 3072 > sizeof out->bytes)
        return -1;
    unsigned previous_selector = out->decoder_selector;
    out->decoder_selector = 11;
    out->managed = out->dynamic = 1;
    ++out->guards;
    unsigned failures[32], count = 0, completed = 0;
    unsigned ea = opcode & 63, mode = ea >> 3;
    int writing = !(opcode & 0x40);
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_cpu(out, 0xf7, 0, offsetof(CPUState, sr));
    word32(out, SR_SUPERVISOR_BIT);
    unsigned supervisor = jump(out, 0x85);
    fpu_predicate_exception(out, pc, 8, pc, 0, failures, &count);
    patch(out, supervisor, out->size);
    if (mode < 2 || (writing && mode == 3) || (!writing && mode == 4)) {
        fpu_predicate_exception(out, pc, 11, pc, 0, failures, &count);
        goto finish;
    }
    if (writing) {
        // A pre-existing busy frame is an exceptional checked exit before EA.
        fpu_cpu(out, 0x83, 7, offsetof(CPUState, fpu.frame_size));
        byte(out, 0);
        failures[count++] = jump(out, 0x85);
    }
    fpu_constant(out, offsetof(FPUInvocation, ea.context.pc), pc + 2, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.code), !writing && mode == 3 ? ea - 8 : ea, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.size), 4, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.writing), writing, 0);
    fpu_constant(out, offsetof(FPUInvocation, ea.registers), 0, 0);
    fpu_call_packet(out, NATIVE040_EA, offsetof(FPUInvocation, ea), failures, &count);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_EA, 0);
    fpu_packet(out, 0x83, 7, offsetof(FPUInvocation, ea.context.error), 0);
    byte(out, 0);
    failures[count++] = jump(out, 0x85);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, ea.context.pc), 0);
    fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, next), 0);
    // Frame data uses data space even for a PC-relative/immediate EA.
    fpu_constant(out, offsetof(FPUInvocation, ea.result.instruction), 0, 0);
    if (writing) {
        fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_FRAME, 0);
        fpu_cpu(out, 0x83, 7, offsetof(CPUState, fpu.frame_size));
        byte(out, 0);
        failures[count++] = jump(out, 0x85); // An EA callback posted a busy frame.
        fpu_cpu(out, 0x83, 7, offsetof(CPUState, fpu.null_state));
        byte(out, 0);
        FP_BYTES(out, 0x0f, 0x94, 0xc0, 0x0f, 0xb6, 0xc0, 0xc1, 0xe0, 24, 0x6b, 0xc0, 0x41);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, memory.value), 0);
        fpu_mapped_transfer(out, 0, 4, 1, failures, &count);
    }
    else {
        fpu_mapped_read(out, 0, 4, failures, &count);
        fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_FRAME, 0);
        FP_BYTES(out, 0x3d);
        word32(out, 0x41000000);
        unsigned idle = jump(out, 0x84);
        FP_BYTES(out, 0xa9);
        word32(out, 0xff000000);
        failures[count++] = jump(out, 0x85); // Busy or malformed captured header.
        // Exact architectural null reset, including padding and saved frame bytes.
        FP_BYTES(out, 0x57, 0x51, 0x48, 0x8d, 0xb9);
        word32(out, offsetof(CPUState, fpu));
        byte(out, 0xb9);
        word32(out, sizeof cpu_state.fpu);
        FP_BYTES(out, 0x31, 0xc0, 0xfc, 0xf3, 0xaa, 0x59, 0x5f);
        fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.null_state));
        word32(out, 1);
        FP_BYTES(out, 0x48, 0xc7, 0xc2, 0xff, 0xff, 0xff, 0xff);
        for (unsigned i = 0; i < 8; ++i) {
            byte(out, 0x66);
            fpu_cpu(out, 0xc7, 0, offsetof(CPUState, fpu.regs) + i * sizeof(Syn68kFP80));
            byte(out, 0xff);
            byte(out, 0x7f);
            byte(out, 0x48);
            fpu_cpu(out, 0x89, 2, offsetof(CPUState, fpu.regs) + i * sizeof(Syn68kFP80) + 8);
        }
        unsigned reset = fpu_jump(out);
        patch(out, idle, out->size);
        const unsigned cleared[] = {offsetof(CPUState, fpu.null_state), offsetof(CPUState, fpu.frame_size),
                                    offsetof(CPUState, fpu.pending_vector)};
        for (unsigned i = 0; i < sizeof cleared / sizeof *cleared; ++i) {
            fpu_cpu(out, 0xc7, 0, cleared[i]);
            word32(out, 0);
        }
        patch(out, reset, out->size);
        if (mode == 3) {
            fpu_cpu(out, 0x83, 0, offsetof(CPUState, regs) + (8 + (ea & 7)) * sizeof cpu_state.regs[0]);
            byte(out, 4);
        }
    }
    completed = fpu_jump(out);
finish:
    for (unsigned i = 0; i < count; ++i)
        patch(out, failures[i], out->size);
    instruction_end(out, pc, 0);
    byte(out, 0xc3);
    if (completed)
        patch(out, completed, out->size);
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_constant(out, offsetof(FPUInvocation, exit), FPU_EXIT_DONE, 0);
    fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, next), 0);
    out->decoder_selector = previous_selector;
    return 1;
}

static int fpu_conditional(emitter* out, unsigned opcode, uint32 pc)
{
    int branch = (opcode & 0xff80) == 0xf280 && !(opcode & 0x20);
    unsigned ea = opcode & 63;
    int conditional = (opcode & 0xffc0) == 0xf240 && (ea < 16 || (ea >= 0x3a && ea <= 0x3c));
    if (!branch && !conditional)
        return 0;
    if (out->size + 2048 > sizeof out->bytes)
        return -1;
    unsigned previous_selector = out->decoder_selector;
    out->decoder_selector = 11;
    ++out->guards;
    out->dynamic = 1;
    unsigned failures[6], count = 0;
    fpu_cpu(out, 0x83, 7, offsetof(CPUState, fpu.frame_size));
    byte(out, 0);
    failures[count++] = jump(out, 0x85);
    unsigned extra = branch       ? (opcode & 0x40 ? 4 : 2)
                     : ea >= 0x3a ? (ea == 0x3a   ? 2
                                     : ea == 0x3b ? 4
                                                  : 0)
                     : ea >= 8    ? 2
                                  : 0;
    unsigned next = branch ? 2 + extra : 4 + extra;
    unsigned code = branch ? opcode & 31 : UINT32_MAX;
    if (!branch) {
        // Do not fetch a displacement or ignored operand for a reserved condition.
        byte(out, 0xba);
        word32(out, pc + 2);
        memory_guard(out, 2, pc, 1);
        memory_load(out, 1, 0);
        pointer_r8(out, RELOC_FPU_FRAME);
        fpu_packet(out, 0x89, 0, offsetof(FPUInvocation, command), 0);
        FP_BYTES(out, 0x83, 0xf8, 31);
        failures[count++] = jump(out, 0x87);
    }
    if (extra)
        fpu_fetch_value(out, pc, pc + (branch ? 2 : 4), extra, offsetof(FPUInvocation, displacement));
    pointer_r8(out, RELOC_FPU_FRAME);
    fpu_signal_condition(out, code, failures, &count);
    fpu_condition_value(out, code);
    if (branch || ea >= 8) {
        FP_BYTES(out, 0x85, 0xc0);
        if (!branch && ea >= 0x3a)
            failures[count++] = jump(out, 0x85); // A taken FTRAPcc is a checked exception.
        else {
            unsigned finished = jump(out, branch ? 0x84 : 0x85);
            unsigned exhausted = 0;
            if (!branch) {
                load(out, opcode & 7, 0);
                FP_BYTES(out, 0x66, 0x83, 0xe8, 1);
                store_sized(out, opcode & 7, 1);
                FP_BYTES(out, 0x66, 0x83, 0xf8, 0xff);
                exhausted = jump(out, 0x84);
            }
            fpu_packet(out, 0x8b, 0, offsetof(FPUInvocation, displacement), 0);
            byte(out, 0x05);
            word32(out, pc + (branch ? 2 : 4));
            unsigned taken = fpu_jump(out);
            patch(out, finished, out->size);
            if (exhausted)
                patch(out, exhausted, out->size);
            instruction_end(out, pc, next);
            unsigned completed = fpu_jump(out);
            for (unsigned i = 0; i < count; ++i)
                patch(out, failures[i], out->size);
            decoder_tail(out, pc, 11);
            patch(out, taken, out->size);
            patch(out, completed, out->size);
            out->decoder_selector = previous_selector;
            return 1;
        }
    }
    else {
        FP_BYTES(out, 0xf6, 0xd8); // NEG AL: boolean -> $00/$FF, retaining the other Dn bytes.
        store_sized(out, opcode & 7, 0);
    }
    instruction_end(out, pc, next);
    unsigned completed = fpu_jump(out);
    for (unsigned i = 0; i < count; ++i)
        patch(out, failures[i], out->size);
    decoder_tail(out, pc, 11);
    patch(out, completed, out->size);
    out->decoder_selector = previous_selector;
    return 1;
}

#undef FP_BYTES
