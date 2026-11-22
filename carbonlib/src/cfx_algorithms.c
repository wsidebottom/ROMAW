/*+===================================================================
  File:      cfx_algorithms.c

  Summary:   Implements CoreFoundation algorithms service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFBSearch, ROMlib_cfx_CFHashBytes, ROMlib_cfx_CFMergeSortArray,
             ROMlib_cfx_CFQSortArray, ROMlib_carbon_stdlib_qsort

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

#include <stdlib.h>

static int sort_array(const ROMlib_carbon_reference* reference, ROMlib_carbon_ppc_frame* call_frame,
                      unsigned compare_index, int has_context)
{
    uint32 base = CFX_ARG(0), count = CFX_ARG(1), size = CFX_ARG(2);
    if (count < 2 || (has_context && (int32)count < 0) || !size)
        return 1;
    uint64 extent = (uint64)count * size;
    if (extent > INT32_MAX)
        return cfp_failure("Native Carbon sort extent overflow");
    uint32 comparator = CFX_ARG(compare_index), context = has_context ? CFX_ARG(compare_index + 1) : 0;
    if (!comparator)
        return cfp_failure("Native Carbon sort comparator is missing");
    if (!ROMlib_carbon_guest_memory(base, extent, 1))
        return 0;
    /* Scratch bytes stay on the host; no guest callback receives this pointer.
       Only the original guest element addresses are passed to comparators. */
    Byte* scratch = malloc((size_t)extent);
    if (!scratch)
        return reference ? ROMlib_carbon_invoke_frame(reference->function, call_frame)
                         : cfp_failure("Could not allocate guest sort scratch storage");
    int ok = 0;
    for (uint64 width = 1; width < count; width *= 2) {
        for (uint64 first = 0; first < count; first += width * 2) {
            uint64 middle = MIN(first + width, count), last = MIN(first + width * 2, count);
            uint64 left = first, right = middle, output = first;
            while (left < middle || right < last) {
                int take_left = right == last;
                if (left < middle && right < last) {
                    uint32 arguments[] = {cfx_address(base, left * size, size), cfx_address(base, right * size, size),
                                          context};
                    uint32 order = 0;
                    if (!cfp_callback(comparator, arguments, has_context ? 3 : 2, &order))
                        goto done;
                    take_left = (int32)order <= 0;
                }
                uint64 source = take_left ? left++ : right++;
                const void* input = ROMlib_carbon_guest_memory(cfx_address(base, source * size, size), size, 0);
                if (!input)
                    goto done;
                memcpy(scratch + output++ * size, input, size);
            }
        }
        void* output = ROMlib_carbon_guest_memory(base, extent, 1);
        if (!output)
            goto done;
        memcpy(output, scratch, (size_t)extent);
    }
    ok = 1;
done:
    free(scratch);
    return ok;
}

int ROMlib_cfx_CFBSearch(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFBSearch", 0x1a124, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 key = CFX_ARG(0), size = CFX_ARG(1), base = CFX_ARG(2), count = CFX_ARG(3);
    uint32 comparator = CFX_ARG(4), context = CFX_ARG(5);
    if ((int32)count <= 0)
        return CFX_RESULT(0);
    if ((int32)size < 0 || !comparator)
        return cfp_failure("Native Carbon binary search requires a comparator and nonnegative element size");
    uint32 arguments[] = {0, key, context}, order = 0;
    if (count < 4) {
        while (count) {
            arguments[0] = cfx_address(base, (uint64)(count - 1) * size, size);
            if (!cfp_callback(comparator, arguments, 3, &order))
                return 0;
            if ((int32)order < 0)
                break;
            --count;
        }
        return CFX_RESULT(count);
    }
    arguments[0] = cfx_address(base, (uint64)(count - 1) * size, size);
    if (!cfp_callback(comparator, arguments, 3, &order))
        return 0;
    if ((int32)order < 0)
        return CFX_RESULT(count);
    /* The low endpoint comparison reverses the operands. Guest comparators
       can observe both the order of calls and the addresses passed. */
    arguments[0] = key;
    arguments[1] = base;
    if (!cfp_callback(comparator, arguments, 3, &order))
        return 0;
    if ((int32)order < 0)
        return CFX_RESULT(0);
    uint32 step = 1;
    while (step <= count / 2)
        step *= 2;
    arguments[0] = cfx_address(base, (uint64)(step - 1) * size, size);
    arguments[1] = key;
    if (!cfp_callback(comparator, arguments, 3, &order))
        return 0;
    int32 previous = (int32)order < 0 ? (int32)(count - step) : -1;
    while ((step /= 2) != 0) {
        int32 index = previous + (int32)step;
        arguments[0] = cfx_address(base, (uint64)index * size, size);
        if (!cfp_callback(comparator, arguments, 3, &order))
            return 0;
        if ((int32)order < 0)
            previous = index;
    }
    return CFX_RESULT(previous + 1);
}

int ROMlib_cfx_CFHashBytes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFHashBytes", 0x1a838, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 bytes = CFX_ARG(0), length = CFX_ARG(1), hash = 0;
    if ((int32)length <= 0)
        return CFX_RESULT(0);
    const Byte* input = ROMlib_carbon_guest_memory(bytes, length, 0);
    if (!input)
        return 0;
    for (uint32 index = 0; index < length; ++index) {
        hash = (hash << 4) + input[index];
        uint32 high = hash & 0xf0000000;
        if (high)
            hash ^= high >> 24;
        hash &= ~high;
    }
    return CFX_RESULT(hash);
}

int ROMlib_cfx_CFMergeSortArray(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFMergeSortArray", 0x4e938, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return sort_array(&reference, call_frame, 4, 1);
}

int ROMlib_cfx_CFQSortArray(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFQSortArray", 0x4e960, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return sort_array(&reference, call_frame, 3, 1);
}

int ROMlib_carbon_stdlib_qsort(ROMlib_carbon_ppc_frame* call_frame)
{
    return sort_array(NULL, call_frame, 3, 0);
}
