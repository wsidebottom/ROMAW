/*+===================================================================
  File:      cfx_dates.c

  Summary:   Implements CoreFoundation dates service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFAbsoluteTimeAddGregorianUnits, ROMlib_cfx_CFAbsoluteTimeGetDayOfWeek,
             ROMlib_cfx_CFAbsoluteTimeGetDayOfYear,
             ROMlib_cfx_CFAbsoluteTimeGetDifferenceAsGregorianUnits,
             ROMlib_cfx_CFAbsoluteTimeGetGregorianDate, ROMlib_cfx_CFAbsoluteTimeGetWeekOfYear,
             ROMlib_cfx_CFGregorianDateGetAbsoluteTime, ROMlib_cfx_CFGregorianDateIsValid

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

#include "cfx_float.h"

#include <math.h>

typedef struct {
    ROMlib_carbon_ppc_frame call_frame;
    int success;
} service_date_context;

typedef struct {
    int32 year;
    int month, day, hour, minute;
    double second;
} service_date;

static uint64 date_bits(double value)
{
    uint64 bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static double date_value(uint64 bits)
{
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static int date_finite(double value)
{
    return (date_bits(value) & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}

static double date_fp(service_date_context* context, unsigned operation, double a, double b, double c)
{
    uint64 result = 0;
    if (context->success && ROMlib_cfx_fp_arithmetic(operation, date_bits(a), date_bits(b), date_bits(c),
                                                     &context->call_frame.fpscr, &result) != 1)
        context->success = 0;
    return date_value(result);
}

static unsigned date_compare(service_date_context* context, double a, double b, int ordered)
{
    return ROMlib_cfx_fp_compare(date_bits(a), date_bits(b), ordered, &context->call_frame.fpscr);
}

static int32 date_integer(service_date_context* context, double value)
{
    int32 result = 0;
    if (context->success && !ROMlib_cfx_fp_integer(date_bits(value), &context->call_frame.fpscr, &result))
        context->success = 0;
    return result;
}

static int64 date_floor_divide(int64 value, int64 divisor)
{
    int64 quotient = value / divisor, remainder = value % divisor;
    return quotient - (remainder < 0);
}

static int64 date_year_days(int64 year)
{
    --year;
    return year * 365 + date_floor_divide(year, 4) - date_floor_divide(year, 100) + date_floor_divide(year, 400);
}

static int date_month_days(int64 year, int month)
{
    static const Byte lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    return lengths[month - 1] + (month == 2 && !(year % 4) && ((year % 100) || !(year % 400)));
}

static int64 date_day_number(int64 year, int month, int day)
{
    int64 result = date_year_days(year) - date_year_days(2001) + day - 1;
    for (int index = 1; index < month; ++index)
        result += date_month_days(year, index);
    return result;
}

static int date_civil(int32 number, service_date* result)
{
    int64 absolute = (int64)number + date_year_days(2001), lower = -6000000, upper = 6000001;
    while (lower + 1 < upper) {
        int64 middle = lower + (upper - lower) / 2;
        if (date_year_days(middle) <= absolute)
            lower = middle;
        else
            upper = middle;
    }
    result->year = lower;
    result->month = 1;
    int64 day = absolute - date_year_days(lower);
    while (result->month < 12 && day >= date_month_days(lower, result->month)) {
        day -= date_month_days(lower, result->month);
        ++result->month;
    }
    result->day = day + 1;
    return day >= 0 && day < date_month_days(lower, result->month);
}

static int date_supported(const ROMlib_carbon_ppc_frame* call_frame, uint32 timezone, double value)
{
    /* Enabled exceptions can inhibit a temporary destination in the reference
       frame. Preserve that original path rather than fabricating its old FPR. */
    return !timezone && !(call_frame->fpscr & 0xf8) && date_finite(value) && value > -180000000000000.0 &&
           value < 180000000000000.0;
}

static int date_day(service_date_context* context, double local, int32* result)
{
    double divided = date_fp(context, 18, local, 86400, 0);
    *result = date_integer(context, floor(divided));
    return context->success;
}

static int date_gregorian(service_date_context* context, double absolute, service_date* result)
{
    int32 day;
    double local = date_fp(context, 21, absolute, 0, 0);
    if (!date_day(context, local, &day) || !date_civil(day, result))
        return 0;
    double hour_total = floor(date_fp(context, 18, local, 3600, 0));
    double hour_days = floor(date_fp(context, 18, hour_total, 24, 0));
    result->hour = date_integer(context, floor(date_fp(context, 30, hour_days, hour_total, 24)));
    if (result->hour < 0)
        result->hour += 24;
    double minutes = date_fp(context, 18, local, 60, 0);
    double minute_total = floor(minutes);
    double minute_hours = floor(date_fp(context, 18, minute_total, 60, 0));
    result->minute = date_integer(context, floor(date_fp(context, 30, minute_hours, minute_total, 60)));
    if (result->minute < 0)
        result->minute += 60;
    double floored = floor(minutes);
    result->second = date_fp(context, 30, floored, local, 60);
    if (date_compare(context, result->second, 0, 1) & 8)
        result->second = date_fp(context, 21, result->second, 60, 0);
    return context->success;
}

static int date_absolute(service_date_context* context, const service_date* value, double* result)
{
    int64 day = date_day_number(value->year, value->month, value->day);
    if (day < INT32_MIN || day > INT32_MAX)
        return 0;
    double date_part = date_fp(context, 25, (double)day, 0, 86400);
    double time = date_fp(context, 25, (double)value->minute, 0, 60);
    time = date_fp(context, 29, (double)value->hour, time, 3600);
    time = date_fp(context, 21, value->second, time, 0);
    *result = date_fp(context, 21, date_part, time, 0);
    return context->success;
}

static int date_add(service_date_context* context, double absolute, const int32 units[5], double seconds,
                    double* output)
{
    service_date value;
    if (!date_gregorian(context, absolute, &value))
        return 0;
    int64 month = (int64)value.month + units[1] - 1;
    int64 year = (int64)value.year + units[0] + date_floor_divide(month, 12);
    value.month = month - date_floor_divide(month, 12) * 12 + 1;
    if (year < INT32_MIN || year > INT32_MAX)
        return 0;
    value.year = year;
    int month_days = date_month_days(year, value.month);
    if (value.day > month_days)
        value.day = month_days;
    int64 day = date_day_number(year, value.month, value.day) + units[2];
    if (day < INT32_MIN || day > INT32_MAX)
        return 0;
    service_date adjusted;
    if (!date_civil(day, &adjusted))
        return 0;
    adjusted.hour = value.hour;
    adjusted.minute = value.minute;
    adjusted.second = value.second;
    if (!date_absolute(context, &adjusted, output))
        return 0;
    *output = date_fp(context, 29, (double)units[3], *output, 3600);
    *output = date_fp(context, 29, (double)units[4], *output, 60);
    *output = date_fp(context, 21, seconds, *output, 0);
    return context->success;
}

static int date_write_double(uint32 address, double value)
{
    uint64 bits = date_bits(value);
    return cfp_write(address, bits >> 32) && cfp_write(cfx_address(address, 4, 4), bits);
}

static int date_weekday(int64 day)
{
    return day < 0 ? (day + 1) % 7 + 7 : day % 7 + 1;
}

int ROMlib_cfx_CFAbsoluteTimeAddGregorianUnits(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFAbsoluteTimeAddGregorianUnits", 0x23cdc, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 timezone = CFX_ARG(2);
    double absolute = cfx_double(call_frame, 0);
    int32 units[5];
    for (unsigned index = 0; index < 5; ++index)
        units[index] = CFX_ARG(index + 3);
    double seconds = date_value(((uint64)CFX_ARG(8) << 32) | CFX_ARG(9));
    if (!date_supported(call_frame, timezone, absolute) || !date_finite(seconds))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    double result;
    if (!date_add(&context, absolute, units, seconds, &result))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    call_frame->fpscr = context.call_frame.fpscr;
    return cfx_double_result(call_frame, result);
}

int ROMlib_cfx_CFAbsoluteTimeGetDayOfWeek(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFAbsoluteTimeGetDayOfWeek", 0x24160, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 timezone = CFX_ARG(2);
    double absolute = cfx_double(call_frame, 0);
    if (!date_supported(call_frame, timezone, absolute))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    int32 day;
    if (!date_day(&context, date_fp(&context, 21, absolute, 0, 0), &day))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    uint32 result = date_weekday(day);
    call_frame->fpscr = context.call_frame.fpscr;
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFAbsoluteTimeGetDayOfYear(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFAbsoluteTimeGetDayOfYear", 0x24228, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 timezone = CFX_ARG(2);
    double absolute = cfx_double(call_frame, 0);
    if (!date_supported(call_frame, timezone, absolute))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    int32 day;
    if (!date_day(&context, date_fp(&context, 21, absolute, 0, 0), &day))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date value;
    if (!date_civil(day, &value))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    uint32 result = day - date_day_number(value.year, 1, 1) + 1;
    call_frame->fpscr = context.call_frame.fpscr;
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFAbsoluteTimeGetDifferenceAsGregorianUnits(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFAbsoluteTimeGetDifferenceAsGregorianUnits", 0x23f38, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 output = CFX_ARG(0), timezone = CFX_ARG(5), flags = CFX_ARG(6);
    double first = cfx_double(call_frame, 0), second = cfx_double(call_frame, 1);
    if (!date_supported(call_frame, timezone, first) || !date_supported(call_frame, timezone, second))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    int32 units[5] = {0};
    double seconds = 0, current = second;
    if (!(date_compare(&context, first, second, 0) & 2)) {
        int direction = date_compare(&context, second, first, 1) & 8 ? 1 : -1;
        for (unsigned unit = 0; unit < 5; ++unit) {
            if (!(flags & (1U << unit)))
                continue;
            double delta = date_fp(&context, 20, first, current, 0);
            int32 scale = cfp_read(cfx_address(US_TO_SYN68K(reference.toc), 0x353c + unit * 4, 4));
            if (scale <= 0)
                return ROMlib_carbon_invoke_frame(reference.function, call_frame);
            double estimate = date_fp(&context, 18, delta, (double)scale, 0);
            estimate = date_fp(&context, 21, (double)(-3 * direction), estimate, 0);
            units[unit] = date_integer(&context, estimate);
            double previous = current;
            unsigned attempts = 0;
            for (;;) {
                if (++attempts > 1000000 || (direction > 0 && units[unit] == INT32_MAX) ||
                    (direction < 0 && units[unit] == INT32_MIN))
                    return ROMlib_carbon_invoke_frame(reference.function, call_frame);
                previous = current;
                units[unit] += direction;
                if (!date_add(&context, second, units, 0, &current))
                    return ROMlib_carbon_invoke_frame(reference.function, call_frame);
                unsigned comparison = direction == 1 ? date_compare(&context, current, first, 1)
                                                     : date_compare(&context, first, current, 1);
                if (!(comparison & (8 | 2)))
                    break;
            }
            units[unit] -= direction;
            current = previous;
        }
        if (flags & 32)
            seconds = date_fp(&context, 20, first, current, 0);
    }
    if (!context.success)
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    for (unsigned index = 0; index < 5; ++index)
        if (!cfp_write(cfx_address(output, index * 4, 4), units[index]))
            return 0;
    if (!date_write_double(cfx_address(output, 20, 8), seconds))
        return 0;
    call_frame->fpscr = context.call_frame.fpscr;
    return 1;
}

int ROMlib_cfx_CFAbsoluteTimeGetGregorianDate(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFAbsoluteTimeGetGregorianDate", 0x23b44, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 output = CFX_ARG(0), timezone = CFX_ARG(3);
    double absolute = cfx_double(call_frame, 0);
    if (!date_supported(call_frame, timezone, absolute))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    service_date result;
    if (!date_gregorian(&context, absolute, &result))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    uint32 packed = ((uint32)(Byte)result.month << 24) | ((uint32)(Byte)result.day << 16) |
                    ((uint32)(Byte)result.hour << 8) | (Byte)result.minute;
    if (!cfp_write(output, result.year) || !cfp_write(cfx_address(output, 4, 4), packed) ||
        !date_write_double(cfx_address(output, 8, 8), result.second))
        return 0;
    call_frame->fpscr = context.call_frame.fpscr;
    return 1;
}

int ROMlib_cfx_CFAbsoluteTimeGetWeekOfYear(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFAbsoluteTimeGetWeekOfYear", 0x242b4, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 timezone = CFX_ARG(2);
    double absolute = cfx_double(call_frame, 0);
    if (!date_supported(call_frame, timezone, absolute))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    int32 day;
    if (!date_day(&context, date_fp(&context, 21, absolute, 0, 0), &day))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date value;
    if (!date_civil(day, &value))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    int january = date_weekday(date_day_number(value.year, 1, 1));
    uint32 result;
    if (value.month == 1 &&
        ((value.day < 4 && january == 5) || (value.day < 3 && january == 6) || (value.day < 2 && january == 7)))
        result = 53;
    else {
        int next = date_weekday(date_day_number((int64)value.year + 1, 1, 1));
        if (value.month == 12 &&
            ((value.day > 28 && next == 4) || (value.day > 29 && next == 3) || (value.day > 30 && next == 2)))
            result = 1;
        else {
            int preceding = date_day_number(value.year, value.month, 1) - date_day_number(value.year, 1, 1);
            result = (preceding + (january - 11) % 7 + 2 + value.day) / 7 + 1;
        }
    }
    call_frame->fpscr = context.call_frame.fpscr;
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFGregorianDateGetAbsoluteTime(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFGregorianDateGetAbsoluteTime", 0x23a24, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 packed = CFX_ARG(1), timezone = CFX_ARG(4);
    service_date value = {(int32)CFX_ARG(0),    (int8)(packed >> 24),
                          (int8)(packed >> 16), (int8)(packed >> 8),
                          (int8)packed,         date_value(((uint64)CFX_ARG(2) << 32) | CFX_ARG(3))};
    if (!date_supported(call_frame, timezone, value.second) || value.month < 1 || value.month > 12)
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    double result;
    if (!date_absolute(&context, &value, &result))
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    call_frame->fpscr = context.call_frame.fpscr;
    return cfx_double_result(call_frame, result);
}

int ROMlib_cfx_CFGregorianDateIsValid(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFGregorianDateIsValid", 0x23918, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    int32 year = CFX_ARG(0);
    uint32 packed = CFX_ARG(1);
    int month = (int8)(packed >> 24), day = (int8)(packed >> 16);
    int hour = (int8)(packed >> 8), minute = (int8)packed;
    double second = date_value(((uint64)CFX_ARG(2) << 32) | CFX_ARG(3));
    if (year <= 0 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour >= 24 || minute < 0 ||
        minute >= 60)
        return CFX_RESULT(0);
    if (call_frame->fpscr & 0xf8)
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    service_date_context context = {*call_frame, 1};
    unsigned below_zero = date_compare(&context, second, 0, 1) & 8;
    unsigned at_least_sixty = !below_zero && (date_compare(&context, 60, second, 1) & (8 | 2));
    call_frame->fpscr = context.call_frame.fpscr;
    return CFX_RESULT(!below_zero && !at_least_sixty && day <= date_month_days(year, month));
}
