/*+===================================================================
  File:      math_native.c

  Summary:   Implement scalar MathLib operations and their PowerPC value, output pointer, and environment conventions.

  Classes:   None.

  Functions: ROMlib_carbon_math_aux, ROMlib_carbon_math_begin_host, ROMlib_carbon_math_finish_host, ROMlib_carbon_math_binary, ROMlib_carbon_math_bits, ROMlib_carbon_math_classify,
             ROMlib_carbon_math_env, ROMlib_carbon_math_financial, ROMlib_carbon_math_raise_exceptions,
             ROMlib_carbon_math_unary

  Origin:    Original native implementation from local MathLib PowerPC disassembly and Apple Universal Interfaces.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "math_native.h"
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <string.h>

#define PPC_SIGN UINT64_C(0x8000000000000000)
#define PPC_EXPONENT UINT64_C(0x7ff0000000000000)
#define PPC_QUIET UINT64_C(0x0008000000000000)

enum {
    math_snan = 0,
    math_qnan,
    math_infinite,
    math_zero,
    math_normal,
    math_subnormal,
};

static double math_value(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static uint64_t math_bits(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

int ROMlib_carbon_math_begin_host(math_host_environment* saved, uint32_t fpscr)
{
    static const int rounding[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    saved->saved_errno = errno;
    if (feholdexcept(&saved->fp_environment))
        return ROMlib_carbon_report_failure("Cannot save the host floating-point environment");
    if (fesetround(rounding[fpscr & 3])) {
        fesetenv(&saved->fp_environment);
        errno = saved->saved_errno;
        return ROMlib_carbon_report_failure("Cannot select the PowerPC rounding mode");
    }
    return 1;
}

void ROMlib_carbon_math_finish_host(ROMlib_carbon_ppc_frame* call_frame, const math_host_environment* saved)
{
    int exceptions = fetestexcept(FE_ALL_EXCEPT);
    fesetenv(&saved->fp_environment);
    errno = saved->saved_errno;
    uint32_t status = 0;
    if (exceptions & FE_INVALID)
        status |= 0x20000400;
    if (exceptions & FE_OVERFLOW)
        status |= 0x10000000;
    if (exceptions & FE_UNDERFLOW)
        status |= 0x08000000;
    if (exceptions & FE_DIVBYZERO)
        status |= 0x04000000;
    if (exceptions & FE_INEXACT)
        status |= 0x02000000;
    if (status) {
        call_frame->fpscr |= status | 0x80000000U;
        if (status & ((call_frame->fpscr & 0xf8) << 22))
            call_frame->fpscr |= 0x40000000;
    }
}

int ROMlib_carbon_math_unary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double))
{
    math_host_environment saved;
    if (!ROMlib_carbon_math_begin_host(&saved, call_frame->fpscr))
        return 0;
    call_frame->floating[0] = math_bits(function(math_value(call_frame->floating[0])));
    ROMlib_carbon_math_finish_host(call_frame, &saved);
    return 1;
}

int ROMlib_carbon_math_binary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double, double))
{
    math_host_environment saved;
    if (!ROMlib_carbon_math_begin_host(&saved, call_frame->fpscr))
        return 0;
    call_frame->floating[0] =
        math_bits(function(math_value(call_frame->floating[0]), math_value(call_frame->floating[1])));
    ROMlib_carbon_math_finish_host(call_frame, &saved);
    return 1;
}

int ROMlib_carbon_math_financial(ROMlib_carbon_ppc_frame* call_frame, int annuity)
{
    math_host_environment saved;
    if (!ROMlib_carbon_math_begin_host(&saved, call_frame->fpscr))
        return 0;
    double rate = math_value(call_frame->floating[0]), periods = math_value(call_frame->floating[1]);
    double value;
    if (!annuity)
        value = rate > -1 ? exp(periods * log1p(rate)) : pow(1 + rate, periods);
    else if (rate == 0)
        value = periods;
    else
        value = (rate > -1 ? -expm1(-periods * log1p(rate)) : 1 - pow(1 + rate, -periods)) / rate;
    call_frame->floating[0] = math_bits(value);
    ROMlib_carbon_math_finish_host(call_frame, &saved);
    return 1;
}

static int classify_bits(uint64_t bits, int single)
{
    uint64_t magnitude = bits & ~PPC_SIGN;
    if (magnitude >= PPC_EXPONENT)
        return magnitude == PPC_EXPONENT ? math_infinite : (bits & PPC_QUIET) ? math_qnan : math_snan;
    if (!magnitude)
        return math_zero;
    if (single && magnitude < UINT64_C(0x3810000000000000))
        return math_subnormal;
    return (magnitude & PPC_EXPONENT) ? math_normal : math_subnormal;
}

int ROMlib_carbon_math_classify(ROMlib_carbon_ppc_frame* call_frame, carbon_math_predicate predicate, int single)
{
    uint64_t bits = call_frame->floating[0];
    int number_class = classify_bits(bits, single);
    switch (predicate) {
    case carbon_math_class:
        call_frame->words[0] = number_class;
        break;
    case carbon_math_finite:
        call_frame->words[0] = number_class >= math_zero;
        break;
    case carbon_math_nan:
        call_frame->words[0] = number_class <= math_qnan;
        break;
    case carbon_math_normal:
        call_frame->words[0] = number_class == math_normal;
        break;
    case carbon_math_sign:
        call_frame->words[0] = (bits & PPC_SIGN) != 0;
        break;
    }
    return 1;
}

int ROMlib_carbon_math_bits(ROMlib_carbon_ppc_frame* call_frame, carbon_math_bits_op operation)
{
    uint64_t* floating = call_frame->floating;
    switch (operation) {
    case carbon_math_abs:
        floating[0] &= ~PPC_SIGN;
        break;
    case carbon_math_extended_abs:
        if (floating[0] & PPC_SIGN) {
            floating[0] ^= PPC_SIGN;
            floating[1] ^= PPC_SIGN;
        }
        break;
    case carbon_math_copy_sign:
        floating[0] = (floating[0] & ~PPC_SIGN) | (floating[1] & PPC_SIGN);
        break;
    case carbon_math_extended_copy_sign:
        if ((floating[0] ^ floating[2]) & PPC_SIGN) {
            floating[0] ^= PPC_SIGN;
            floating[1] ^= PPC_SIGN;
        }
        break;
    case carbon_math_infinity:
        floating[0] = PPC_EXPONENT;
        break;
    case carbon_math_relation: {
        uint64_t first = floating[0], second = floating[1];
        if ((first & ~PPC_SIGN) > PPC_EXPONENT || (second & ~PPC_SIGN) > PPC_EXPONENT) {
            if (classify_bits(first, 0) == math_snan || classify_bits(second, 0) == math_snan)
                call_frame->fpscr |= 0xa1000000U;
            call_frame->words[0] = 3;
        }
        else {
            double left = math_value(first), right = math_value(second);
            call_frame->words[0] = left < right ? 1 : left == right ? 2 : 0;
        }
        break;
    }
    }
    return 1;
}

static void write_word(unsigned char* output, uint32_t value)
{
    output[0] = value >> 24;
    output[1] = value >> 16;
    output[2] = value >> 8;
    output[3] = value;
}

static uint32_t read_word(const unsigned char* bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

int ROMlib_carbon_math_aux(ROMlib_carbon_ppc_frame* call_frame, carbon_math_aux_op operation)
{
    unsigned char* output = NULL;
    if (operation != carbon_math_ldexp && operation != carbon_math_nextafterf) {
        unsigned slot = operation == carbon_math_remquo    ? 4
                        : operation == carbon_math_modff   ? 1
                        : operation == carbon_math_randomx ? 0
                                                           : 2;
        uint32_t address = call_frame->words[slot];
        if (!address)
            return ROMlib_carbon_report_failure("Missing MathLib output pointer");
        output = ROMlib_carbon_guest_memory(
            address, (operation == carbon_math_modf || operation == carbon_math_randomx) ? 8 : 4, 1);
        if (!output)
            return 0;
    }
    math_host_environment saved;
    if (!ROMlib_carbon_math_begin_host(&saved, call_frame->fpscr))
        return 0;
    double value = math_value(call_frame->floating[0]);
    double integral = 0;
    int auxiliary = 0;
    switch (operation) {
    case carbon_math_frexp:
        value = frexp(value, &auxiliary);
        write_word(output, auxiliary);
        break;
    case carbon_math_ldexp:
        value = ldexp(value, (int32_t)call_frame->words[2]);
        break;
    case carbon_math_modf: {
        value = modf(value, &integral);
        uint64_t bits = math_bits(integral);
        write_word(output, bits >> 32);
        write_word(output + 4, bits);
        break;
    }
    case carbon_math_remquo:
        value = remquo(value, math_value(call_frame->floating[1]), &auxiliary);
        write_word(output, auxiliary);
        break;
    case carbon_math_modff: {
        float ipart;
        value = modff((float)value, &ipart);
        uint32_t bits;
        memcpy(&bits, &ipart, sizeof bits);
        write_word(output, bits);
        break;
    }
    case carbon_math_nextafterf:
        value = nextafterf((float)value, (float)math_value(call_frame->floating[1]));
        break;
    case carbon_math_randomx: {
        uint64_t seed = (uint64_t)read_word(output) << 32 | read_word(output + 4);
        value = fmod(16807.0 * math_value(seed), 2147483647.0);
        if (signbit(value))
            value += 2147483647.0;
        uint64_t bits = math_bits(value);
        write_word(output, bits >> 32);
        write_word(output + 4, bits);
        break;
    }
    }
    call_frame->floating[0] = math_bits(value);
    ROMlib_carbon_math_finish_host(call_frame, &saved);
    return 1;
}

/* The MathLib environment entries write FPSCR using mtfsf, which
   recomputes VX and FEX from the cause and enable bits. */
static uint32_t normalize_environment(uint32_t status)
{
    status &= ~0x60000000U;
    if (status & 0x01f80700)
        status |= 0x20000000;
    if ((status >> 25) & (status >> 3) & 0x1f)
        status |= 0x40000000;
    return status;
}

uint32_t ROMlib_carbon_math_raise_exceptions(uint32_t status, uint32_t mask)
{
    uint32_t causes = mask & 0x1e000000;
    if (mask & 0x20000000)
        causes |= 0x01000000;
    return normalize_environment(status | causes);
}

int ROMlib_carbon_math_env(ROMlib_carbon_ppc_frame* call_frame, carbon_math_env_op operation)
{
    const uint32_t invalid = 0x20000000, exception_mask = 0x3e000000, invalid_causes = 0x01f80100;
    uint32_t mask = call_frame->words[0] & exception_mask;
    unsigned char* environment = NULL;
    if (operation == carbon_math_get_except || operation == carbon_math_set_except) {
        mask = call_frame->words[1] & exception_mask;
        if (!mask && operation == carbon_math_set_except)
            return 1;
    }
    int write_environment =
        operation == carbon_math_get_env || operation == carbon_math_hold_except || operation == carbon_math_get_except;
    if (write_environment || operation == carbon_math_set_env || operation == carbon_math_set_except ||
        operation == carbon_math_update_env) {
        uint32_t address = call_frame->words[0];
        if (!address)
            return ROMlib_carbon_report_failure("Missing MathLib environment pointer");
        environment = ROMlib_carbon_guest_memory(address, 4, write_environment);
        if (!environment)
            return 0;
    }
    switch (operation) {
    case carbon_math_get_round:
        call_frame->words[0] = call_frame->fpscr & 3;
        break;
    case carbon_math_set_round: {
        uint32_t rounding = call_frame->words[0];
        if (rounding <= 3)
            call_frame->fpscr = (call_frame->fpscr & ~3u) | rounding;
        call_frame->words[0] = rounding <= 3;
        break;
    }
    case carbon_math_get_env:
        write_word(environment, call_frame->fpscr);
        break;
    case carbon_math_set_env:
        call_frame->fpscr = normalize_environment(read_word(environment));
        break;
    case carbon_math_hold_except:
        write_word(environment, call_frame->fpscr);
        call_frame->fpscr &= 0x0007fe07;
        call_frame->words[0] = 1;
        break;
    case carbon_math_test_except:
        call_frame->words[0] = call_frame->fpscr & mask;
        break;
    case carbon_math_clear_except:
        call_frame->fpscr &= ~(mask | ((mask & invalid) ? invalid_causes : 0));
        if (!(call_frame->fpscr & exception_mask))
            call_frame->fpscr &= ~0x80000000u;
        call_frame->fpscr = normalize_environment(call_frame->fpscr);
        break;
    case carbon_math_get_except: {
        uint32_t selected = mask & call_frame->fpscr;
        uint32_t result = 0;
        if (selected) {
            uint32_t keep = (selected & invalid) ? 0xfff80100 : 0xfe000000;
            result = call_frame->fpscr & keep & (selected | ~exception_mask);
        }
        write_word(environment, result);
        break;
    }
    case carbon_math_set_except: {
        uint32_t value = read_word(environment);
        uint32_t replace = mask | ((mask & invalid) ? invalid_causes : 0);
        uint32_t status = (call_frame->fpscr & ~replace) | (value & replace);
        status = (status & ~0x80000000u) | ((status & exception_mask) ? 0x80000000u : 0);
        call_frame->fpscr = normalize_environment(status);
        break;
    }
    case carbon_math_raise_except:
        call_frame->fpscr = ROMlib_carbon_math_raise_exceptions(call_frame->fpscr, mask);
        break;
    case carbon_math_update_env:
        call_frame->fpscr =
            ROMlib_carbon_math_raise_exceptions(read_word(environment), call_frame->fpscr & exception_mask);
        break;
    }
    return 1;
}
