/*+===================================================================
  File:      script_utilities.h

  Summary:   Implements Script Manager utilities with PowerPC callbacks and records.

  Classes:   None.

  Functions: Private CarbonLib frame adapters.

  Origin:    CarbonLib 1.4 exports, Mac OS 9.2 ScriptUtils and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_SCRIPT_UTILITIES_H
#define ROMLIB_CARBON_SCRIPT_UTILITIES_H

#include "rsys/osevent.h"

static int carbon_IsCmdChar(ROMlib_carbon_ppc_frame* call)
{
    const EventRecord* event = ROMlib_carbon_guest_memory(call->words[0], sizeof *event, 0);
    if (!event)
        return 0;
    const Ptr keyboard = ROMlib_kchr_ptr();
    const int16_t expected = (int16_t)call->words[1];
    const uint16_t key = (CL(event->message) >> 8) & 0xff;
    const uint16_t original = key | (CW(event->modifiers) & 0xff00);
    const uint16_t without_command = original & ~cmdKey;
    const uint16_t without_shift = without_command ^ shiftKey;
    const uint16_t variants[] = {original, without_command, without_shift, without_shift | cmdKey};
    call->words[0] = false;
    for (unsigned i = 0; keyboard && i < NELEM(variants); ++i) {
        LONGINT state = 0;
        if ((int8_t)C_KeyTrans(keyboard, variants[i], &state) == expected) {
            call->words[0] = true;
            break;
        }
    }
    return 1;
}

static void script_reverse_order(uint16_t* order, unsigned first, unsigned last)
{
    while (first < last) {
        uint16_t swap = order[first];
        order[first++] = order[last];
        order[last--] = swap;
    }
}

static int carbon_GetFormatOrder(ROMlib_carbon_ppc_frame* call)
{
    const int first = (int16_t)call->words[1], last = (int16_t)call->words[2];
    if (first < 0 || last < first)
        return 1;
    const unsigned count = last - first + 1;
    const uint32_t procedure = call->words[4], context = call->words[5];
    const bool right = (uint8_t)call->words[3] != 0;
    uint16_t* order = ROMlib_carbon_guest_memory(call->words[0], count * sizeof *order, 1);
    if (!order)
        return 0;
    int run = -1;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned index = right ? count - 1 - i : i;
        order[index] = CW(first + i);
        uint32_t direction = right;
        const ROMlib_carbon_ppc_frame arguments = {.words = {first + i, context}};
        if (procedure && !ROMlib_carbon_invoke_pascal(procedure, 0x390, &arguments, &direction))
            return 0;
        if (((uint8_t)direction != 0) != right) {
            if (run < 0)
                run = i;
        }
        else if (run >= 0) {
            script_reverse_order(order, right ? count - i : (unsigned)run, right ? count - 1 - run : i - 1);
            run = -1;
        }
    }
    if (run >= 0)
        script_reverse_order(order, right ? 0 : (unsigned)run, right ? count - 1 - run : count - 1);
    return 1;
}

static int carbon_HiliteText(ROMlib_carbon_ppc_frame* call)
{
    if (C_FontScript() != smRoman) {
        ROMlib_carbon_reference saved;
        return ROMlib_carbon_reference_function("CarbonLib", "HiliteText", &saved) &&
               ROMlib_carbon_invoke_frame(saved.function, call);
    }
    const int length = MAX(0, (int16_t)call->words[1]);
    const int first = (int16_t)call->words[2], last = (int16_t)call->words[3];
    const uint16_t ranges[6] = {CW(MAX(0, MIN(first, length))), CW(MAX(0, MIN(last, length)))};
    void* output = ROMlib_carbon_guest_memory(call->words[4], sizeof ranges, 1);
    if (!output)
        return 0;
    memcpy(output, ranges, sizeof ranges);
    return 1;
}

#endif
