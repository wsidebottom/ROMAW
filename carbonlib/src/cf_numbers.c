/*+===================================================================
  File:      cf_numbers.c

  Summary:   Native CFNumber public control flow. The real 1.4 conversion worker at 0x2473c retains PowerPC
             narrowing/rounding semantics until it is migrated.

  Classes:   None.

  Functions: ROMlib_cfnumber_compare, ROMlib_cfnumber_create, ROMlib_cfnumber_value

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native CFNumber public control flow. The real 1.4 conversion worker at
   0x2473c retains PowerPC narrowing/rounding semantics until it is migrated. */
#include "cf_batch.h"

static int number_mapping(const ROMlib_carbon_reference* reference, uint32 type, uint32* storage, uint32* input)
{
    if (!type || type > 14)
        return cfp_failure("Native Carbon CFNumber type is invalid");
    const uint8* table = ROMlib_carbon_guest_memory(US_TO_SYN68K(reference->toc) + 0x35a4, 30, 0);
    if (!table)
        return 0;
    *input = table[type];
    *storage = table[15 + type];
    return 1;
}

static unsigned number_size(uint32 type)
{
    static const unsigned sizes[] = {0, 1, 2, 4, 8, 4, 8};
    return type < NELEM(sizes) ? sizes[type] : 0;
}

uint32 ROMlib_cfnumber_create(uint32 allocator, uint32 type, uint32 value)
{
    uint32 arguments[] = {allocator, type, value}, result = 0, storage, input;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFNumberCreate", 0x24d58, arguments, 3, &reference, &result))
        return result;
    if (!number_mapping(&reference, type, &storage, &input))
        return 0;
    if (!allocator && !cfp_call("CFAllocatorGetDefault", NULL, 0, &allocator))
        return 0;
    uint32 create[] = {allocator, cfp_toc(&reference, 0x920), number_size(storage), 0};
    if (!cfp_call("_CFRuntimeCreateInstance", create, 4, &result) || !result)
        return 0;
    if (input == 1) {
        const int8* source = ROMlib_carbon_guest_memory(value, 1, 0);
        if (!source || !cfp_write(result + 8, (int32)*source))
            return 0;
    }
    else if (input == 2) {
        const void* source = ROMlib_carbon_guest_memory(value, 2, 0);
        uint16 encoded;
        if (!source)
            return 0;
        memcpy(&encoded, source, 2);
        if (!cfp_write(result + 8, (int32)(int16)CW(encoded)))
            return 0;
    }
    else if (!cfp_move(result + 8, value, number_size(input)))
        return 0;
    return cfp_write(result + 4, (cfp_read(result + 4) & ~0x7fU) | storage) ? result : 0;
}

static int number_convert(const ROMlib_carbon_reference* reference, uint32 source, uint32 source_type,
                          uint32 target_type, uint32 destination)
{
    uint32 arguments[] = {source, source_type, target_type, destination}, ignored;
    return cfp_private(reference, 0x2473c, arguments, 4, &ignored);
}

static double number_double(const uint32 words[2])
{
    uint64 bits = (uint64)CL(words[0]) << 32 | CL(words[1]);
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

uint32 ROMlib_cfnumber_value(uint32 object, uint32 type, uint32 destination)
{
    uint32 arguments[] = {object, type, destination}, result = 0, storage, input, flags;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFNumberGetValue", 0x24fcc, arguments, 3, &reference, &result))
        return result;
    if (!number_mapping(&reference, type, &storage, &input) || !cfp_get(object + 4, &flags))
        return 0;
    uint32 source_type = flags & 31;
    if (source_type < 3 || source_type > 6)
        return cfp_failure("Native Carbon CFNumber storage type is invalid");
    _Alignas(16) uint32 scratch[2] = {0, 0}, canonical[2] = {0, 0};
    uint32 output = destination ? destination : US_TO_SYN68K(scratch);
    if (!number_convert(&reference, object + 8, source_type, input, output))
        return 0;
    /* The original reports canonical same-type conversions as exact, even when
       the caller selected an 8/16-bit alias which truncates the stored int32. */
    if (source_type == storage)
        return 1;
    if (input == 1 || input == 2) {
        const void* memory = ROMlib_carbon_guest_memory(output, number_size(input), 0);
        if (!memory)
            return 0;
        int32 value;
        if (input == 1)
            value = *(const int8*)memory;
        else {
            uint16 encoded;
            memcpy(&encoded, memory, 2);
            value = (int16)CW(encoded);
        }
        canonical[0] = CL(value);
    }
    else if (!cfp_move(US_TO_SYN68K(canonical), output, number_size(input)))
        return 0;
    _Alignas(16) uint32 converted[2] = {0, 0}, original[2] = {0, 0};
    uint32 comparison_type = source_type >= 5 || storage >= 5 ? 6 : 4;
    if (!number_convert(&reference, US_TO_SYN68K(canonical), storage, comparison_type, US_TO_SYN68K(converted)) ||
        !number_convert(&reference, object + 8, source_type, comparison_type, US_TO_SYN68K(original)))
        return 0;
    if (comparison_type == 6)
        return number_double(converted) == number_double(original);
    return converted[0] == original[0] && converted[1] == original[1];
}

uint32 ROMlib_cfnumber_compare(uint32 first, uint32 second, uint32 context)
{
    uint32 arguments[] = {first, second, context}, result = 0, first_flags, second_flags;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFNumberCompare", 0x25428, arguments, 3, &reference, &result))
        return result;
    if (!cfp_get(first + 4, &first_flags) || !cfp_get(second + 4, &second_flags))
        return 0;
    uint32 first_type = first_flags & 31, second_type = second_flags & 31;
    if (first_type < 3 || first_type > 6 || second_type < 3 || second_type > 6)
        return cfp_failure("Native Carbon CFNumber comparison storage type is invalid");
    uint32 comparison_type = first_type >= 5 || second_type >= 5 ? 6 : 4;
    _Alignas(16) uint32 a[2] = {0, 0}, b[2] = {0, 0};
    if (!number_convert(&reference, first + 8, first_type, comparison_type, US_TO_SYN68K(a)) ||
        !number_convert(&reference, second + 8, second_type, comparison_type, US_TO_SYN68K(b)))
        return 0;
    if (comparison_type == 6) {
        double x = number_double(a), y = number_double(b);
        /* The 1.4 branches test greater/less CR bits separately. Unordered
           operands therefore compare equal, as do both signs of zero. */
        return x > y ? 1 : x < y ? (uint32)-1 : 0;
    }
    int32 ah = CL(a[0]), bh = CL(b[0]);
    uint32 al = CL(a[1]), bl = CL(b[1]);
    return ah > bh || (ah == bh && al > bl) ? 1 : ah < bh || (ah == bh && al < bl) ? (uint32)-1 : 0;
}
