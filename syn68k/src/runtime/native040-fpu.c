/*+===================================================================
  File:      native040-fpu.c

  Summary:   Instantiate closed floating arithmetic and conversion roots with invocation-local SoftFloat state.

  Classes:   None.

  Functions: Build-time native040_fadd/fsub/fmul/fdiv/fsqrt/fmove/fabs/fneg/fsgldiv/fsglmul/fcmp/ftst roots;
             native040_from_i32/from_f32/from_f64 and native040_to_i32/to_i16/to_i8/to_f32/to_f64 roots.

  Origin:    Original ARDI Executor direct 68040 lowering; uses maintained MMU and floating arithmetic.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "native040-control.h"

/* Reserve an invocation base throughout the closed image. Entry trampolines
   save the caller's R12 and establish it before any arithmetic executes. No
   compiled runtime/SoftFloat globals are referenced by these templates. */
register Syn68kNative040FPU* native_fpu __asm__("r12");
#define SYN68K_SOFTFLOAT_CONTEXT 1
#define float_exception_flags (native_fpu->flags)
#define float_rounding_mode (native_fpu->rounding)
#define floatx80_rounding_precision (native_fpu->precision)
#define float_detect_tininess (native_fpu->tininess)
#include "../../third_party/softfloat/softfloat.c"
#include "fpu_arithmetic.h"
#include "fpu_convert.h"

/* The E3 writeback is older than E1. An E1 move-out can retain an
   exception, but its arithmetic and destination writes have completed. */
static void restore_writeback(Syn68kNative040Restore* packet)
{
    packet->applied = 0;
    unsigned size = packet->size;
    if (!packet->cpu || (size != 52 && size != 100))
        return;
    const uint8* image = packet->image;
    unsigned command = image[size - 36] * 256u + image[size - 35];
    unsigned stages = image[size - 28] & 6;
    int moveout = command >> 13 == 3;
    int arithmetic = fpu_restore_arithmetic(command);
    if (!stages || ((stages & 4) && !moveout && !arithmetic) || ((stages & 2) && size != 100) ||
        (size == 100 && image[8] == 0xfe && !moveout))
        return;
    unsigned format = command >> 10 & 7;
    unsigned nonmaskable = 0x1800; // Overflow and underflow.
    if (moveout && (format == 0 || format == 4 || format == 6))
        nonmaskable |= 0x6000; // Integer move-outs also retain SNAN and OPERR.
    Syn68kFPUState* fpu = &packet->cpu->fpu;
    unsigned pending = fpu_exception_vector(fpu->fpsr & (fpu->fpcr | nonmaskable));
    if ((stages & 4) && size == 100 &&
        ((image[0x3c] & 0x80) || (image[0x44] & 0x80) || (command >> 13 == 2 && format == 3) ||
         (moveout && (format == 3 || format == 7))))
        pending = 55; // Unsupported data takes priority over conversion status.
    int wrote_back = 0;
    if (stages & 2) {
        unsigned command3 = image[0x34] * 256u + image[0x35];
        unsigned operation = fpu_writeback_operation(command3);
        if (!fpu_writeback_supported(operation))
            return;
        unsigned older_pending = fpu_exception_vector(fpu->fpsr & (fpu->fpcr | 0x1800) & 0x1a00);
        if (older_pending)
            pending = older_pending;
        else {
            if ((stages & 4) && arithmetic && !pending)
                return; // E1 arithmetic must resume after the older writeback.
            unsigned precision = packet->entry_fpcr >> 6 & 3;
            native_fpu->precision = precision == 1 ? 32 : precision == 2 ? 64 : 80;
            native_fpu->rounding = packet->entry_fpcr >> 4 & 3;
            floatx80 result = fpu_frame_writeback(image, operation, native_fpu->precision);
            unsigned reg = command3 >> 7 & 7;
            fpu->regs[reg].exponent = result.high;
            fpu->regs[reg].significand = result.low;
            stages &= ~2u;
            wrote_back = 1;
        }
    }
    else if ((moveout && pending == 50) ||
             (arithmetic && (pending < 49 || pending > 55)))
        return; // Arithmetic replay and invalid stages retain checked continuations.
    for (unsigned i = 0; i < size; ++i)
        fpu->frame[i] = image[i];
    if (wrote_back)
        fpu->frame[0x48] &= ~2u;
    fpu->frame_size = stages && pending ? size : 0;
    fpu->pending_vector = stages ? pending : 0;
    fpu->null_state = 0;
    packet->applied = 1;
}

static void calculate(unsigned operation)
{
    floatx80 source = {native_fpu->source.high, native_fpu->source.low};
    floatx80 target = {native_fpu->target.high, native_fpu->target.low};
    FPUdivision result = {0};
    if (operation == 0x38 || operation == 0x3a) {
        int source_nan = (source.high & 0x7fff) == 0x7fff && (source.low << 1);
        int target_nan = (target.high & 0x7fff) == 0x7fff && (target.low << 1);
        result.result = source;
        if ((source_nan && !(source.low & UINT64_C(0x4000000000000000))) ||
            (operation == 0x38 && target_nan && !(target.low & UINT64_C(0x4000000000000000))))
            float_exception_flags |= float_flag_invalid;
        if (operation == 0x38) {
            if (source_nan || target_nan)
                result.result = (floatx80){0x7fff, UINT64_MAX};
            else if ((target.high == source.high && (target.low == source.low || (target.high & 0x7fff) == 0x7fff)) ||
                     (!(target.high & 0x7fff) && !(source.high & 0x7fff) && !target.low && !source.low))
                result.result = (floatx80){0, 0};
            else {
                int less;
                if ((target.high ^ source.high) & 0x8000)
                    less = !!(target.high & 0x8000);
                else {
                    less = (target.high & 0x7fff) < (source.high & 0x7fff) ||
                           ((target.high & 0x7fff) == (source.high & 0x7fff) && target.low < source.low);
                    if (target.high & 0x8000)
                        less = !less;
                }
                result.result = (floatx80){less ? 0xbfff : 0x3fff, UINT64_C(0x8000000000000000)};
            }
        }
    }
    else {
        int dyadic = operation == 0x20 || operation == 0x22 || operation == 0x23 || operation == 0x28 ||
                     operation == 0x24 || operation == 0x27;
        int source_special = (source.high & 0x7fff) == 0x7fff;
        int source_nan = source_special && (source.low << 1);
        int target_nan = dyadic && (target.high & 0x7fff) == 0x7fff && (target.low << 1);
        if (source_nan || target_nan) {
            /* The 68040 prefers the destination NaN, keeping its sign and J bit. */
            result.result = target_nan ? target : source;
            result.result.low |= UINT64_C(0x4000000000000000);
            if ((source_nan && floatx80_is_signaling_nan(source)) || (target_nan && floatx80_is_signaling_nan(target)))
                float_exception_flags |= float_flag_invalid;
        }
        else if (!operation && native_fpu->precision == 80 && source_special)
            result.result = source; // Plain FMOVE retains either infinity encoding.
        else
            result = fpu_calculate(target, source, operation, native_fpu->precision);
    }
    native_fpu->result = (Syn68kNative040Float){result.result.high, result.result.low};
    native_fpu->condition = (Syn68kNative040Float){result.condition.high, result.condition.low};
    native_fpu->writeback = (Syn68kNative040Float){result.writeback.high, result.writeback.low};
    native_fpu->status = result.status;
    native_fpu->grs = result.grs;
    native_fpu->sticky = result.sticky;
    native_fpu->exponent15 = result.exponent15;
    native_fpu->finite = result.finite;
}

#if defined(_WIN32)
#define ROOT_ENTER "push %r12\nmov %rcx,%r12\nsub $32,%rsp\n"
#define ROOT_LEAVE "add $32,%rsp\npop %r12\nret\n"
#else
#define ROOT_ENTER "push %r12\nmov %rdi,%r12\n"
#define ROOT_LEAVE "pop %r12\nret\n"
#endif
#define ENTRY(name, expression)                                                     \
    __attribute__((used, noinline)) void native040_##name##_body(void)              \
    {                                                                               \
        expression;                                                                 \
    }                                                                               \
    __asm__(".text\n.p2align 4\n.globl native040_" #name "\nnative040_" #name ":\n" \
            "endbr64\n" ROOT_ENTER "call native040_" #name "_body\n" ROOT_LEAVE);

ENTRY(restore_writeback, restore_writeback((Syn68kNative040Restore*)native_fpu))
#define ROOT(name, operation) ENTRY(name, calculate(operation))

ROOT(fadd, 0x22)
ROOT(fsub, 0x28)
ROOT(fmul, 0x23)
ROOT(fdiv, 0x20)
ROOT(fsqrt, 4)
ROOT(fmove, 0)
ROOT(fabs, 0x18)
ROOT(fneg, 0x1a)
ROOT(fsgldiv, 0x24)
ROOT(fsglmul, 0x27)

ROOT(fcmp, 0x38)
ROOT(ftst, 0x3a)

static void source_result(FPUSource source)
{
    native_fpu->status = 0;
    native_fpu->result = (Syn68kNative040Float){source.value.high, source.value.low};
    native_fpu->unsupported = source.unsupported;
    native_fpu->signaling = source.signaling;
    native_fpu->source_tag = source.source_tag;
}

static void integer_destination(unsigned width)
{
    floatx80 rounded, source = {native_fpu->source.high, native_fpu->source.low};
    native_fpu->status = 0;
    native_fpu->bits = (int64)fpu_integer_destination(source, width, &rounded);
    native_fpu->result = (Syn68kNative040Float){rounded.high, rounded.low};
}

static void floating_destination(unsigned precision)
{
    floatx80 source = {native_fpu->source.high, native_fpu->source.low};
    FPUdivision range = fpu_calculate(source, source, 0, precision);
    floatx80 intermediate = range.status == 0x1000 ? range.result : source;
    native_fpu->status = range.status;
    native_fpu->result = (Syn68kNative040Float){intermediate.high, intermediate.low};
    native_fpu->bits = precision == 32 ? floatx80_to_float32(source) : floatx80_to_float64(source);
}

ENTRY(from_i32, source_result(((FPUSource){.value = int32_to_floatx80((int32)native_fpu->bits)})))
ENTRY(from_f32, source_result(fpu_single_source(native_fpu->bits)))
ENTRY(from_f64, source_result(fpu_double_source(native_fpu->bits)))
ENTRY(to_i32, integer_destination(32))
ENTRY(to_i16, integer_destination(16))
ENTRY(to_i8, integer_destination(8))
ENTRY(to_f32, floating_destination(32))
ENTRY(to_f64, floating_destination(64))
