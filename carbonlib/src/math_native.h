/*+===================================================================
  File:      math_native.h

  Summary:   Native MathLib operations on a PowerPC call frame.

  Classes:   None.

  Functions: Internal declarations for scalar arithmetic, classification, and floating-point environment support.

  Origin:    MathLib PowerPC entry points and Universal Interfaces 3.4.2 fp.h and fenv.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MATH_NATIVE_H
#define ROMAW_CARBON_MATH_NATIVE_H

#include "carbonlib.h"
#include <fenv.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t ROMlib_carbon_math_raise_exceptions(uint32_t status, uint32_t mask);

typedef enum {
    carbon_math_class,
    carbon_math_finite,
    carbon_math_nan,
    carbon_math_normal,
    carbon_math_sign
} carbon_math_predicate;

typedef enum {
    carbon_math_abs,
    carbon_math_extended_abs,
    carbon_math_copy_sign,
    carbon_math_extended_copy_sign,
    carbon_math_infinity,
    carbon_math_relation
} carbon_math_bits_op;

typedef enum {
    carbon_math_frexp,
    carbon_math_ldexp,
    carbon_math_modf,
    carbon_math_remquo,
    carbon_math_modff,
    carbon_math_nextafterf,
    carbon_math_randomx
} carbon_math_aux_op;

typedef enum {
    carbon_math_get_round,
    carbon_math_set_round,
    carbon_math_get_env,
    carbon_math_set_env,
    carbon_math_hold_except,
    carbon_math_test_except,
    carbon_math_clear_except,
    carbon_math_get_except,
    carbon_math_set_except,
    carbon_math_raise_except,
    carbon_math_update_env
} carbon_math_env_op;

typedef enum {
    carbon_math_dd_floor,
    carbon_math_dd_ceil,
    carbon_math_dd_trunc,
    carbon_math_dd_round,
    carbon_math_dd_rint,
    carbon_math_dd_nearby,
    carbon_math_dd_sqrt,
    carbon_math_dd_scale,
    carbon_math_dd_modf,
    carbon_math_dd_relation,
    carbon_math_to_long,
    carbon_math_round_to_long,
    carbon_math_dd_to_long,
    carbon_math_dd_round_to_long,
    carbon_math_x80_to_double,
    carbon_math_double_to_x80,
    carbon_math_x80_to_dd,
    carbon_math_dd_to_x80
} carbon_math_extended_op;

typedef struct {
    fenv_t fp_environment;
    int saved_errno;
} math_host_environment;

int ROMlib_carbon_math_begin_host(math_host_environment* saved, uint32_t fpscr);
void ROMlib_carbon_math_finish_host(ROMlib_carbon_ppc_frame* call_frame, const math_host_environment* saved);
int ROMlib_carbon_math_unary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double));
int ROMlib_carbon_math_binary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double, double));
int ROMlib_carbon_math_classify(ROMlib_carbon_ppc_frame* call_frame, carbon_math_predicate predicate, int single);
int ROMlib_carbon_math_bits(ROMlib_carbon_ppc_frame* call_frame, carbon_math_bits_op operation);
int ROMlib_carbon_math_aux(ROMlib_carbon_ppc_frame* call_frame, carbon_math_aux_op operation);
int ROMlib_carbon_math_env(ROMlib_carbon_ppc_frame* call_frame, carbon_math_env_op operation);
int ROMlib_carbon_math_financial(ROMlib_carbon_ppc_frame* call_frame, int annuity);
int ROMlib_carbon_math_dec_to_binary(ROMlib_carbon_ppc_frame* call_frame, const unsigned char* decimal, int single);
int ROMlib_carbon_math_binary_to_decl(ROMlib_carbon_ppc_frame* call_frame, int style, int digits, unsigned char* decimal);
int ROMlib_carbon_math_dd_frexp(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_carbon_math_extended(ROMlib_carbon_ppc_frame* call_frame, carbon_math_extended_op operation);
int ROMlib_carbon_fixed_to_double(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_fract_to_double(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_double_to_fixed(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_double_to_fract(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_fract_cosine(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_fract_sine(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_fract_square_root(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_fixed_atan2(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_math_dd_unary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double));
int ROMlib_carbon_math_dd_binary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double, double));
int ROMlib_carbon_math_dd_remquo(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_carbon_math_quad_unary(ROMlib_carbon_ppc_frame* call_frame, __float128 (*function)(__float128));
int ROMlib_carbon_math_quad_binary(ROMlib_carbon_ppc_frame* call_frame, __float128 (*function)(__float128, __float128));

#ifdef __cplusplus
}
#endif

#endif
