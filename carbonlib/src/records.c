/*+===================================================================
  File:      records.c

  Summary:   Classic List, Control, Menu and Dialog record accessors from CarbonLib 1.4. Addresses, pointers and packed
             Point values use raw PPC guest words. Each dereference translates afresh; a 32-bit guest pointer is never a
             host pointer.

  Classes:   None.

  Functions: ROMlib_carbon_GetControlDataHandle, ROMlib_carbon_GetControlHilite, ROMlib_carbon_GetControlOwner,
             ROMlib_carbon_GetControlPopupMenuHandle, ROMlib_carbon_GetControlPopupMenuID,
             ROMlib_carbon_GetDialogCancelItem, ROMlib_carbon_GetDialogKeyboardFocusItem, ROMlib_carbon_GetDialogPort,
             ROMlib_carbon_GetListActive, ROMlib_carbon_GetListCellIndent, ROMlib_carbon_GetListCellSize,
             ROMlib_carbon_GetListClickLocation, ROMlib_carbon_GetListClickLoop, ROMlib_carbon_GetListClickTime,
             ROMlib_carbon_GetListDataBounds, ROMlib_carbon_GetListDataHandle, ROMlib_carbon_GetListDefinition,
             ROMlib_carbon_GetListFlags, ROMlib_carbon_GetListHorizontalScrollBar, ROMlib_carbon_GetListMouseLocation,
             ROMlib_carbon_GetListPort, ROMlib_carbon_GetListRefCon, ROMlib_carbon_GetListSelectionFlags,
             ROMlib_carbon_GetListUserHandle, ROMlib_carbon_GetListVerticalScrollBar, ROMlib_carbon_GetListViewBounds,
             ROMlib_carbon_GetListVisibleCells, ROMlib_carbon_GetMenuHeight, ROMlib_carbon_GetMenuTitle,
             ROMlib_carbon_GetMenuWidth, ROMlib_carbon_IsControlHilited, ROMlib_carbon_SetControlBounds,
             ROMlib_carbon_SetControlDataHandle, ROMlib_carbon_SetControlPopupMenuHandle,
             ROMlib_carbon_SetControlPopupMenuID, ROMlib_carbon_SetListCellIndent, ROMlib_carbon_SetListClickLoop,
             ROMlib_carbon_SetListClickTime, ROMlib_carbon_SetListFlags, ROMlib_carbon_SetListLastClick,
             ROMlib_carbon_SetListPort, ROMlib_carbon_SetListRefCon, ROMlib_carbon_SetListSelectionFlags,
             ROMlib_carbon_SetListUserHandle, ROMlib_carbon_SetListViewBounds, ROMlib_carbon_SetMenuHeight,
             ROMlib_carbon_SetMenuID, ROMlib_carbon_SetMenuWidth

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic List, Control, Menu and Dialog record accessors from CarbonLib 1.4.
   Addresses, pointers and packed Point values use raw PPC guest words. Each
   dereference translates afresh; a 32-bit guest pointer is never a host pointer. */
#include "internal.h"
#include "rsys/wind.h"

static uint32 record_read(uint32 address, unsigned width, unsigned sign_extend)
{
    const void* memory = ROMlib_carbon_guest_memory(address, width, 0);
    if (!memory)
        return 0;
    if (width == 1) {
        Byte value = *(const Byte*)memory;
        return sign_extend ? (uint32)(int32)(int8)value : value;
    }
    if (width == 2) {
        uint16 value;
        memcpy(&value, memory, 2);
        return sign_extend ? (uint32)(int32)(int16)CW(value) : CW(value);
    }
    uint32 value;
    memcpy(&value, memory, 4);
    return CL(value);
}

static void record_write(uint32 address, unsigned width, uint32 value)
{
    void* memory = ROMlib_carbon_guest_memory(address, width, 1);
    if (!memory)
        return;
    if (width == 1)
        *(Byte*)memory = (Byte)value;
    else if (width == 2) {
        uint16 word = CW((uint16)value);
        memcpy(memory, &word, 2);
    }
    else {
        value = CL(value);
        memcpy(memory, &value, 4);
    }
}

/* The original Rect copies consist of ordered 32-bit loads/stores. Preserve
   that order when the output aliases the record, rather than using memmove. */
static void record_copy(uint32 source, uint32 destination, unsigned width)
{
    for (unsigned i = 0; i < width; i += 4) {
        uint32 word;
        const void* input = ROMlib_carbon_guest_memory(source + i, 4, 0);
        if (!input)
            return;
        memcpy(&word, input, 4);
        void* output = ROMlib_carbon_guest_memory(destination + i, 4, 1);
        if (!output)
            return;
        memcpy(output, &word, 4);
    }
}

/* CarbonLib 1.4 code 0x7570c. */
uint32 ROMlib_carbon_GetListPort(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 8, 4, 0);
}

/* CarbonLib 1.4 code 0x75760. */
uint32 ROMlib_carbon_GetListVerticalScrollBar(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 28, 4, 0);
}

/* CarbonLib 1.4 code 0x7576c. */
uint32 ROMlib_carbon_GetListHorizontalScrollBar(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 32, 4, 0);
}

/* CarbonLib 1.4 code 0x75778. */
uint32 ROMlib_carbon_GetListFlags(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 39, 1, 1);
}

/* CarbonLib 1.4 code 0x75788. */
uint32 ROMlib_carbon_GetListSelectionFlags(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 36, 1, 1);
}

/* CarbonLib 1.4 code 0x75798. */
uint32 ROMlib_carbon_GetListActive(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 37, 1, 0);
}

/* CarbonLib 1.4 code 0x757a4. */
uint32 ROMlib_carbon_GetListClickTime(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 40, 4, 0);
}

/* CarbonLib 1.4 code 0x757d8. */
uint32 ROMlib_carbon_GetListClickLoop(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 52, 4, 0);
}

/* CarbonLib 1.4 code 0x757e4. */
uint32 ROMlib_carbon_GetListRefCon(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 60, 4, 0);
}

/* CarbonLib 1.4 code 0x757f0. */
uint32 ROMlib_carbon_GetListDefinition(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 64, 4, 0);
}

/* CarbonLib 1.4 code 0x757fc. */
uint32 ROMlib_carbon_GetListUserHandle(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 68, 4, 0);
}

/* CarbonLib 1.4 code 0x75828. */
uint32 ROMlib_carbon_GetListDataHandle(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 80, 4, 0);
}

/* CarbonLib 1.4 code 0x756f0. */
uint32 ROMlib_carbon_GetListViewBounds(uint32 handle, uint32 output)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(record + 0, output, 8);
    return output;
}

/* CarbonLib 1.4 code 0x75718. */
uint32 ROMlib_carbon_GetListCellIndent(uint32 handle, uint32 output)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(record + 12, output, 4);
    return output;
}

/* CarbonLib 1.4 code 0x7572c. */
uint32 ROMlib_carbon_GetListCellSize(uint32 handle, uint32 output)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(record + 16, output, 4);
    return output;
}

/* CarbonLib 1.4 code 0x75740. */
uint32 ROMlib_carbon_GetListVisibleCells(uint32 handle, uint32 output)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(record + 20, output, 8);
    return output;
}

/* CarbonLib 1.4 code 0x757b0. */
uint32 ROMlib_carbon_GetListClickLocation(uint32 handle, uint32 output)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(record + 44, output, 4);
    return output;
}

/* CarbonLib 1.4 code 0x757c4. */
uint32 ROMlib_carbon_GetListMouseLocation(uint32 handle, uint32 output)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(record + 48, output, 4);
    return output;
}

/* CarbonLib 1.4 code 0x75808. */
uint32 ROMlib_carbon_GetListDataBounds(uint32 handle, uint32 output)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(record + 72, output, 8);
    return output;
}

/* CarbonLib 1.4 code 0x75834. */
void ROMlib_carbon_SetListViewBounds(uint32 handle, uint32 value)
{
    uint32 record = record_read(handle, 4, 0);
    record_copy(value, record + 0, 8);
}

/* CarbonLib 1.4 code 0x75858. */
void ROMlib_carbon_SetListCellIndent(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_copy(value, record + 12, 4);
}

/* CarbonLib 1.4 code 0x75898. */
void ROMlib_carbon_SetListLastClick(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_copy(value, record + 56, 4);
}

/* CarbonLib 1.4 code 0x7584c. */
void ROMlib_carbon_SetListPort(uint32 handle, uint32 value)
{
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 8, 4, value);
}

/* CarbonLib 1.4 code 0x75870. */
void ROMlib_carbon_SetListClickTime(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 40, 4, value);
}

/* CarbonLib 1.4 code 0x75884. */
void ROMlib_carbon_SetListClickLoop(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 52, 4, value);
}

/* CarbonLib 1.4 code 0x758b0. */
void ROMlib_carbon_SetListRefCon(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 60, 4, value);
}

/* CarbonLib 1.4 code 0x758c4. */
void ROMlib_carbon_SetListFlags(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 39, 1, value);
}

/* CarbonLib 1.4 code 0x758d8. */
void ROMlib_carbon_SetListSelectionFlags(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 36, 1, value);
}

/* CarbonLib 1.4 code 0x758ec. */
void ROMlib_carbon_SetListUserHandle(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 68, 4, value);
}

/* CarbonLib 1.4 code 0x72914. */
uint32 ROMlib_carbon_GetControlHilite(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 17, 1, 0);
}

/* CarbonLib 1.4 code 0x72900. */
uint32 ROMlib_carbon_IsControlHilited(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 17, 1, 0) != 0;
}

/* CarbonLib 1.4 code 0x72920. */
uint32 ROMlib_carbon_GetControlDataHandle(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 28, 4, 0);
}

/* CarbonLib 1.4 code 0x7292c. */
uint32 ROMlib_carbon_GetControlOwner(uint32 handle)
{
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 4, 4, 0);
}

/* CarbonLib 1.4 code 0x72990. */
void ROMlib_carbon_SetControlBounds(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_copy(value, record + 8, 8);
}

/* CarbonLib 1.4 code 0x729b4. */
void ROMlib_carbon_SetControlDataHandle(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 28, 4, value);
}

/* CarbonLib 1.4 code 0x72938. */
uint32 ROMlib_carbon_GetControlPopupMenuHandle(uint32 handle)
{
    if (!handle)
        return 0;
    uint32 record = record_read(handle, 4, 0);
    uint32 data = record_read(record + 28, 4, 0);
    if (!data)
        return 0;
    record = record_read(data, 4, 0);
    return record_read(record + 0, 4, 0);
}

/* CarbonLib 1.4 code 0x72964. */
uint32 ROMlib_carbon_GetControlPopupMenuID(uint32 handle)
{
    if (!handle)
        return 0;
    uint32 record = record_read(handle, 4, 0);
    uint32 data = record_read(record + 28, 4, 0);
    if (!data)
        return 0;
    record = record_read(data, 4, 0);
    return record_read(record + 4, 2, 1);
}

/* CarbonLib 1.4 code 0x729c8. */
void ROMlib_carbon_SetControlPopupMenuHandle(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    uint32 data = record_read(record + 28, 4, 0);
    if (!data)
        return;
    record = record_read(data, 4, 0);
    record_write(record + 0, 4, value);
}

/* CarbonLib 1.4 code 0x729ec. */
void ROMlib_carbon_SetControlPopupMenuID(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    uint32 data = record_read(record + 28, 4, 0);
    if (!data)
        return;
    record = record_read(data, 4, 0);
    record_write(record + 4, 2, value);
}

/* CarbonLib 1.4 code 0x75260. */
uint32 ROMlib_carbon_GetMenuWidth(uint32 handle)
{
    if (!handle)
        return 0;
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 2, 2, 1);
}

/* CarbonLib 1.4 code 0x7527c. */
uint32 ROMlib_carbon_GetMenuHeight(uint32 handle)
{
    if (!handle)
        return 0;
    uint32 record = record_read(handle, 4, 0);
    return record_read(record + 4, 2, 1);
}

/* CarbonLib 1.4 code 0x7530c. */
void ROMlib_carbon_SetMenuID(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 0, 2, value);
}

/* CarbonLib 1.4 code 0x75320. */
void ROMlib_carbon_SetMenuWidth(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 2, 2, value);
}

/* CarbonLib 1.4 code 0x75334. */
void ROMlib_carbon_SetMenuHeight(uint32 handle, uint32 value)
{
    if (!handle)
        return;
    uint32 record = record_read(handle, 4, 0);
    record_write(record + 4, 2, value);
}

/* CarbonLib 1.4 code 0x75298. */
uint32 ROMlib_carbon_GetMenuTitle(uint32 handle, uint32 output)
{
    if (!handle || !output)
        return output;
    uint32 record = record_read(handle, 4, 0);
    unsigned length = record_read(record + 14, 1, 0);
    const void* input = ROMlib_carbon_guest_memory(record + 14, length + 1, 0);
    void* destination = ROMlib_carbon_guest_memory(output, length + 1, 1);
    if (!input || !destination)
        return output;
    memmove(destination, input, length + 1);
    if (record_read(output, 2, 0) == 0x0501)
        record_write(output, 1, 0);
    return output;
}

/* CarbonLib 1.4 code 0x72a24. */
uint32 ROMlib_carbon_GetDialogCancelItem(uint32 handle)
{
    const ROMlib_window_metadata* metadata =
        handle ? ROMlib_window_get_metadata((WindowPtr)SYN68K_TO_US(handle)) : NULL;
    if (metadata)
        return metadata->dialog_options & ROMLIB_DIALOG_CANCEL_ITEM;
    return record_read(handle + 166, 2, 1);
}

/* CarbonLib 1.4 code 0x72a2c. */
uint32 ROMlib_carbon_GetDialogKeyboardFocusItem(uint32 handle)
{
    if (!handle)
        return UINT32_MAX;
    int16 field = (int16)record_read(handle + 164, 2, 1);
    return field < 0 ? UINT32_MAX : (uint32)(int32)(int16)(field + 1);
}

/* CarbonLib 1.4 code 0x72a88. */
uint32 ROMlib_carbon_GetDialogPort(uint32 handle)
{
    return handle;
}
