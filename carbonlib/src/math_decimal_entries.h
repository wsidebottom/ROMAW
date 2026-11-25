/*+===================================================================
  File:      math_decimal_entries.h

  Summary:   Marshal decimal records and MathLib NaN constructors in PPC call frames.

  Classes:   None.

  Functions: Private decimal conversion entries for the CarbonLib export table.

  Origin:    MathLib 9.2.1 PowerPC exports and the Inside Macintosh: PowerPC Numerics record format.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MATH_DECIMAL_ENTRIES_H
#define ROMAW_CARBON_MATH_DECIMAL_ENTRIES_H

#include "math_native.h"
#include "synppc/src/romaw/decimal.h"
#include <string.h>

static int carbon_decimal_short(const unsigned char* bytes)
{
    return (int16_t)((bytes[0] << 8) | bytes[1]);
}

static void carbon_decimal_store_short(unsigned char* bytes, int value)
{
    bytes[0] = (uint16_t)value >> 8;
    bytes[1] = (uint16_t)value;
}

static void* carbon_decimal_memory(uint32_t address, size_t length, int writable)
{
    return address ? ROMlib_carbon_try_guest_memory(address, length, writable) : NULL;
}

static const char* carbon_decimal_string(uint32_t address, size_t* length)
{
    if (address) {
        for (uint32_t offset = 0; offset <= INT16_MAX && address <= UINT32_MAX - offset; ++offset) {
            const char* byte = ROMlib_carbon_try_guest_memory(address + offset, 1, 0);
            if (!byte)
                break;
            if (!*byte) {
                *length = offset;
                return ROMlib_carbon_try_guest_memory(address, offset + 1, 0);
            }
        }
    }
    return NULL;
}

static int carbon_math_entry_num2dec(ROMlib_carbon_ppc_frame* call_frame)
{
    const unsigned char* form = carbon_decimal_memory(call_frame->words[0], 4, 0);
    /* f1 occupies the r4/r5 parameter slots; the result record follows in r6. */
    unsigned char* decimal = carbon_decimal_memory(call_frame->words[3], ROMLIB_PPC_DECIMAL_SIZE, 1);
    double value;
    memcpy(&value, &call_frame->floating[0], sizeof value);
    if (form && decimal && ROMlib_ppc_num2dec(form[0], carbon_decimal_short(form + 2), value, decimal))
        return 1;
    return ROMlib_carbon_report_failure("Invalid MathLib decimal format or output");
}

static int carbon_math_entry_num2decl(ROMlib_carbon_ppc_frame* call_frame)
{
    const unsigned char* form = carbon_decimal_memory(call_frame->words[0], 4, 0);
    /* Two double limbs occupy r4 through r7. */
    unsigned char* decimal = carbon_decimal_memory(call_frame->words[5], ROMLIB_PPC_DECIMAL_SIZE, 1);
    if (!form || !decimal)
        return ROMlib_carbon_report_failure("Invalid MathLib decimal format or output");
    return ROMlib_carbon_math_binary_to_decl(call_frame, form[0], carbon_decimal_short(form + 2), decimal);
}

static int carbon_math_entry_dec2str(ROMlib_carbon_ppc_frame* call_frame)
{
    const unsigned char* form = carbon_decimal_memory(call_frame->words[0], 4, 0);
    const unsigned char* input = carbon_decimal_memory(call_frame->words[1], ROMLIB_PPC_DECIMAL_SIZE, 0);
    char* output = carbon_decimal_memory(call_frame->words[2], ROMLIB_PPC_DECIMAL_STRING_SIZE, 1);
    if (form && input && output) {
        unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE];
        memcpy(decimal, input, sizeof decimal);
        if (ROMlib_ppc_dec2str(form[0], carbon_decimal_short(form + 2), decimal, output))
            return 1;
    }
    return ROMlib_carbon_report_failure("Invalid MathLib decimal format, record or output");
}

static int carbon_math_entry_str2dec(ROMlib_carbon_ppc_frame* call_frame)
{
    size_t length;
    const char* text = carbon_decimal_string(call_frame->words[0], &length);
    unsigned char* index_ptr = carbon_decimal_memory(call_frame->words[1], 2, 1);
    unsigned char* output = carbon_decimal_memory(call_frame->words[2], ROMLIB_PPC_DECIMAL_SIZE, 1);
    unsigned char* valid_ptr = carbon_decimal_memory(call_frame->words[3], 2, 1);
    if (text && index_ptr && output && valid_ptr) {
        unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE];
        int index = carbon_decimal_short(index_ptr), valid;
        if (ROMlib_ppc_str2dec(text, length, &index, decimal, &valid)) {
            memcpy(output, decimal, sizeof decimal);
            carbon_decimal_store_short(index_ptr, index);
            carbon_decimal_store_short(valid_ptr, valid);
            return 1;
        }
    }
    return ROMlib_carbon_report_failure("Invalid MathLib decimal string or output");
}

static int carbon_math_entry_dec2num(ROMlib_carbon_ppc_frame* call_frame)
{
    const unsigned char* decimal = carbon_decimal_memory(call_frame->words[0], ROMLIB_PPC_DECIMAL_SIZE, 0);
    double value;
    if (decimal && ROMlib_ppc_dec2num(decimal, &value)) {
        memcpy(&call_frame->floating[0], &value, sizeof value);
        return 1;
    }
    return ROMlib_carbon_report_failure("Invalid MathLib decimal record");
}

static int carbon_math_entry_dec2f(ROMlib_carbon_ppc_frame* call_frame)
{
    const unsigned char* decimal = carbon_decimal_memory(call_frame->words[0], ROMLIB_PPC_DECIMAL_SIZE, 0);
    return ROMlib_carbon_math_dec_to_binary(call_frame, decimal, 1);
}

static int carbon_math_entry_dec2numl(ROMlib_carbon_ppc_frame* call_frame)
{
    const unsigned char* decimal = carbon_decimal_memory(call_frame->words[0], ROMLIB_PPC_DECIMAL_SIZE, 0);
    return ROMlib_carbon_math_dec_to_binary(call_frame, decimal, 0);
}

static int carbon_math_entry_ldexpl(ROMlib_carbon_ppc_frame* call_frame)
{
    return ROMlib_carbon_math_extended(call_frame, carbon_math_dd_scale);
}

static int carbon_decimal_integer(ROMlib_carbon_ppc_frame* call_frame, const unsigned char* decimal, uint32_t limit,
                                  int32_t* output)
{
    int count = decimal[4];
    if (!count || decimal[5] == '0') {
        *output = 0;
        return 1;
    }
    if (count > 36)
        return ROMlib_carbon_report_failure("Invalid MathLib decimal length");
    const int negative = decimal[0] != 0;
    const int places = count + carbon_decimal_short(decimal + 2);
    uint64_t magnitude = 0;
    uint32_t exception;
    if (decimal[5] == 'N' || decimal[5] == 'I' || places > 10)
        exception = 0x20000000;
    else {
        int first_fraction = 0, sticky = 0;
        for (int i = 0; i < count; ++i) {
            unsigned digit = decimal[5 + i] - '0';
            if (digit > 9)
                return ROMlib_carbon_report_failure("Invalid MathLib decimal digit");
            if (i < places)
                magnitude = magnitude * 10 + digit;
            else if (i == places)
                first_fraction = digit;
            else
                sticky |= digit != 0;
        }
        for (int i = count; i < places; ++i)
            magnitude *= 10;
        const int inexact = first_fraction || sticky;
        const unsigned rounding = call_frame->fpscr & 3;
        if ((!rounding && (first_fraction > 5 || (first_fraction == 5 && (sticky || (magnitude & 1))))) ||
            (inexact && rounding == (negative ? 3u : 2u)))
            ++magnitude;
        exception = magnitude > limit - !negative ? 0x20000000 : inexact ? 0x02000000 : 0;
    }
    if (exception == 0x20000000)
        *output = -(int64_t)limit;
    else
        *output = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    if (exception)
        call_frame->fpscr = ROMlib_carbon_math_raise_exceptions(call_frame->fpscr, exception);
    return 1;
}

static int carbon_math_integer_entry(ROMlib_carbon_ppc_frame* call_frame, uint32_t limit)
{
    const unsigned char* decimal = carbon_decimal_memory(call_frame->words[0], ROMLIB_PPC_DECIMAL_SIZE, 0);
    int32_t result;
    if (!decimal)
        return ROMlib_carbon_report_failure("Missing MathLib decimal record");
    if (!carbon_decimal_integer(call_frame, decimal, limit, &result))
        return 0;
    call_frame->words[0] = result;
    return 1;
}

static int carbon_math_entry_dec2s(ROMlib_carbon_ppc_frame* call_frame)
{
    return carbon_math_integer_entry(call_frame, UINT32_C(0x8000));
}

static int carbon_math_entry_dec2l(ROMlib_carbon_ppc_frame* call_frame)
{
    return carbon_math_integer_entry(call_frame, UINT32_C(0x80000000));
}

static int carbon_math_entry_nan(ROMlib_carbon_ppc_frame* call_frame)
{
    unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE];
    size_t length;
    const char* tag = carbon_decimal_string(call_frame->words[0], &length);
    int index = 0, valid;
    int32_t payload;
    if (!tag || !ROMlib_ppc_str2dec(tag, length, &index, decimal, &valid))
        return ROMlib_carbon_report_failure("Invalid MathLib NaN tag pointer");
    if (!carbon_decimal_integer(call_frame, decimal, UINT32_C(0x8000), &payload))
        return 0;
    payload = payload < 0 ? 0 : payload > 255 ? 255 : payload;
    /* This payload survives widening from a single-precision PPC result. */
    call_frame->floating[0] = UINT64_C(0x7ff8000000000000) | (uint64_t)payload << 37;
    return 1;
}

static int carbon_math_entry_nanl(ROMlib_carbon_ppc_frame* call_frame)
{
    if (!carbon_math_entry_nan(call_frame))
        return 0;
    call_frame->floating[1] = 0;
    return 1;
}

#endif
