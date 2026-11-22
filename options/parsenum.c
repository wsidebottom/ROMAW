/*+===================================================================
  File:      parsenum.c

  Summary:   Implements parsenum support for ROMAW options and preferences.

  Classes:   None.

  Functions: parse_number

  Origin:    Existing repository source in executor/src/options; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/parsenum.h"
#include <ctype.h>
#include <limits.h>

/* Parse a bounded range without modifying or copying the input. */
static boolean_t parse_base_number(const char* s, const char* end, const char* digits, unsigned long long* vp,
                                   unsigned long long* divisorp)
{
    unsigned long long value = 0, divisor = 1;
    unsigned radix = strlen(digits);
    boolean_t found_dot = FALSE, found_digit = FALSE;

    for (; s < end; ++s) {
        const char* digit;
        unsigned n = toupper((unsigned char)*s);
        if (n == '.') {
            if (found_dot)
                return FALSE;
            found_dot = TRUE;
            continue;
        }
        digit = strchr(digits, n);
        if (!digit)
            return FALSE;
        n = digit - digits;
        if (value > (ULLONG_MAX - n) / radix)
            return FALSE;
        value = value * radix + n;
        if (found_dot) {
            if (divisor > ULLONG_MAX / radix)
                return FALSE;
            divisor *= radix;
        }
        found_digit = TRUE;
    }
    if (!found_digit)
        return FALSE;
    *vp = value;
    *divisorp = divisor;
    return TRUE;
}

/* Parse a number specified in a flexible format.  Returns TRUE iff
 * the parse was successful and the result didn't exceed 32 bits,
 * else FALSE.  Returns by reference the parsed value in *val.
 *
 * Each number may have an optional preceding "+" or "-" sign, which
 * do the obvious thing.
 * 
 * Base numbers may be specified in several ways:
 *
 * "123"	; decimal number
 * "146.5"	; floating point value
 * "0x12AC"	; hexadecimal number
 * "0x12AC.12A"	; hexadecimal floating point
 * "0b010010"	; binary number
 * "0b010.10"	; binary floating point
 *
 * I decided not to allow octal because that might confuse
 * unsophisticated users who accidentally precede a number with a
 * leading zero.
 *
 * The number may be followed by an optional case-insensitive suffix:
 *
 * "K" or "k"	; multiply the value by 1024 (1K)
 * "M" or "m"	; multiply the value by 1024*1024 (1M)
 *
 * Floating point results will be rounded toward zero before they are
 * returned.  The rounding happens *after* any multiplication suffix
 * is applied, so "1.5K" is exactly equivalent to "1536".
 *
 * The truncated result can then be rounded up to the next highest
 * (farther from zero) multiple of a specified number.  If you don't
 * want rounding, just pass "1" for round_up_to_multiple_of.
 */
boolean_t parse_number(const char* orig_num, int32* val, unsigned round_up_to_multiple_of)
{
    unsigned long long magnitude, divisor, limit;
    const char *num, *end, *digits;
    unsigned shift = 0;
    boolean_t negative;

    if (!val)
        return FALSE;
    *val = 0;
    if (!orig_num || !*orig_num)
        return FALSE;
    num = orig_num;
    negative = *num == '-';
    if (*num == '-' || *num == '+')
        ++num;
    end = num + strlen(num);
    if (num == end)
        return FALSE;
    switch (end[-1]) {
    case 'k':
    case 'K':
        shift = 10;
        --end;
        break;
    case 'm':
    case 'M':
        shift = 20;
        --end;
        break;
    }
    digits = "0123456789";
    if (end - num >= 2 && num[0] == '0') {
        if (num[1] == 'x')
            digits = "0123456789ABCDEF", num += 2;
        else if (num[1] == 'b')
            digits = "01", num += 2;
    }
    if (!parse_base_number(num, end, digits, &magnitude, &divisor))
        return FALSE;
    if (magnitude > (ULLONG_MAX >> shift))
        return FALSE;
    magnitude = (magnitude << shift) / divisor;
    if (round_up_to_multiple_of > 1) {
        unsigned long long remainder = magnitude % round_up_to_multiple_of;
        if (remainder) {
            unsigned long long increment = round_up_to_multiple_of - remainder;
            if (magnitude > ULLONG_MAX - increment)
                return FALSE;
            magnitude += increment;
        }
    }
    limit = negative ? 2147483648ULL : 2147483647ULL;
    if (magnitude > limit)
        return FALSE;
    if (negative)
        *val = magnitude == 2147483648ULL ? (-2147483647 - 1) : -(int32)magnitude;
    else
        *val = magnitude;
    return TRUE;
}
