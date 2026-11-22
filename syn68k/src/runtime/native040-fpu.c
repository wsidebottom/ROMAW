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

#include "native040.h"

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
    else
        result = fpu_calculate(target, source, operation, native_fpu->precision);
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
