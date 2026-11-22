/*+===================================================================
  File:      fpu_convert.h

  Summary:   Share exact 68040 input classification and integer destination conversions with native templates.

  Classes:   None.

  Functions: Private fpu_single_source, fpu_double_source and fpu_integer_destination definitions.

  Origin:    Extracted from the maintained ARDI Executor 68040 FPU model in fpu.c.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_FPU_CONVERT_H
#define SYN68K_FPU_CONVERT_H
#include "../../../third_party/softfloat/softfloat-compat.h"
#include <limits.h>

typedef struct {
    floatx80 value;
    unsigned unsupported, signaling, source_tag;
} FPUSource;

static FPUSource fpu_single_source(uint32 value)
{
    FPUSource result = {0};
    if (!(value & 0x7f800000) && (value & 0x007fffff)) {
        result.unsupported = 1;
        result.source_tag = 0xa0;
        result.value = (floatx80){(value >> 16) & 0x8000, (uint64)(value & 0x007fffff) << 40};
    }
    else if ((value & 0x7f800000) == 0x7f800000 && (value & 0x007fffff)) {
        result.signaling = !(value & 0x00400000);
        result.value = (floatx80){((value >> 16) & 0x8000) | 0x7fff,
                                  UINT64_C(0x8000000000000000) | (uint64)(value & 0x007fffff) << 40};
    }
    else
        result.value = float32_to_floatx80(value);
    return result;
}

static FPUSource fpu_double_source(uint64 value)
{
    FPUSource result = {0};
    if (!(value & UINT64_C(0x7ff0000000000000)) && (value & UINT64_C(0x000fffffffffffff))) {
        result.unsupported = 1;
        result.source_tag = 0xa0;
        result.value = (floatx80){(value >> 48) & 0x8000, (value & UINT64_C(0x000fffffffffffff)) << 11};
    }
    else if ((value & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
             (value & UINT64_C(0x000fffffffffffff))) {
        result.signaling = !(value & UINT64_C(0x0008000000000000));
        result.value = (floatx80){((value >> 48) & 0x8000) | 0x7fff,
                                  UINT64_C(0x8000000000000000) | (value & UINT64_C(0x000fffffffffffff)) << 11};
    }
    else
        result.value = float64_to_floatx80(value);
    return result;
}

/* width is one of the architectural integer formats: 8, 16 or 32. Retain
   the pre-clamp rounded value needed by deferred OPERR writeback frames. */
static int32 fpu_integer_destination(floatx80 value, unsigned width, floatx80* rounded)
{
    int32 integer = floatx80_to_int32(value);
    *rounded = int32_to_floatx80(integer);
    int32 minimum = width == 8 ? INT8_MIN : width == 16 ? INT16_MIN : INT32_MIN;
    int32 maximum = width == 8 ? INT8_MAX : width == 16 ? INT16_MAX : INT32_MAX;
    if (integer < minimum || integer > maximum || integer == minimum)
        float_exception_flags |= float_flag_invalid;
    if (float_exception_flags & float_flag_invalid) {
        if ((value.high & 0x7fff) == 0x7fff && (value.low << 1))
            integer = value.low >> (64 - width);
        else
            integer = value.high & 0x8000 ? (int32)(UINT32_MAX << (width - 1)) : (int32)(UINT32_MAX >> (33 - width));
    }
    return integer;
}
#endif
