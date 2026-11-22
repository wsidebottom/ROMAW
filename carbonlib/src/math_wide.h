/*+===================================================================
  File:      math_wide.h

  Summary:   Declares native Math64Lib and classic FixMath wide arithmetic operations.

  Classes:   None.

  Functions: Internal declarations for wide integer and double-double operations.

  Origin:    Math64Lib from Mac OS 9.2.1 and Universal Interfaces 3.4.2 Math64.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MATH_WIDE_H
#define ROMAW_CARBON_MATH_WIDE_H

#include "carbonlib.h"

typedef enum {
    wide_unsigned_max,
    wide_signed_max,
    wide_signed_min,
    wide_add,
    wide_subtract,
    wide_multiply,
    wide_negate,
    wide_absolute,
    wide_signed_divide,
    wide_unsigned_divide,
    wide_sign_extend,
    wide_zero_extend,
    wide_narrow,
    wide_signed_compare,
    wide_unsigned_compare,
    wide_logical_and,
    wide_logical_or,
    wide_logical_xor,
    wide_logical_not,
    wide_bitwise_and,
    wide_bitwise_or,
    wide_bitwise_xor,
    wide_bitwise_not,
    wide_signed_right,
    wide_unsigned_right,
    wide_left,
    wide_reinterpret,
    wide_signed_to_float,
    wide_unsigned_to_float,
    wide_float_to_signed,
    wide_float_to_unsigned
} carbon_wide_operation;

typedef enum {
    fixmath_add,
    fixmath_subtract,
    fixmath_compare,
    fixmath_multiply,
    fixmath_negate,
    fixmath_shift_rounded,
    fixmath_shift_bits,
    fixmath_root,
    fixmath_divide_long,
    fixmath_divide_wide
} carbon_fixmath_wide_operation;

#ifdef __cplusplus
extern "C" {
#endif

int ROMlib_carbon_math_wide(ROMlib_carbon_ppc_frame* call_frame, carbon_wide_operation operation);
int ROMlib_carbon_math_wide_float(ROMlib_carbon_ppc_frame* call_frame, carbon_wide_operation operation);
int ROMlib_carbon_fixmath_wide(ROMlib_carbon_ppc_frame* call_frame, carbon_fixmath_wide_operation operation);

#ifdef __cplusplus
}
#endif

#endif
