/*+===================================================================
  File:      math_wide_entries.h

  Summary:   Maps Math64Lib and classic FixMath wide exports to their native frame operations.

  Classes:   None.

  Functions: Private frame wrappers for the export registry.

  Origin:    Math64Lib from Mac OS 9.2.1 and the CarbonLib 1.4 import table.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MATH_WIDE_ENTRIES_H
#define ROMAW_CARBON_MATH_WIDE_ENTRIES_H

#include "math_wide.h"

#define CARBON_MATH_WIDE(name, operation)                                   \
    static int carbon_math_wide_##name(ROMlib_carbon_ppc_frame* call_frame) \
    {                                                                       \
        return ROMlib_carbon_math_wide(call_frame, wide_##operation);       \
    }

CARBON_MATH_WIDE(LongDoubleToSInt64, float_to_signed)
CARBON_MATH_WIDE(LongDoubleToUInt64, float_to_unsigned)
CARBON_MATH_WIDE(S32Set, narrow)
CARBON_MATH_WIDE(S64Absolute, absolute)
CARBON_MATH_WIDE(S64Add, add)
CARBON_MATH_WIDE(S64And, logical_and)
CARBON_MATH_WIDE(S64BitwiseAnd, bitwise_and)
CARBON_MATH_WIDE(S64BitwiseEor, bitwise_xor)
CARBON_MATH_WIDE(S64BitwiseNot, bitwise_not)
CARBON_MATH_WIDE(S64BitwiseOr, bitwise_or)
CARBON_MATH_WIDE(S64Compare, signed_compare)
CARBON_MATH_WIDE(S64Divide, signed_divide)
CARBON_MATH_WIDE(S64Eor, logical_xor)
CARBON_MATH_WIDE(S64Max, signed_max)
CARBON_MATH_WIDE(S64Min, signed_min)
CARBON_MATH_WIDE(S64Multiply, multiply)
CARBON_MATH_WIDE(S64Negate, negate)
CARBON_MATH_WIDE(S64Not, logical_not)
CARBON_MATH_WIDE(S64Or, logical_or)
CARBON_MATH_WIDE(S64Set, sign_extend)
CARBON_MATH_WIDE(S64SetU, zero_extend)
CARBON_MATH_WIDE(S64ShiftLeft, left)
CARBON_MATH_WIDE(S64ShiftRight, signed_right)
CARBON_MATH_WIDE(S64Subtract, subtract)
CARBON_MATH_WIDE(SInt64ToLongDouble, signed_to_float)
CARBON_MATH_WIDE(SInt64ToUInt64, reinterpret)
CARBON_MATH_WIDE(U32SetU, narrow)
CARBON_MATH_WIDE(U64Add, add)
CARBON_MATH_WIDE(U64And, logical_and)
CARBON_MATH_WIDE(U64BitwiseAnd, bitwise_and)
CARBON_MATH_WIDE(U64BitwiseEor, bitwise_xor)
CARBON_MATH_WIDE(U64BitwiseNot, bitwise_not)
CARBON_MATH_WIDE(U64BitwiseOr, bitwise_or)
CARBON_MATH_WIDE(U64Compare, unsigned_compare)
CARBON_MATH_WIDE(U64Divide, unsigned_divide)
CARBON_MATH_WIDE(U64Eor, logical_xor)
CARBON_MATH_WIDE(U64Max, unsigned_max)
CARBON_MATH_WIDE(U64Multiply, multiply)
CARBON_MATH_WIDE(U64Not, logical_not)
CARBON_MATH_WIDE(U64Or, logical_or)
CARBON_MATH_WIDE(U64Set, sign_extend)
CARBON_MATH_WIDE(U64SetU, zero_extend)
CARBON_MATH_WIDE(U64ShiftLeft, left)
CARBON_MATH_WIDE(U64ShiftRight, unsigned_right)
CARBON_MATH_WIDE(U64Subtract, subtract)
CARBON_MATH_WIDE(UInt64ToLongDouble, unsigned_to_float)
CARBON_MATH_WIDE(UInt64ToSInt64, reinterpret)

#undef CARBON_MATH_WIDE

#define CARBON_FIXMATH_WIDE(name, operation)                                \
    static int carbon_fixmath_##name(ROMlib_carbon_ppc_frame* call_frame)   \
    {                                                                       \
        return ROMlib_carbon_fixmath_wide(call_frame, fixmath_##operation); \
    }

CARBON_FIXMATH_WIDE(WideAdd, add)
CARBON_FIXMATH_WIDE(WideBitShift, shift_bits)
CARBON_FIXMATH_WIDE(WideCompare, compare)
CARBON_FIXMATH_WIDE(WideDivide, divide_long)
CARBON_FIXMATH_WIDE(WideMultiply, multiply)
CARBON_FIXMATH_WIDE(WideNegate, negate)
CARBON_FIXMATH_WIDE(WideShift, shift_rounded)
CARBON_FIXMATH_WIDE(WideSquareRoot, root)
CARBON_FIXMATH_WIDE(WideSubtract, subtract)
CARBON_FIXMATH_WIDE(WideWideDivide, divide_wide)

#undef CARBON_FIXMATH_WIDE
#endif
