/*+===================================================================
  File:      type_selection.h

  Summary:   Binds classic Script Manager type selection to guest PPC callers.

  Classes:   None.

  Functions: carbon_TypeSelectClear, carbon_TypeSelectNewKey,
             carbon_TypeSelectCompare, carbon_TypeSelectFindItem,
             carbon_CharacterType.

  Origin:    Executor Script Manager and the CarbonLib 1.4 export directory.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_TYPE_SELECTION_H
#define ROMAW_CARBON_TYPE_SELECTION_H

#include "rsys/typeselect.h"

static int carbon_TypeSelectClear(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_type_select_record* record =
        call->words[0] ? ROMlib_carbon_guest_memory(call->words[0], sizeof *record, 1) : NULL;
    if (call->words[0] && !record)
        return 0;
    ROMlib_type_select_clear(record);
    return 1;
}

static int carbon_TypeSelectNewKey(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    EventRecord* event = args[0] ? ROMlib_carbon_guest_memory(args[0], sizeof *event, 0) : NULL;
    ROMlib_type_select_record* record = args[1] ? ROMlib_carbon_guest_memory(args[1], sizeof *record, 1) : NULL;
    if ((args[0] && !event) || (args[1] && !record))
        return 0;
    call->words[0] = ROMlib_type_select_key(event, record);
    return 1;
}

static int carbon_TypeSelectCompare(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const ROMlib_type_select_record* record = args[0] ? ROMlib_carbon_guest_memory(args[0], sizeof *record, 0) : NULL;
    StringPtr string = args[2] ? ROMlib_carbon_guest_memory(args[2], 1, 0) : NULL;
    if ((args[0] && !record) || (args[2] && (!string || !ROMlib_carbon_guest_memory(args[2], *string + 1, 0))))
        return 0;
    call->words[0] = (int32_t)ROMlib_type_select_compare(record, (int16_t)args[1], string);
    return 1;
}

static int carbon_TypeSelectFindItem(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const ROMlib_type_select_record* record = args[0] ? ROMlib_carbon_guest_memory(args[0], sizeof *record, 0) : NULL;
    ProcPtr callback = args[3] ? ROMlib_carbon_guest_memory(args[3], 2, 0) : NULL;
    if ((args[0] && !record) || (args[3] && !callback))
        return 0;
    call->words[0] = (int32_t)ROMlib_type_select_find(record, (int16_t)args[1], (int16_t)args[2], callback,
                                                      args[4] ? SYN68K_TO_US(args[4]) : NULL);
    return 1;
}

static int carbon_CharacterType(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const int16_t script = (int16_t)args[2];
    if (script == smRoman || script == smSystemScript) {
        const int64_t address = (int64_t)args[0] + (int16_t)args[1];
        if (address < 0 || address > UINT32_MAX)
            return 0;
        Ptr character = ROMlib_carbon_guest_memory((uint32_t)address, 1, 0);
        if (!character)
            return 0;
        if (*(const Byte*)character < 0x80) {
            call->words[0] = (int32_t)C_CharType(character, 0);
            return 1;
        }
    }
    ROMlib_carbon_reference saved;
    return ROMlib_carbon_reference_function("CarbonLib", "CharacterType", &saved) &&
           ROMlib_carbon_invoke_frame(saved.function, call);
}

#endif
