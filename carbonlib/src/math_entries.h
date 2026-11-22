/*+===================================================================
  File:      math_entries.h

  Summary:   Specialize the MathLib scalar and environment functions for the CarbonLib registry.

  Classes:   None.

  Functions: Private frame entry points for the MathLib export table.

  Origin:    MathLib 9.2.1 PowerPC exports and the CarbonLib 1.4 import table.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MATH_ENTRIES_H
#define ROMAW_CARBON_MATH_ENTRIES_H

#include "math_native.h"
#include <math.h>
#include <quadmath.h>

#define CARBON_MATH_ENTRY(name, ...)                                         \
    static int carbon_math_entry_##name(ROMlib_carbon_ppc_frame* call_frame) \
    {                                                                        \
        return __VA_ARGS__;                                                  \
    }

#define CARBON_MATH_UNARY(name, function) CARBON_MATH_ENTRY(name, ROMlib_carbon_math_unary(call_frame, function))
#define CARBON_MATH_BINARY(name, function) CARBON_MATH_ENTRY(name, ROMlib_carbon_math_binary(call_frame, function))
#define CARBON_MATH_CLASS(name, operation, single) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_classify(call_frame, carbon_math_##operation, single))
#define CARBON_MATH_BITS(name, operation) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_bits(call_frame, carbon_math_##operation))
#define CARBON_MATH_AUX(name, operation) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_aux(call_frame, carbon_math_##operation))
#define CARBON_MATH_ENV(name, operation) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_env(call_frame, carbon_math_##operation))

#define CARBON_MATH_EXTENDED(name, operation) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_extended(call_frame, carbon_math_##operation))
#define CARBON_MATH_FINANCIAL(name, annuity) CARBON_MATH_ENTRY(name, ROMlib_carbon_math_financial(call_frame, annuity))

#define CARBON_MATH_QUAD_UNARY(name, function) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_quad_unary(call_frame, function))
#define CARBON_MATH_QUAD_BINARY(name, function) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_quad_binary(call_frame, function))
#define CARBON_MATH_NARROW_UNARY(name, function) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_dd_unary(call_frame, function))
#define CARBON_MATH_NARROW_BINARY(name, function) \
    CARBON_MATH_ENTRY(name, ROMlib_carbon_math_dd_binary(call_frame, function))
#define CARBON_MATH_NARROW_AUX(name, operation) CARBON_MATH_ENTRY(name, ROMlib_carbon_math_dd_##operation(call_frame))

CARBON_MATH_UNARY(acos, acos)
CARBON_MATH_UNARY(acosh, acosh)
CARBON_MATH_UNARY(asin, asin)
CARBON_MATH_UNARY(asinh, asinh)
CARBON_MATH_UNARY(atan, atan)
CARBON_MATH_UNARY(atanh, atanh)
CARBON_MATH_UNARY(ceil, ceil)
CARBON_MATH_UNARY(cos, cos)
CARBON_MATH_UNARY(cosh, cosh)
CARBON_MATH_UNARY(erf, erf)
CARBON_MATH_UNARY(erfc, erfc)
CARBON_MATH_UNARY(exp, exp)
CARBON_MATH_UNARY(exp2, exp2)
CARBON_MATH_UNARY(expm1, expm1)
CARBON_MATH_UNARY(floor, floor)
CARBON_MATH_UNARY(lgamma, lgamma)
CARBON_MATH_UNARY(log, log)
CARBON_MATH_UNARY(log10, log10)
CARBON_MATH_UNARY(log1p, log1p)
CARBON_MATH_UNARY(log2, log2)
CARBON_MATH_UNARY(logb, logb)
CARBON_MATH_UNARY(nearbyint, nearbyint)
CARBON_MATH_UNARY(rint, rint)
CARBON_MATH_UNARY(round, round)
CARBON_MATH_UNARY(sin, sin)
CARBON_MATH_UNARY(sinh, sinh)
CARBON_MATH_UNARY(sqrt, sqrt)
CARBON_MATH_UNARY(tan, tan)
CARBON_MATH_UNARY(tanh, tanh)
CARBON_MATH_UNARY(trunc, trunc)
CARBON_MATH_UNARY(gamma, tgamma)
CARBON_MATH_BINARY(atan2, atan2)
CARBON_MATH_BINARY(fdim, fdim)
CARBON_MATH_BINARY(fmax, fmax)
CARBON_MATH_BINARY(fmin, fmin)
CARBON_MATH_BINARY(fmod, fmod)
CARBON_MATH_BINARY(hypot, hypot)
CARBON_MATH_BINARY(pow, pow)
CARBON_MATH_BINARY(remainder, remainder)
CARBON_MATH_BINARY(nextafterd, nextafter)
CARBON_MATH_CLASS(__fpclassify, class, 0)
CARBON_MATH_CLASS(__fpclassifyd, class, 0)
CARBON_MATH_CLASS(__fpclassifyf, class, 1)
CARBON_MATH_CLASS(__isfinite, finite, 0)
CARBON_MATH_CLASS(__isfinited, finite, 0)
CARBON_MATH_CLASS(__isfinitef, finite, 1)
CARBON_MATH_CLASS(__isnan, nan, 0)
CARBON_MATH_CLASS(__isnand, nan, 0)
CARBON_MATH_CLASS(__isnanf, nan, 1)
CARBON_MATH_CLASS(__isnormal, normal, 0)
CARBON_MATH_CLASS(__isnormald, normal, 0)
CARBON_MATH_CLASS(__isnormalf, normal, 1)
CARBON_MATH_CLASS(__signbit, sign, 0)
CARBON_MATH_CLASS(__signbitd, sign, 0)
CARBON_MATH_CLASS(__signbitf, sign, 1)
CARBON_MATH_BITS(fabs, abs)
CARBON_MATH_BITS(fabsl, extended_abs)
CARBON_MATH_BITS(copysign, copy_sign)
CARBON_MATH_BITS(copysignl, extended_copy_sign)
CARBON_MATH_BITS(__inf, infinity)
CARBON_MATH_BITS(relation, relation)
CARBON_MATH_AUX(frexp, frexp)
CARBON_MATH_AUX(ldexp, ldexp)
CARBON_MATH_AUX(scalb, ldexp)
CARBON_MATH_AUX(modf, modf)
CARBON_MATH_AUX(remquo, remquo)
CARBON_MATH_ENV(fegetround, get_round)
CARBON_MATH_ENV(fesetround, set_round)
CARBON_MATH_ENV(fegetenv, get_env)
CARBON_MATH_ENV(fesetenv, set_env)
CARBON_MATH_ENV(feholdexcept, hold_except)
CARBON_MATH_ENV(fetestexcept, test_except)

CARBON_MATH_AUX(modff, modff)
CARBON_MATH_AUX(nextafterf, nextafterf)
CARBON_MATH_AUX(randomx, randomx)
CARBON_MATH_ENV(feclearexcept, clear_except)
CARBON_MATH_ENV(fegetexcept, get_except)
CARBON_MATH_ENV(fesetexcept, set_except)
CARBON_MATH_ENV(feraiseexcept, raise_except)
CARBON_MATH_ENV(feupdateenv, update_env)
CARBON_MATH_EXTENDED(floorl, dd_floor)
CARBON_MATH_EXTENDED(ceill, dd_ceil)
CARBON_MATH_EXTENDED(truncl, dd_trunc)
CARBON_MATH_EXTENDED(roundl, dd_round)
CARBON_MATH_EXTENDED(rintl, dd_rint)
CARBON_MATH_EXTENDED(nearbyintl, dd_nearby)
CARBON_MATH_EXTENDED(sqrtl, dd_sqrt)
CARBON_MATH_EXTENDED(scalbl, dd_scale)
CARBON_MATH_EXTENDED(modfl, dd_modf)
CARBON_MATH_EXTENDED(relationl, dd_relation)
CARBON_MATH_EXTENDED(rinttol, to_long)
CARBON_MATH_EXTENDED(roundtol, round_to_long)
CARBON_MATH_EXTENDED(rinttoll, dd_to_long)
CARBON_MATH_EXTENDED(roundtoll, dd_round_to_long)
CARBON_MATH_EXTENDED(x80tod, x80_to_double)
CARBON_MATH_EXTENDED(dtox80, double_to_x80)
CARBON_MATH_EXTENDED(x80told, x80_to_dd)
CARBON_MATH_EXTENDED(ldtox80, dd_to_x80)
CARBON_MATH_FINANCIAL(compound, 0)
CARBON_MATH_FINANCIAL(annuity, 1)

CARBON_MATH_QUAD_UNARY(acosl, acosq)
CARBON_MATH_QUAD_UNARY(asinl, asinq)
CARBON_MATH_QUAD_UNARY(atanl, atanq)
CARBON_MATH_QUAD_UNARY(cosl, cosq)
CARBON_MATH_QUAD_UNARY(exp2l, exp2q)
CARBON_MATH_QUAD_UNARY(expl, expq)
CARBON_MATH_QUAD_UNARY(expm1l, expm1q)
CARBON_MATH_QUAD_UNARY(log10l, log10q)
CARBON_MATH_QUAD_UNARY(log1pl, log1pq)
CARBON_MATH_QUAD_UNARY(log2l, log2q)
CARBON_MATH_QUAD_UNARY(logbl, logbq)
CARBON_MATH_QUAD_UNARY(logl, logq)
CARBON_MATH_QUAD_UNARY(sinl, sinq)
CARBON_MATH_QUAD_UNARY(tanl, tanq)
CARBON_MATH_QUAD_BINARY(atan2l, atan2q)
CARBON_MATH_QUAD_BINARY(hypotl, hypotq)
CARBON_MATH_QUAD_BINARY(powl, powq)
CARBON_MATH_NARROW_UNARY(acoshl, acosh)
CARBON_MATH_NARROW_UNARY(asinhl, asinh)
CARBON_MATH_NARROW_UNARY(atanhl, atanh)
CARBON_MATH_NARROW_UNARY(coshl, cosh)
CARBON_MATH_NARROW_UNARY(erfl, erf)
CARBON_MATH_NARROW_UNARY(erfcl, erfc)
CARBON_MATH_NARROW_UNARY(lgammal, lgamma)
CARBON_MATH_NARROW_UNARY(sinhl, sinh)
CARBON_MATH_NARROW_UNARY(tanhl, tanh)
CARBON_MATH_NARROW_UNARY(gammal, tgamma)
CARBON_MATH_NARROW_BINARY(fdiml, fdim)
CARBON_MATH_NARROW_BINARY(fmaxl, fmax)
CARBON_MATH_NARROW_BINARY(fminl, fmin)
CARBON_MATH_NARROW_BINARY(remainderl, remainder)
CARBON_MATH_NARROW_AUX(remquol, remquo)

#undef CARBON_MATH_QUAD_UNARY
#undef CARBON_MATH_QUAD_BINARY
#undef CARBON_MATH_NARROW_UNARY
#undef CARBON_MATH_NARROW_BINARY
#undef CARBON_MATH_NARROW_AUX
#undef CARBON_MATH_EXTENDED
#undef CARBON_MATH_FINANCIAL
#undef CARBON_MATH_ENV
#undef CARBON_MATH_AUX
#undef CARBON_MATH_BITS
#undef CARBON_MATH_CLASS
#undef CARBON_MATH_BINARY
#undef CARBON_MATH_UNARY
#undef CARBON_MATH_ENTRY
#endif
