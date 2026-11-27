/*+===================================================================
  File:      fpu.c

  Summary:   Implements fpu support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_fpu_finish_frame, syn68k_fpu_execute, syn68k_fpu_finish_captured, syn68k_fpu_finish_command, syn68k_fpu_finish_command_at,
             syn68k_fpu_reset, syn68k_fpu_finish_destination,
             syn68k_fpu_step, syn68k_fpu_step_decoded

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MC68040 floating-point execution. Arithmetic uses software IEEE extended
   precision; neither the host double format nor its floating environment is
   part of the guest state. See docs/68040-status.md for validation limits. */
/* Division scaling/writeback mapping also adapts MAME's BSD-3-Clause code;
   see include/fpu_exception.h and third_party/mame/ for its retained notice. */
#include "syn68k_private.h"
#include "trap.h"
#include "ea.h"
#include "fpu.h"
#include "fpu_exception.h"
#include "fpu_arithmetic.h"
#include "fpu_convert.h"
#include "fpu-captured.h"
#include "../../third_party/softfloat/softfloat-compat.h"
#include <setjmp.h>
#include <string.h>

#define FP_N 0x08000000u
#define FP_Z 0x04000000u
#define FP_I 0x02000000u
#define FP_NAN 0x01000000u
#define FP_BSUN 0x8000u
#define FP_SNAN 0x4000u
#define FP_OPERR 0x2000u
#define FP_OVFL 0x1000u
#define FP_UNFL 0x0800u
#define FP_DZ 0x0400u
#define FP_INEX2 0x0200u

typedef struct {
    uint32 start, pc, fault_address;
    unsigned opcode, signaling, post_instruction, unimplemented, packed, replay;
    unsigned defer_exception, deferred_vector;
    unsigned final_store;
    unsigned replay_store, replay_ea_size, replay_numeric;
    uint32 replay_fpsr;
    unsigned trace_flow;
    uint32 prefetch_address;
    unsigned frame_clear;
    uint32 store_tail;
    unsigned store_tail_valid;
    /* Control and frame tails are read by the next normal loop iteration,
       after the preceding callback. Keep their sources until fault cleanup. */
    const uint32* store_tail_control;
    const uint8* store_tail_frame;
    unsigned store_ready, store_command;
    uint32 store_ea;
    floatx80 store_source, store_intermediate, store_rounded;
    unsigned extra_status, extra_enables, illegal_fline;
    unsigned unsupported, source_tag, restoring;
    unsigned original_null;
    unsigned captured, captured_destination, captured_frame, frame_header, has_entry, frame_checked;
    const uint8* captured_image;
    Syn68kEA captured_ea;
    uint32 entry_fpcr;
    unsigned command, has_command;
    floatx80 captured_source;
    floatx80 restored_source, restored_target;
    int restored_source_exponent, restored_target_exponent;
    uint32 packed_high;
    Syn68kMMU040Memory* memory;
    Syn68kMMU040Access access;
    jmp_buf failure;
} FPUContext;

typedef Syn68kEA FPEA;

void syn68k_fpu_reset(void)
{
    memset(&cpu_state.fpu, 0, sizeof cpu_state.fpu);
    cpu_state.fpu.null_state = 1;
    for (unsigned i = 0; i < 8; ++i) {
        cpu_state.fpu.regs[i].exponent = 0x7fff;
        cpu_state.fpu.regs[i].significand = UINT64_MAX;
    }
}

static void fault(FPUContext* context, unsigned vector)
{
    longjmp(context->failure, vector);
}

static void reserved_instruction(FPUContext* context)
{
    context->illegal_fline = 1;
    cpu_state.fpu.null_state = context->original_null;
    fault(context, 11);
}

static void span(FPUContext* context, uint32 address, unsigned length)
{
    /* Flat multiword stores retain their existing whole-span bounds check.
     Mapped accesses validate each actual transfer and retain partial effects. */
    if (context->memory)
        return;
    for (unsigned i = 0; i < length; ++i)
        if (!syn68k_memory_span(address + i, 1)) {
            context->fault_address = address + i;
            fault(context, 2);
        }
}

static uint32 read_value_space(FPUContext* context, uint32 address, unsigned length, int instruction)
{
    Syn68kEAContext access = {.memory = context->memory};
    uint32 value = instruction ? syn68k_ea_fetch_memory(&access, address, length)
                               : syn68k_ea_read_memory(&access, address, length);
    if (access.error) {
        context->access = access.access;
        context->fault_address = access.fault_address;
        fault(context, access.error);
    }
    return value;
}

static uint32 read_value(FPUContext* context, uint32 address, unsigned length)
{
    return read_value_space(context, address, length, 0);
}

static void write_value(FPUContext* context, uint32 address, unsigned length, uint32 value)
{
    Syn68kEAContext access = {.memory = context->memory};
    syn68k_ea_write_memory(&access, address, length, value);
    if (access.error) {
        context->access = access.access;
        context->fault_address = access.fault_address;
        fault(context, access.error);
    }
    if (!context->memory)
        syn68k_invalidate_checked_write(address, length);
}

static uint32 fetch(FPUContext* context, unsigned length)
{
    uint32 value = read_value_space(context, context->pc, length, 1);
    context->pc += length;
    return value;
}

static FPEA effective_address(FPUContext* context, unsigned code, unsigned size, int writing, int registers)
{
    Syn68kEAContext address = {.pc = context->pc, .memory = context->memory};
    FPEA result = syn68k_effective_address(&address, code, size, writing, registers);
    context->pc = address.pc;
    if (address.error) {
        context->fault_address = address.fault_address;
        context->access = address.access;
        if (address.error == 11)
            reserved_instruction(context);
        if (address.error == 4)
            cpu_state.fpu.null_state = context->original_null;
        fault(context, address.error);
    }
    return result;
}

static uint32 read_ea(FPUContext* context, FPEA ea, unsigned size)
{
    if (ea.reg >= 0) {
        uint32 value = cpu_state.regs[ea.reg].ul.n;
        return size == 1 ? value & 0xff : size == 2 ? value & 0xffff : value;
    }
    return read_value_space(context, ea.address, size, ea.instruction);
}

static void write_ea(FPUContext* context, FPEA ea, unsigned size, uint32 value)
{
    if (ea.reg >= 0) {
        uint32 mask = size == 1 ? 0xff : size == 2 ? 0xffff : UINT32_MAX;
        cpu_state.regs[ea.reg].ul.n = (cpu_state.regs[ea.reg].ul.n & ~mask) | (value & mask);
    }
    else
        write_value(context, ea.address, size, value);
}

static floatx80 get_fp(unsigned reg)
{
    floatx80 value = {cpu_state.fpu.regs[reg].exponent, cpu_state.fpu.regs[reg].significand};
    return value;
}

static void set_fp(unsigned reg, floatx80 value)
{
    cpu_state.fpu.regs[reg].exponent = value.high;
    cpu_state.fpu.regs[reg].significand = value.low;
}

static floatx80 read_extended_space(FPUContext* context, uint32 address, int instruction)
{
    floatx80 value;
    /* The 96-bit memory/immediate representation includes an unused word after
     the exponent. Fetch it too, including its mapping and access checks. */
    value.high = read_value_space(context, address, 4, instruction) >> 16;
    value.low = (uint64)read_value_space(context, address + 4, 4, instruction) << 32;
    value.low |= read_value_space(context, address + 8, 4, instruction);
    return value;
}

static floatx80 read_extended(FPUContext* context, uint32 address)
{
    return read_extended_space(context, address, 0);
}

static void write_extended(FPUContext* context, uint32 address, floatx80 value, unsigned final_store,
                           unsigned allow_tail)
{
    span(context, address, 12);
    write_value(context, address, 2, value.high);
    write_value(context, address + 2, 2, 0);
    if (final_store && allow_tail) {
        context->store_tail = value.low;
        context->store_tail_valid = 1;
    }
    write_value(context, address + 4, 4, value.low >> 32);
    context->store_tail_valid = 0;
    context->final_store = final_store;
    write_value(context, address + 8, 4, value.low);
}

static int is_nan(floatx80 value)
{
    return (value.high & 0x7fff) == 0x7fff && (value.low << 1) != 0;
}

static void check_operand(FPUContext* context, floatx80 value, int exponent)
{
    if ((!exponent && value.low && !(value.low >> 63)) || (exponent && exponent != 0x7fff && !(value.low >> 63)))
        context->unsupported = 1;
    if (exponent == 0x7fff && floatx80_is_signaling_nan(value))
        context->signaling = 1;
}

static void condition_codes(floatx80 value)
{
    uint32 flags = value.high & 0x8000 ? FP_N : 0;
    if (is_nan(value))
        flags |= FP_NAN;
    else if ((value.high & 0x7fff) == 0x7fff)
        flags |= FP_I;
    else if (!(value.high & 0x7fff) && !value.low)
        flags |= FP_Z;
    cpu_state.fpu.fpsr = (cpu_state.fpu.fpsr & 0x00ffffff) | flags;
}

static void exceptions(FPUContext* context)
{
    unsigned flags = float_exception_flags, status = context->extra_status, accrued = 0;
    if (flags & float_flag_invalid) {
        status |= context->signaling ? FP_SNAN : FP_OPERR;
        accrued |= 0x80;
    }
    if (flags & float_flag_divbyzero)
        status |= FP_DZ, accrued |= 0x10;
    if (flags & float_flag_overflow)
        status |= FP_OVFL, accrued |= 0x40;
    if (flags & float_flag_underflow) {
        status |= FP_UNFL;
        if (flags & float_flag_inexact)
            accrued |= 0x20;
    }
    if (flags & float_flag_inexact)
        status |= FP_INEX2, accrued |= 0x08;
    if (status & FP_OVFL)
        accrued |= 0x48;
    if ((status & (FP_UNFL | FP_INEX2)) == (FP_UNFL | FP_INEX2))
        accrued |= 0x20;
    cpu_state.fpu.fpsr = (cpu_state.fpu.fpsr & ~0xff00u) | status | accrued;
    unsigned enabled =
        status & (cpu_state.fpu.fpcr | context->extra_enables | (context->defer_exception ? FP_OVFL | FP_UNFL : 0));
    unsigned vector = fpu_exception_vector(enabled);
    if (context->defer_exception && vector)
        context->deferred_vector = vector;
    else if (vector)
        fault(context, vector);
}

static unsigned format_size(unsigned format)
{
    static const unsigned sizes[] = {4, 4, 12, 12, 2, 8, 1, 12};
    return sizes[format];
}

static floatx80 source_value(FPUContext* context, unsigned format, unsigned ea_code)
{
    if (format == 7)
        reserved_instruction(context); /* Dynamic packed is only a destination. */
    FPEA ea = effective_address(context, ea_code, format_size(format), 0, 1);
    context->fault_address = ea.reg < 0 && ea_code != 0x3c ? ea.address : 0;
    switch (format) {
    case 0:
        return int32_to_floatx80((int32)read_ea(context, ea, 4));
    case 1: {
        FPUSource source = fpu_single_source(read_ea(context, ea, 4));
        context->unsupported |= source.unsupported;
        context->signaling |= source.signaling;
        if (source.source_tag)
            context->source_tag = source.source_tag;
        return source.value;
    }
    case 2:
        return read_extended_space(context, ea.address, ea.instruction);
    case 3: {
        floatx80 value = {0, 0};
        context->packed = 1;
        context->unsupported = 1;
        context->packed_high = read_value_space(context, ea.address, 4, ea.instruction);
        value.low = (uint64)read_value_space(context, ea.address + 4, 4, ea.instruction) << 32;
        value.low |= read_value_space(context, ea.address + 8, 4, ea.instruction);
        return value;
    }
    case 4:
        return int32_to_floatx80((int16)read_ea(context, ea, 2));
    case 5: {
        uint64 value = (uint64)read_value_space(context, ea.address, 4, ea.instruction) << 32;
        value |= read_value_space(context, ea.address + 4, 4, ea.instruction);
        FPUSource source = fpu_double_source(value);
        context->unsupported |= source.unsupported;
        context->signaling |= source.signaling;
        if (source.source_tag)
            context->source_tag = source.source_tag;
        return source.value;
    }
    default:
        return int32_to_floatx80((int8)read_ea(context, ea, 1));
    }
}

static void frame_value(unsigned offset, unsigned length, uint32 value)
{
    for (unsigned i = 0; i < length; ++i)
        cpu_state.fpu.frame[offset + i] = value >> ((length - i - 1) * 8);
}

static void frame_extended(unsigned offset, floatx80 value)
{
    frame_value(offset, 2, value.high);
    frame_value(offset + 4, 4, value.low >> 32);
    frame_value(offset + 8, 4, value.low);
}

static unsigned operand_tag(floatx80 value)
{
    unsigned exponent = value.high & 0x7fff;
    if (!exponent && !value.low)
        return 0x30;
    if (exponent == 0x7fff)
        return is_nan(value) ? 0x60 : 0x40;
    if (!(value.low >> 63))
        return 0x80;
    return exponent <= 0x3fff ? 0x10 : 0;
}

static int operand_frame_exponent(floatx80 value, unsigned tag)
{
    int exponent = value.high & 0x7fff;
    /* FPSP sets ETE15/FPTE15 for ordinary exponents <= $3fff as well
     as normalized denormals. Sign-extend only the wrapped negative
     range: res_func extracts those exponents as signed 15-bit fields. */
    if ((tag & 0x10) && (exponent & 0x4000))
        exponent -= 0x8000;
    return exponent;
}

static void operand_frame(FPUContext* context, unsigned extension, floatx80 source)
{
    /* MC68040 rev $41, section 9.8 / figure 9-10. Offsets are architectural,
     not a serialization of the host structure or its alignment padding. */
    memset(cpu_state.fpu.frame, 0, sizeof cpu_state.fpu.frame);
    cpu_state.fpu.frame_size = 52;
    frame_value(0, 4, 0x41300000);
    frame_value(12, 1, context->source_tag ? context->source_tag : operand_tag(source));
    frame_value(16, 2, extension);
    floatx80 target = context->restoring ? context->restored_target : get_fp((extension >> 7) & 7);
    frame_value(20, 1, operand_tag(target));
    if (context->restoring && context->restored_target_exponent < 0)
        frame_value(20, 1, 0x10);
    frame_value(24, 1, 4); /* E1; no writeback-stage exception. */
    frame_extended(28, target);
    frame_extended(40, source);
    if (context->packed)
        frame_value(36, 4, context->packed_high);
    cpu_state.fpu.fpiar = context->start;
}

static void unimplemented_instruction(FPUContext* context, unsigned extension, floatx80 source)
{
    const Syn68kMMU040Prefetch* boundary = context->memory ? context->memory->prefetch : NULL;
    if (!context->restoring && boundary && context->start == boundary->pc && boundary->page_fault &&
        boundary->page_fault(boundary->opaque, context->pc, &context->access)) {
        context->fault_address = context->access.logical;
        fault(context, 2);
    }
    operand_frame(context, extension, source);
    /* In an unimplemented-instruction frame E1 identifies a packed operand;
     the frame header/command retain the pending instruction without E1. */
    frame_value(24, 1, context->packed ? 4 : 0);
    cpu_state.fpu.pending_vector = 11;
    fault(context, 11);
}

static void writeback_frame(FPUContext* context, unsigned command, floatx80 source, floatx80 destination,
                            FPUdivision division)
{
    memset(cpu_state.fpu.frame, 0, sizeof cpu_state.fpu.frame);
    cpu_state.fpu.frame_size = 100;
    frame_value(0, 4, 0x41600000);
    frame_value(0x28, 4, context->start);
    unsigned command3 = (command & 0x03c3) | ((command & 0x0038) >> 1) | ((command & 4) << 3);
    frame_value(0x34, 2, command3);
    frame_value(0x3c, 1, (context->source_tag ? context->source_tag : operand_tag(source)) | division.grs);
    frame_value(0x3d, 1, division.sticky << 7);
    frame_value(0x40, 2, command);
    frame_value(0x44, 1, operand_tag(destination));
    if (context->restoring && context->restored_target_exponent < 0)
        frame_value(0x44, 1, 0x10);
    frame_value(0x45, 1, division.exponent15 << 4);
    frame_value(0x48, 1, 2); /* E3 writeback exception. */
    frame_extended(0x18, division.writeback);
    frame_extended(0x4c, destination);
    frame_extended(0x58, source);
}

static void destination_exception_frame(FPUContext* context)
{
    FPUdivision empty = {0};
    writeback_frame(context, context->store_command, context->store_source, context->store_intermediate, empty);
    frame_value(0x48, 1, 4);
    frame_value(0x49, 1, 0x10);
    if (context->deferred_vector == 52)
        frame_extended(0x18, context->store_rounded);
    cpu_state.fpu.pending_vector = context->deferred_vector;
}

static void unsupported_operand(FPUContext* context, unsigned command, floatx80 source, floatx80 target)
{
    FPUdivision empty = {0};
    writeback_frame(context, command, source, target, empty);
    frame_value(0x3c, 1, context->source_tag ? context->source_tag : operand_tag(source));
    frame_value(0x48, 1, 4); /* Unsupported types always use a busy E1 frame. */
    if (context->post_instruction)
        frame_value(0x49, 1, 0x10); /* T, bit 20 of the flags longword. */
    if (context->packed)
        frame_value(0x54, 4, context->packed_high);
    cpu_state.fpu.fpiar = context->start;
    cpu_state.fpu.pending_vector = 55;
    fault(context, 55);
}

static int unimplemented_operation(unsigned operation)
{
    return operation < 64 && ((SYN68K_FPU_FPSP_OPERATIONS >> operation) & 1);
}

static void destination_value(FPUContext* context, unsigned extension, unsigned ea_code, floatx80 value)
{
    unsigned format = (extension >> 10) & 7;
    FPEA ea = context->captured_destination ? context->captured_ea
                                            : effective_address(context, ea_code, format_size(format), 1, 1);
    cpu_state.fpu.fpiar = context->start;
    context->post_instruction = 1;
    context->fault_address = ea.reg < 0 ? ea.address : 0;
    if (format == 3 || format == 7)
        unsupported_operand(context, extension, value, int32_to_floatx80(0));
    check_operand(context, value, value.high & 0x7fff);
    if (context->unsupported)
        unsupported_operand(context, extension, value, int32_to_floatx80(0));
    floatx80 original = value, intermediate = value, rounded_integer = {0, 0};
    context->defer_exception = 1;
    uint64 bits = 0;
    switch (format) {
    case 0:
    case 4:
    case 6: {
        int32 integer = fpu_integer_destination(value, format == 6 ? 8 : format == 4 ? 16 : 32, &rounded_integer);
        context->extra_enables = FP_SNAN | FP_OPERR;
        bits = integer;
        break;
    }
    case 1:
    case 5: {
        unsigned precision = format == 1 ? 32 : 64;
        FPUdivision range = fpu_calculate(value, value, 0, precision);
        context->extra_status = range.status;
        intermediate = range.status == FP_OVFL ? range.result : value;
        bits = format == 1 ? floatx80_to_float32(value) : floatx80_to_float64(value);
        break;
    }
    case 2:
        if (context->signaling) {
            value.low |= UINT64_C(0x4000000000000000);
            float_exception_flags |= float_flag_invalid;
        }
        break;
    }
    exceptions(context);
    /* Keep converted operands outside the setjmp frame. Fault cleanup must
       retain the pending exception without repeating EA or bus callbacks. */
    context->store_ready = 1;
    context->store_command = extension;
    context->store_ea = ea.reg < 0 ? ea.address : 0;
    context->store_source = original;
    context->store_intermediate = intermediate;
    context->store_rounded = rounded_integer;
    /* Only extended stores have earlier scalar transfers than the existing
       final/penultimate retirement certificate. An enabled SNAN does not
       store at all; capture status before any physical callback. */
    if (format == 2 && !context->deferred_vector) {
        unsigned mode = ea_code >> 3;
        context->replay_store = context->replay_numeric = 1;
        context->replay_ea_size = mode == 3 || mode == 4 ? 12 : 0;
        context->replay_fpsr = cpu_state.fpu.fpsr;
    }
    if (context->deferred_vector != 54) {
        if (format == 2)
            write_extended(context, ea.address, value, 1, 1);
        else if (format == 5) {
            span(context, ea.address, 8);
            context->store_tail = bits;
            context->store_tail_valid = 1;
            write_value(context, ea.address, 4, bits >> 32);
            context->store_tail_valid = 0;
            context->final_store = 1;
            write_value(context, ea.address + 4, 4, bits);
        }
        else {
            context->final_store = 1;
            write_ea(context, ea, format_size(format), bits);
        }
    }
    if (context->deferred_vector) {
        destination_exception_frame(context);
        fault(context, context->deferred_vector);
    }
}

static int condition(FPUContext* context, unsigned code)
{
    if (code & ~31u)
        reserved_instruction(context);
    unsigned fpsr = cpu_state.fpu.fpsr;
    int n = !!(fpsr & FP_N), z = !!(fpsr & FP_Z), nan = !!(fpsr & FP_NAN);
    if ((code & 16) && nan) {
        cpu_state.fpu.fpsr |= FP_BSUN | 0x80;
        if (cpu_state.fpu.fpcr & FP_BSUN)
            fault(context, 48);
    }
    switch (code & 15) {
    case 0:
        return 0;
    case 1:
        return z;
    case 2:
        return !nan && !z && !n;
    case 3:
        return z || (!nan && !n);
    case 4:
        return n && !nan && !z;
    case 5:
        return z || (n && !nan);
    case 6:
        return !nan && !z;
    case 7:
        return !nan;
    case 8:
        return nan;
    case 9:
        return nan || z;
    case 10:
        return nan || (!n && !z);
    case 11:
        return nan || z || !n;
    case 12:
        return nan || (n && !z);
    case 13:
        return nan || z || n;
    case 14:
        return !z;
    default:
        return 1;
    }
}

static void arithmetic(FPUContext* context, unsigned extension)
{
    unsigned destination = (extension >> 7) & 7;
    unsigned operation = extension & 127;
    unsigned base = operation;
    if (operation == 0x24 || operation == 0x27)
        base = operation == 0x24 ? 0x20 : 0x23;
    if (operation & 0x40) {
        floatx80_rounding_precision = operation & 4 ? 64 : 32;
        switch (operation) {
        case 0x40:
        case 0x44:
            base = 0;
            break;
        case 0x41:
        case 0x45:
            base = 4;
            break;
        case 0x58:
        case 0x5c:
            base = 0x18;
            break;
        case 0x5a:
        case 0x5e:
            base = 0x1a;
            break;
        case 0x60:
        case 0x64:
            base = 0x20;
            break;
        case 0x62:
        case 0x66:
            base = 0x22;
            break;
        case 0x63:
        case 0x67:
            base = 0x23;
            break;
        case 0x68:
        case 0x6c:
            base = 0x28;
            break;
        default:
            reserved_instruction(context);
        }
    }
    context->unimplemented = unimplemented_operation(operation);
    /* Reserved operations must not fetch an operand or change an address
     register. Valid unimplemented operations do fetch it for the FPSP. */
    if (!context->unimplemented)
        switch (base) {
        case 0:
        case 4:
        case 0x18:
        case 0x1a:
        case 0x20:
        case 0x22:
        case 0x23:
        case 0x28:
        case 0x38:
        case 0x3a:
            break;
        default:
            reserved_instruction(context);
        }
    floatx80 source = context->captured    ? context->captured_source
                      : context->restoring ? context->restored_source
                      : extension & 0x4000 ? source_value(context, (extension >> 10) & 7, context->opcode & 63)
                                           : get_fp((extension >> 10) & 7);
    floatx80 target = context->restoring ? context->restored_target : get_fp(destination), result = source;
    FPUdivision division = {0};
    int source_exponent = context->restoring ? context->restored_source_exponent : source.high & 0x7fff;
    int target_exponent = context->restoring ? context->restored_target_exponent : target.high & 0x7fff;
    if (context->unimplemented)
        unimplemented_instruction(context, extension, source);
    context->defer_exception = 1;
    check_operand(context, source, source_exponent);
    if (base == 0x20 || base == 0x22 || base == 0x23 || base == 0x28 || base == 0x38)
        check_operand(context, target, target_exponent);
    if (context->unsupported)
        unsupported_operand(context, operation == 4 ? (extension & ~127u) | 5 : extension, source, target);
    cpu_state.fpu.fpiar = context->start;
    int dyadic = base == 0x20 || base == 0x22 || base == 0x23 || base == 0x28;
    int source_nan = source_exponent == 0x7fff && is_nan(source);
    int target_nan = target_exponent == 0x7fff && is_nan(target);
    if ((base != 0x38 && base != 0x3a) && (source_nan || (dyadic && target_nan))) {
        /* Motorola quiets both operands before choosing the destination NaN.
         SoftFloat's generic propagation can instead choose the source when
         the destination was signaling. Preserve the guest payload/J bit. */
        result = dyadic && target_nan ? target : source;
        result.low |= UINT64_C(0x4000000000000000);
        if (context->signaling)
            float_exception_flags |= float_flag_invalid;
        goto completed;
    }
    switch (base) {
    case 0: /* FMOVE, FSMOVE, FDMOVE */
        if (operation != 0 || context->restoring) {
            division = fpu_calculate_ex(target, source, base, operation ? floatx80_rounding_precision : 80,
                                        target_exponent, source_exponent);
            result = division.result;
        }
        else if (context->signaling) {
            result.low |= UINT64_C(0x4000000000000000);
            float_exception_flags |= float_flag_invalid;
        }
        break;
    case 4:
    case 0x18:
    case 0x1a:
        division =
            fpu_calculate_ex(target, source, base, floatx80_rounding_precision, target_exponent, source_exponent);
        result = division.result;
        break;
    case 0x20:
    case 0x22:
    case 0x23:
    case 0x28:
        division = fpu_calculate_ex(target, source, operation == 0x24 || operation == 0x27 ? operation : base,
                                    floatx80_rounding_precision, target_exponent, source_exponent);
        result = division.result;
        break;
    case 0x38:
        if (source_nan || target_nan) {
            result.high = 0x7fff;
            result.low = UINT64_MAX;
            if (context->signaling)
                float_exception_flags |= float_flag_invalid;
        }
        else if ((target_exponent == source_exponent && target.high == source.high &&
                  (target.low == source.low || target_exponent == 0x7fff)) ||
                 (!(target.high & 0x7fff) && !(source.high & 0x7fff) && !target.low && !source.low))
            result = int32_to_floatx80(0);
        else {
            int less;
            if ((target.high ^ source.high) & 0x8000)
                less = !!(target.high & 0x8000);
            else {
                int te = target_exponent;
                int se = source_exponent;
                less = te < se || (te == se && target.low < source.low);
                if (target.high & 0x8000)
                    less = !less;
            }
            result = int32_to_floatx80(less ? -1 : 1);
        }
        break;
    case 0x3a:
        if (context->signaling)
            float_exception_flags |= float_flag_invalid;
        break;
    default:
        fault(context, 11);
    }
completed:
    context->extra_status = division.status;
    floatx80 condition_result = division.finite ? division.condition : result;
    if (base == 0x3a && source_exponent < 0)
        condition_result.high = (source.high & 0x8000) | 0x3fff;
    condition_codes(condition_result);
    exceptions(context);
    if (context->deferred_vector) {
        if ((base == 4 || base == 0x20 || base == 0x22 || base == 0x23 || base == 0x28) &&
            (context->deferred_vector == 49 || context->deferred_vector == 51 || context->deferred_vector == 53))
            writeback_frame(context, operation == 4 ? (extension & ~127u) | 5 : extension, source, target, division);
        else {
            operand_frame(context, operation == 4 ? (extension & ~127u) | 5 : extension, source);
            if (division.finite &&
                (context->deferred_vector == 49 || context->deferred_vector == 51 || context->deferred_vector == 53))
                frame_extended(28, division.writeback);
        }
        cpu_state.fpu.pending_vector = context->deferred_vector;
        if (context->deferred_vector == 49)
            set_fp(destination, result);
        return;
    }
    if (base != 0x38 && base != 0x3a)
        set_fp(destination, result);
}

static void control_registers(FPUContext* context, unsigned extension)
{
    context->trace_flow = 1;
    unsigned mask = (extension >> 10) & 7;
    int writing = !!(extension & 0x2000);
    unsigned count = !!(mask & 4) + !!(mask & 2) + !!(mask & 1);
    /* An direct is permitted only for FPIAR, not FPCR/FPSR. Reject the
     instruction before effective-address evaluation can change registers.
     Invalid F-line patterns use vector 11/format 0, distinct from FPSP's
     valid but unimplemented instructions (vector 11/format 2). */
    unsigned addressing = (context->opcode >> 3) & 7;
    if (!count || (extension & 0x03ff) || (addressing == 0 && count != 1) || (addressing == 1 && mask != 1) ||
        (addressing == 7 && ((context->opcode & 7) > (writing ? 1 : 4))))
        reserved_instruction(context);
    FPEA ea = effective_address(context, context->opcode & 63, 4 * count, writing, 2);
    context->replay_store = writing && count == 3;
    context->replay_ea_size = addressing == 3 || addressing == 4 ? 4 * count : 0;
    uint32* registers[] = {&cpu_state.fpu.fpcr, &cpu_state.fpu.fpsr, &cpu_state.fpu.fpiar};
    for (unsigned i = 0; i < 3; ++i)
        if (mask & (4 >> i)) {
            if (writing) {
                unsigned remaining = mask & ((4u >> i) - 1);
                context->final_store = !remaining;
                context->store_tail_valid = remaining && !(remaining & (remaining - 1));
                context->store_tail_control = remaining & 2 ? &cpu_state.fpu.fpsr : &cpu_state.fpu.fpiar;
                write_ea(context, ea, 4, *registers[i]);
                context->store_tail_valid = 0;
                context->store_tail_control = NULL;
            }
            else
                *registers[i] = read_ea(context, ea, 4) & (i == 0 ? 0x0000fff0u : i == 1 ? 0x0ffffff8u : UINT32_MAX);
            ea.address += 4;
        }
}

static void multiple_registers(FPUContext* context, unsigned extension)
{
    context->trace_flow = 1;
    int writing = !!(extension & 0x2000);
    unsigned mode = (extension >> 11) & 3;
    unsigned mask = mode & 1 ? EM_DREG((extension >> 4) & 7) & 255 : extension & 255;
    if ((extension & 0x0700) || ((mode & 1) && (extension & 0x008f)))
        reserved_instruction(context);
    unsigned ea_code = context->opcode & 63;
    unsigned addressing = ea_code >> 3;
    int predecrement = addressing == 4;
    if ((predecrement && (!writing || mode >= 2)) || (!predecrement && mode < 2) || (writing && addressing == 3) ||
        (!writing && addressing == 4))
        reserved_instruction(context);
    unsigned count = 0;
    for (unsigned i = 0; i < 8; ++i)
        count += !!(mask & (1u << i));
    FPEA ea = effective_address(context, ea_code, count * 12, writing, 0);
    if (addressing != 2 && addressing != 3 && addressing != 4 && addressing != 5 && addressing != 6 && addressing != 7)
        reserved_instruction(context);
    context->replay_store = writing;
    context->replay_ea_size = predecrement ? count * 12 : 0;
    for (unsigned position = 0; position < 8; ++position) {
        /* EA has already decremented by the entire list size. In ascending
         memory order FP0 precedes FP7 for both transfer directions. */
        unsigned reg = position;
        unsigned bit = predecrement ? reg : 7 - reg;
        if (!(mask & (1u << bit)))
            continue;
        --count;
        if (writing)
            write_extended(context, ea.address, get_fp(reg), !count, 1);
        else
            set_fp(reg, read_extended(context, ea.address));
        ea.address += 12;
    }
}

/* Interpret the already acquired frame image without another guest access. */
static uint32 frame_read_u32(const uint8* bytes)
{
    return (uint32)bytes[0] << 24 | (uint32)bytes[1] << 16 | (uint32)bytes[2] << 8 | bytes[3];
}

static floatx80 frame_read_extended(const uint8* bytes)
{
    return (floatx80){.high = frame_read_u32(bytes) >> 16,
                      .low = (uint64)frame_read_u32(bytes + 4) << 32 | frame_read_u32(bytes + 8)};
}

/* Captured frame exits retain the evaluated EA and initial restore header. */
static void state_frame(FPUContext* context)
{
    context->trace_flow = 1;
    unsigned opcode = context->opcode;
    if (!context->captured_frame && !(cpu_state.sr & SR_SUPERVISOR_BIT))
        fault(context, 8);
    int writing = !(opcode & 0x40);
    unsigned addressing = (opcode >> 3) & 7;
    if (!context->captured_frame && !syn68k_fpu_frame_ea_valid(opcode))
        reserved_instruction(context);
    /* FPSP's gen_except synthesizes busy frames to post software E1
     exceptions. Once posted, the conversion pipeline saves the short
     frame consumed by fpsp_snan/operr, not the original busy wrapper. */
    unsigned frame_offset = !context->captured_frame && writing && cpu_state.fpu.frame_size == 100 &&
                                    (cpu_state.fpu.frame[0x48] & 7) == 5 && (cpu_state.fpu.frame[0x49] & 0x80) &&
                                    (cpu_state.fpu.pending_vector == 50 || cpu_state.fpu.pending_vector == 52 ||
                                     cpu_state.fpu.pending_vector == 54)
                                ? 48
                                : 0;
    unsigned size =
        !context->captured_frame && writing && cpu_state.fpu.frame_size ? cpu_state.fpu.frame_size - frame_offset : 4;
    unsigned ea_code = opcode & 63;
    /* FRESTORE learns the transfer size from its header. Commit a
     postincrement only after the complete frame has been validated. */
    FPEA ea = context->captured_frame
                  ? context->captured_ea
                  : effective_address(context, !writing && addressing == 3 ? ea_code - 8 : ea_code, size, writing, 0);
    if (writing) {
        context->replay_store = !context->captured_frame;
        context->replay_ea_size = addressing == 4 ? size : 0;
        span(context, ea.address, size);
        if (cpu_state.fpu.frame_size) {
            context->frame_clear = 1;
            for (unsigned i = 0; i < size; i += 4) {
                uint32 value = 0;
                for (unsigned j = 0; j < 4; ++j)
                    value = (value << 8) | cpu_state.fpu.frame[frame_offset + i + j];
                if (frame_offset && i < 4)
                    value = 0x41300000;
                context->final_store = i + 4 == size;
                context->store_tail_valid = i + 8 == size;
                context->store_tail_frame =
                    context->store_tail_valid ? cpu_state.fpu.frame + frame_offset + i + 4 : NULL;
                write_value(context, ea.address + i, 4, value);
                context->store_tail_valid = 0;
                context->store_tail_frame = NULL;
            }
            cpu_state.fpu.frame_size = 0;
            cpu_state.fpu.pending_vector = 0;
        }
        else {
            context->final_store = 1;
            write_value(context, ea.address, 4, cpu_state.fpu.null_state ? 0 : 0x41000000);
        }
    }
    else {
        uint32 frame = context->captured_frame ? context->frame_header : read_value(context, ea.address, 4);
        if (!(frame >> 24))
            syn68k_fpu_reset();
        else if (frame == 0x41000000) {
            cpu_state.fpu.null_state = 0;
            cpu_state.fpu.frame_size = 0;
            cpu_state.fpu.pending_vector = 0;
        }
        else if (frame == 0x41300000 || frame == 0x41600000) {
            uint8 restored[100];
            size = frame == 0x41600000 ? 100 : 52;
            if (context->captured_image)
                memcpy(restored, context->captured_image, size);
            else {
                span(context, ea.address, size);
                for (unsigned i = 0; i < 4; ++i)
                    restored[i] = frame >> ((3 - i) * 8);
                for (unsigned i = 4; i < size; i += 4) {
                    uint32 value = read_value(context, ea.address + i, 4);
                    for (unsigned j = 0; j < 4; ++j)
                        restored[i + j] = value >> ((3 - j) * 8);
                }
            }
            unsigned command_offset = size - 36, flags_offset = size - 28;
            unsigned command = restored[command_offset] * 256 + restored[command_offset + 1];
            unsigned stages = restored[flags_offset] & 6;
            if (size == 52 && (stages & 2))
                fault(context, 14); /* An E3 stage requires WBTEMP. */
            unsigned format = (command >> 10) & 7;
            int move_out = command >> 13 == 3;
            unsigned nonmaskable = FP_UNFL | FP_OVFL;
            if (move_out && (format == 0 || format == 4 || format == 6))
                nonmaskable |= FP_SNAN | FP_OPERR;
            int recognized = (command >> 13 == 0 || command >> 13 == 2) &&
                             (unimplemented_operation(command & 127) || (command & 0xfc00) == 0x5c00);
            /* gen_except can synthesize an E1/E3 busy frame after
             completing an FPSP instruction. S/X mark the software
             exception; its command need not be a hardware operation. */
            int software_completed = recognized && (restored[flags_offset] & 1) && (restored[flags_offset + 1] & 0x80);
            int software_instruction = size == 52 && recognized && !software_completed;
            unsigned pending = fpu_exception_vector(cpu_state.fpu.fpsr & (cpu_state.fpu.fpcr | nonmaskable));
            if (move_out && stages == 4 && pending != 50)
                recognized = 1;
            int unsupported = size == 100 && (stages & 4) &&
                              ((restored[0x3c] & 0x80) || (restored[0x44] & 0x80) ||
                               (command >> 13 == 2 && ((command >> 10) & 7) == 3) ||
                               (command >> 13 == 3 && (((command >> 10) & 7) == 3 || ((command >> 10) & 7) == 7)));
            if (unsupported)
                pending = 55, recognized = 1;
            unsigned operation = command & 127;
            int valid_arithmetic = fpu_restore_arithmetic(command);
            if (!recognized && valid_arithmetic &&
                ((stages == 4 && (pending == 0 || pending == 49 || pending == 50 || pending == 51 || pending == 52 ||
                                  pending == 53 || pending == 54)) ||
                 (size == 100 && stages == 2 && (pending == 0 || pending == 49 || pending == 51 || pending == 53))))
                recognized = 1;
            int writeback = size == 100 && (stages & 2);
            unsigned command3 = 0, writeback_operation = 0;
            if (writeback) {
                command3 = restored[0x34] * 256 + restored[0x35];
                writeback_operation = fpu_writeback_operation(command3);
                recognized = fpu_writeback_supported(writeback_operation) &&
                             (!(stages & 4) || recognized || valid_arithmetic || move_out);
                /* The writeback pipeline is older than the conversion
                 pipeline; E3 wins even over an E1 signaling NaN. */
                pending = fpu_exception_vector(cpu_state.fpu.fpsr & (cpu_state.fpu.fpcr | FP_OVFL | FP_UNFL) &
                                               (FP_OVFL | FP_UNFL | FP_INEX2));
                if (software_completed && pending)
                    recognized = 1;
            }
            if (stages && !recognized)
                fault(context, 14); /* Arithmetic restart is not ready yet. */
            memcpy(cpu_state.fpu.frame, restored, size);
            cpu_state.fpu.frame_size = stages || software_instruction ? size : 0;
            cpu_state.fpu.pending_vector =
                software_instruction ? 11
                : stages ? writeback || unsupported || valid_arithmetic || move_out || software_completed ? pending : 11
                         : 0;
            cpu_state.fpu.null_state = 0;
            if (writeback && !pending) {
                set_fp((command3 >> 7) & 7,
                       fpu_frame_writeback(restored, writeback_operation, floatx80_rounding_precision));
                stages &= ~2u;
                cpu_state.fpu.frame[0x48] &= ~2u;
                pending = unsupported ? 55
                          : (stages && !valid_arithmetic && !move_out)
                              ? 11
                              : fpu_exception_vector(cpu_state.fpu.fpsr & (cpu_state.fpu.fpcr | nonmaskable));
                cpu_state.fpu.frame_size = stages ? size : 0;
                cpu_state.fpu.pending_vector = stages ? pending : 0;
            }
            /* Move-out and software-completed stages need no arithmetic replay.
               Retire them only after any older writeback has resolved. */
            if ((move_out || software_completed) && !pending)
                cpu_state.fpu.frame_size = 0;
            if (((stages & 4) || (size == 100 && restored[8] == 0xfe)) && valid_arithmetic && !pending) {
                context->restoring = 1;
                context->restored_source = frame_read_extended(restored + size - 12);
                context->restored_target = frame_read_extended(restored + size - 24);
                context->restored_source_exponent =
                    operand_frame_exponent(context->restored_source, restored[size - 40]);
                context->restored_target_exponent =
                    operand_frame_exponent(context->restored_target, restored[size - 32]);
                context->source_tag = restored[size - 40] & 0xf0;
                context->start = size == 100 ? frame_read_u32(restored + 0x28) : cpu_state.fpu.fpiar;
                cpu_state.fpu.frame_size = 0;
                arithmetic(context, operation == 5 ? (command & ~127u) | 4 : command);
            }
        }
        else
            fault(context, 14);
        if (addressing == 3)
            EM_AREG(opcode & 7) += size;
    }
}

static void execute(FPUContext* context)
{
    unsigned opcode = context->opcode;
    if ((opcode & 0xffc0) == 0xf300 || (opcode & 0xffc0) == 0xf340) {
        state_frame(context);
        return;
    }
    if (cpu_state.fpu.frame_size && !(context->has_command && context->frame_checked)) {
        context->replay = 1;
        fault(context, cpu_state.fpu.pending_vector);
    }
    if ((opcode & 0xff80) == 0xf280) {
        if (opcode & 0x20)
            reserved_instruction(context);
        uint32 base = context->pc;
        int32 displacement = opcode & 0x40 ? (int32)fetch(context, 4) : (int16)fetch(context, 2);
        context->prefetch_address = base + displacement;
        if (condition(context, opcode & 63)) {
            context->trace_flow = 1;
            context->pc = base + displacement;
        }
        return;
    }
    if ((opcode & 0xffc0) != 0xf200 && (opcode & 0xffc0) != 0xf240)
        reserved_instruction(context);
    if ((opcode & 0xfff8) == 0xf248)
        context->trace_flow = 1;
    unsigned extension;
    if (context->has_command) {
        extension = context->command;
        context->pc += 2;
    }
    else
        extension = fetch(context, 2);
    if ((opcode & 0xffc0) == 0xf240) {
        if (extension & ~31u)
            reserved_instruction(context);
        unsigned ea = opcode & 63;
        if ((ea >> 3) == 1) /* FDBcc */
        {
            uint32 base = context->pc;
            int16 displacement = fetch(context, 2);
            context->prefetch_address = base + displacement;
            if (!condition(context, extension)) {
                unsigned reg = ea & 7;
                uint16 counter = EM_DREG(reg) - 1;
                EM_DREG(reg) = (EM_DREG(reg) & 0xffff0000u) | counter;
                if (counter != 0xffff)
                    context->pc = base + displacement;
            }
        }
        else if (ea >= 0x3a) /* FTRAPcc */
        {
            if (ea == 0x3a)
                fetch(context, 2);
            else if (ea == 0x3b)
                fetch(context, 4);
            else if (ea != 0x3c)
                reserved_instruction(context);
            if (condition(context, extension))
                fault(context, 7);
        }
        else {
            int yes = condition(context, extension);
            FPEA destination = effective_address(context, ea, 1, 1, 1);
            context->final_store = 1;
            write_ea(context, destination, 1, yes ? 0xff : 0);
        }
        return;
    }
    cpu_state.fpu.null_state = 0;
    switch (extension >> 13) {
    case 0:
    case 2:
        if ((extension & 0xfc00) == 0x5c00) /* FMOVECR requires FPSP. */
        {
            if (opcode & 63)
                reserved_instruction(context);
            context->unimplemented = 1;
            unimplemented_instruction(context, extension, int32_to_floatx80(0));
        }
        if (!(extension & 0x4000) && (opcode & 63))
            reserved_instruction(context);
        arithmetic(context, extension);
        break;
    case 3: {
        unsigned format = (extension >> 10) & 7;
        if (format != 3 && format != 7 && (extension & 127))
            reserved_instruction(context);
        floatx80 source = get_fp((extension >> 7) & 7);
        destination_value(context, extension, opcode & 63, source);
        break;
    }
    case 4:
    case 5:
        control_registers(context, extension);
        break;
    case 6:
    case 7:
        multiple_registers(context, extension);
        break;
    default:
        reserved_instruction(context);
    }
}

static void step_result(FPUContext* context, unsigned exception, Syn68kFPUStep* result)
{
    result->trace_flow = context->trace_flow;
    result->prefetch_address = context->prefetch_address;
    if (!exception) {
        result->next = context->pc;
        return;
    }
    result->vector = exception;
    result->exception_pc =
        exception == 7 || ((exception == 11 || exception == 55) && !context->replay && !context->illegal_fline) ||
                (context->post_instruction && exception >= 48 && exception <= 55)
            ? context->pc
            : context->start;
    result->fault_address = context->fault_address;
    result->format = exception == 2                                                                       ? 7
                     : context->post_instruction && exception >= 48 && exception <= 55                    ? 3
                     : (exception == 11 && !context->replay && !context->illegal_fline) || exception == 7 ? 2
                                                                                                          : 0;
    result->access = context->access;
    if (exception == 2 && context->memory && context->store_ready && context->deferred_vector)
        destination_exception_frame(context);
    if (exception == 2 && context->memory && (context->final_store || context->store_tail_valid) &&
        ((context->opcode & ~63u) == 0xf200 || (context->opcode & ~63u) == 0xf240 ||
         (context->opcode & ~63u) == 0xf300)) {
        syn68k_fpu_final_store_fault(result, context->pc);
        if (result->store_state)
            result->store_frame_clear = context->frame_clear;
        if (result->store_state && context->store_tail_valid) {
            if (result->access.size != 4)
                result->store_state = 0;
            else {
                result->store_tail_valid = 1;
                result->store_tail = context->store_tail;
                if (context->store_tail_control)
                    result->store_tail = *context->store_tail_control;
                else if (context->store_tail_frame) {
                    result->store_tail = 0;
                    for (unsigned i = 0; i < 4; ++i)
                        result->store_tail = result->store_tail << 8 | context->store_tail_frame[i];
                }
                result->store_tail_fc = (cpu_state.sr >> 11 & 4) | 1;
            }
        }
        if (result->store_state && context->deferred_vector) {
            result->post_vector = context->deferred_vector;
            result->post_ea = context->store_ea;
        }
    }
    if (exception == 2 && context->memory && context->replay_store) {
        syn68k_fpu_replay_store_fault(result);
        if (result->store_state == 2) {
            result->store_ea_size = context->replay_ea_size;
            if (context->replay_numeric) {
                result->store_state = 3;
                result->replay_fpsr = context->replay_fpsr;
            }
        }
    }
}

static void rounding_context(uint32 fpcr)
{
    float_exception_flags = 0;
    float_rounding_mode = (fpcr >> 4) & 3;
    unsigned precision = (fpcr >> 6) & 3;
    floatx80_rounding_precision = precision == 1 ? 32 : precision == 2 ? 64 : 80;
}

/* The mutable context belongs to the caller, outside the function containing
   setjmp. Its metadata remains defined after longjmp, and a nested callback
   cannot overwrite an outer instruction's context or jump buffer. */
static int run_context(FPUContext* context, Syn68kFPUStep* result, int decoded, unsigned opcode, unsigned command)
{
    int exception = setjmp(context->failure);
    if (!exception) {
        if (context->captured_frame) {
            rounding_context(context->entry_fpcr);
            state_frame(context);
        }
        else if (context->captured_destination) {
            rounding_context(context->has_entry ? context->entry_fpcr : cpu_state.fpu.fpcr);
            destination_value(context, command, context->opcode & 63, context->captured_source);
        }
        else if (context->captured) {
            rounding_context(context->has_entry ? context->entry_fpcr : cpu_state.fpu.fpcr);
            arithmetic(context, command);
        }
        else {
            if (decoded) {
                context->opcode = opcode;
                context->pc += 2;
            }
            else
                context->opcode = fetch(context, 2);
            if ((context->opcode & 0xfe00) != 0xf200)
                return 0;
            rounding_context(context->has_entry ? context->entry_fpcr : cpu_state.fpu.fpcr);
            execute(context);
        }
    }
    step_result(context, exception, result);
    return 1;
}

static int guarded_context(FPUContext* context, Syn68kFPUStep* result, int decoded, unsigned opcode, unsigned command)
{
    const int8 rounding = float_rounding_mode, precision = floatx80_rounding_precision;
    const int8 flags = float_exception_flags, tininess = float_detect_tininess;
    int handled = run_context(context, result, decoded, opcode, command);
    float_rounding_mode = rounding;
    floatx80_rounding_precision = precision;
    float_exception_flags = flags;
    float_detect_tininess = tininess;
    return handled;
}

static int step_common(uint32_t pc, Syn68kMMU040Memory* memory, Syn68kFPUStep* result, int decoded, unsigned opcode,
                       unsigned command)
{
    if (!result)
        return 0;
    memset(result, 0, sizeof *result);
    result->next = pc;
    if (!memory && !syn68k_memory_span(pc, 2))
        return 0;
    FPUContext context = {.start = pc,
                          .pc = pc,
                          .memory = memory,
                          .original_null = cpu_state.fpu.null_state,
                          .has_command = decoded == 2,
                          .command = command};
    return guarded_context(&context, result, decoded, opcode, command);
}

int syn68k_fpu_finish_frame(const Syn68kFPUFrame* captured, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    if (!captured || (memory && !memory->mmu) || !result || (captured->pc & 1) || (captured->next & 1) ||
        captured->next - captured->pc < 2 || captured->next - captured->pc > 12 ||
        ((captured->opcode & ~63u) != 0xf300 && (captured->opcode & ~63u) != 0xf340) ||
        (captured->entry.original_null > 1 || captured->entry.frame_checked > 1))
        return 0;
    int writing = !(captured->opcode & 0x40);
    if (!syn68k_fpu_frame_ea_valid(captured->opcode))
        return 0;
    if (captured->image) {
        unsigned size = captured->header == 0x41600000 ? 100 : captured->header == 0x41300000 ? 52 : 0;
        if (writing || !size || captured->image_size != size || frame_read_u32(captured->image) != captured->header)
            return 0;
    }
    else if (captured->image_size)
        return 0;
    FPUContext context = {.start = captured->pc,
                          .pc = captured->next,
                          .opcode = captured->opcode,
                          .memory = memory,
                          .original_null = captured->entry.original_null,
                          .entry_fpcr = captured->entry.fpcr,
                          .has_entry = 1,
                          .captured_frame = 1,
                          .frame_header = captured->header,
                          .captured_image = captured->image,
                          .captured_ea = {.address = captured->ea}};
    memset(result, 0, sizeof *result);
    result->next = captured->pc;
    return guarded_context(&context, result, 1, captured->opcode, 0);
}

int syn68k_fpu_finish_captured(const Syn68kFPUCaptured* captured, Syn68kFPUStep* result)
{
    if (!captured || !result ||
        (cpu_state.fpu.frame_size && !(captured->has_entry == 1 && captured->entry.frame_checked == 1)) ||
        (captured->pc & 1) || (captured->next & 1) || captured->next - captured->pc < 4 ||
        captured->next - captured->pc > 16 || (captured->opcode & ~63u) != 0xf200 || captured->command >> 13 != 2 ||
        captured->original_null > 1 || captured->unsupported > 1 || captured->signaling > 1 ||
        captured->has_entry > 1 ||
        (captured->has_entry &&
         (captured->entry.original_null != captured->original_null || captured->entry.frame_checked > 1)) ||
        (captured->source_tag != 0 && captured->source_tag != 0xa0) || captured->access.bus_error ||
        captured->access.translation.fault != MMU040_OK || captured->access.writing || captured->access.rmw ||
        (captured->access.size != 1 && captured->access.size != 2 && captured->access.size != 4) ||
        captured->access.completed != captured->access.size ||
        (captured->access.fc != 1 && captured->access.fc != 2 && captured->access.fc != 5 && captured->access.fc != 6))
        return 0;
    unsigned format = (captured->command >> 10) & 7, mode = (captured->opcode >> 3) & 7;
    if (format == 3 || format == 7 || mode == 1 || (mode == 7 && (captured->opcode & 7) > 4) ||
        (mode == 0 && (format == 2 || format == 5)))
        return 0;
    const unsigned operations[] = {0,    4,    0x18, 0x1a, 0x20, 0x22, 0x23, 0x28, 0x24, 0x27, 0x38, 0x3a, 0x40, 0x44,
                                   0x41, 0x45, 0x58, 0x5c, 0x5a, 0x5e, 0x60, 0x64, 0x62, 0x66, 0x63, 0x67, 0x68, 0x6c};
    unsigned valid = 0;
    for (unsigned i = 0; i < sizeof operations / sizeof *operations; ++i)
        valid |= (captured->command & 127) == operations[i];
    if (!valid)
        return 0;
    memset(result, 0, sizeof *result);
    result->next = captured->pc;
    FPUContext storage = {0};
    FPUContext* context = &storage;
    context->start = captured->pc;
    context->pc = captured->next;
    context->opcode = captured->opcode;
    context->fault_address = captured->ea;
    context->original_null = captured->original_null;
    context->captured = 1;
    context->has_entry = captured->has_entry;
    context->entry_fpcr = captured->entry.fpcr;
    context->captured_source = (floatx80){captured->source.high, captured->source.low};
    context->unsupported = captured->unsupported;
    context->signaling = captured->signaling;
    context->source_tag = captured->source_tag;
    /* The completed access certifies capture, not an access-error exception.
       The ordinary FPU model publishes access metadata only for failed reads. */
    return guarded_context(context, result, 0, 0, captured->command);
}

int syn68k_fpu_finish_destination(const Syn68kFPUCaptured* captured, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    if (!captured || !result || (memory && !memory->mmu) ||
        (cpu_state.fpu.frame_size && !(captured->has_entry == 1 && captured->entry.frame_checked == 1)) ||
        (captured->pc & 1) || (captured->next & 1) || captured->next - captured->pc < 4 ||
        captured->next - captured->pc > 16 || (captured->opcode & ~63u) != 0xf200 || captured->command >> 13 != 3 ||
        (captured->command & 127) || captured->original_null > 1 || captured->unsupported || captured->signaling ||
        captured->source_tag || captured->has_entry > 1 ||
        (captured->has_entry &&
         (captured->entry.original_null != captured->original_null || captured->entry.frame_checked > 1)))
        return 0;
    unsigned format = captured->command >> 10 & 7, ea = captured->opcode & 63;
    if (format == 3 || format == 7 || ea < 16 || ea > 57)
        return 0;
    FPUContext context = {.start = captured->pc,
                          .pc = captured->next,
                          .fault_address = captured->ea,
                          .opcode = captured->opcode,
                          .original_null = captured->original_null,
                          .captured_destination = 1,
                          .captured_ea = {.address = captured->ea, .reg = -1},
                          .has_entry = captured->has_entry,
                          .entry_fpcr = captured->entry.fpcr,
                          .captured_source = {captured->source.high, captured->source.low},
                          .memory = memory};
    memset(result, 0, sizeof *result);
    result->next = captured->pc;
    return guarded_context(&context, result, 0, 0, captured->command);
}

int syn68k_fpu_step(uint32_t pc, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    return step_common(pc, memory, result, 0, 0, 0);
}

int syn68k_fpu_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    return step_common(pc, memory, result, 1, opcode, 0);
}

int syn68k_fpu_finish_command(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* memory,
                              Syn68kFPUStep* result)
{
    if ((opcode & ~63u) != 0xf200 || !memory)
        return 0;
    return syn68k_fpu_finish_command_at(pc, opcode, command, memory, NULL, result);
}

int syn68k_fpu_finish_command_at(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* memory,
                                 const Syn68kFPUEntry* entry, Syn68kFPUStep* result)
{
    if ((pc & 1) || ((opcode & ~63u) != 0xf200 && (opcode & ~63u) != 0xf240) || command > 65535 ||
        (memory ? !memory->mmu : (!entry || entry->frame_checked != 1)) ||
        (cpu_state.fpu.frame_size && (!entry || !entry->frame_checked)) || !result ||
        (entry && (entry->original_null > 1 || entry->frame_checked > 1)))
        return 0;
    FPUContext context = {.start = pc,
                          .pc = pc,
                          .memory = memory,
                          .original_null = entry ? entry->original_null : cpu_state.fpu.null_state,
                          .has_command = 1,
                          .command = command,
                          .has_entry = entry != NULL,
                          .entry_fpcr = entry ? entry->fpcr : 0};
    memset(result, 0, sizeof *result);
    result->next = pc;
    context.frame_checked = entry ? entry->frame_checked : 0;
    return guarded_context(&context, result, 2, opcode, command);
}

int syn68k_fpu_execute(syn68k_addr_t pc, syn68k_addr_t* next)
{
    uint16 entry_sr = cpu_state.sr;
    Syn68kFPUStep result;
    if (!syn68k_fpu_step(pc, NULL, &result))
        return 0;
    syn68k_fpu_trace_result(&result, entry_sr, pc);
    *next = result.vector ? trap_exception(result.vector, result.exception_pc, result.fault_address, result.format)
                          : result.next;
    return 1;
}
