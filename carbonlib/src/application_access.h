/*+===================================================================
  File:      application_access.h

  Summary:   Implements Carbon menu, window and Resource Manager accessors.

  Classes:   None.

  Functions: Private frame and word-argument adapters for the export registry.

  Origin:    CarbonLib 1.4 code, System 9.2.1 WindowsLib, and Executor's
             live Menu, Window, Resource and Memory Manager state.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_APPLICATION_ACCESS_H
#define ROMAW_CARBON_APPLICATION_ACCESS_H

#include "internal.h"
#include "rsys/cquick.h"
#include "rsys/wind.h"

static void* carbon_application_handle(uint32_t address, unsigned extent)
{
    const uint32_t* master = address ? ROMlib_carbon_guest_memory(address, 4, 0) : NULL;
    return master && *master ? ROMlib_carbon_guest_memory(CL(*master), extent, 0) : NULL;
}

static int carbon_GetMenuID(ROMlib_carbon_ppc_frame* call)
{
    const MenuInfo* menu = carbon_application_handle(call->words[0], offsetof(MenuInfo, menuData));
    call->words[0] = menu ? CW(menu->menuID) : 0;
    return 1;
}

static int carbon_SetMenuTitle(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t menu = call->words[0], title_address = call->words[1];
    const unsigned title_offset = offsetof(MenuInfo, menuData);
    const MenuInfo* info = carbon_application_handle(menu, title_offset + 1);
    if (!info) {
        call->words[0] = (uint32_t)-5623; /* menuInvalidErr */
        return 1;
    }
    if (!title_address) {
        call->words[0] = (uint32_t)paramErr;
        return 1;
    }
    const uint8_t* length = ROMlib_carbon_guest_memory(title_address, 1, 0);
    if (!length)
        return 0;
    const size_t extent = (size_t)*length + 1;
    const void* title = ROMlib_carbon_guest_memory(title_address, extent, 0);
    if (!title)
        return 0;

    /* Munger can relocate the menu. Copy the Pascal text before resizing,
       including when it is already stored inside the menu's data handle. */
    _Alignas(256) uint8_t saved[256];
    memcpy(saved, title, extent);
    const int16_t id = CW(info->menuID);
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = menu;
    arguments.words[1] = title_offset;
    arguments.words[3] = (uint8_t)info->menuData[0] + 1;
    arguments.words[4] = US_TO_SYN68K(saved);
    arguments.words[5] = extent;
    uint32_t result;
    if (!ROMlib_carbon_call_toolbox_vector(0x1580, 0x3fff0, 0, &arguments, &result))
        return 0;
    if ((int32_t)result < 0) {
        call->words[0] = (uint32_t)memFullErr;
        return 1;
    }
    if (!ROMlib_carbon_call_toolbox_vector(0x1320, 0xc0, 0, &arguments, &result))
        return 0;
    arguments.words[0] = id;
    if (!ROMlib_carbon_call_toolbox_vector(0x1324, 0xb0, 0, &arguments, &result))
        return 0;
    if (result == menu && !ROMlib_carbon_call_toolbox_vector(0x0e74, 0, 0, &arguments, &result))
        return 0;
    call->words[0] = noErr;
    return 1;
}

static int16_t carbon_GetWindowKind(WindowPtr window)
{
    return ROMlib_valid_window(window) ? WINDOW_KIND(window) : 0;
}

static void carbon_SetWindowKind(WindowPtr window, int16_t kind)
{
    if (ROMlib_valid_window(window))
        WINDOW_KIND_X(window) = CW(kind);
}

static BOOLEAN carbon_IsWindowVisible(WindowPtr window)
{
    return ROMlib_valid_window(window) && WINDOW_VISIBLE(window);
}

static int32_t carbon_GetWindowTitleLength(WindowPtr window, uint32_t* length)
{
    if (!ROMlib_valid_window(window))
        return -5600; /* errInvalidWindowRef */
    if (!length)
        return paramErr;
    const StringHandle title = (StringHandle)WINDOW_TITLE(window);
    *length = CL(title && STARH(title) ? *STARH(title) : 0);
    return noErr;
}

static int carbon_window_region(ROMlib_carbon_ppc_frame* call, int bounds_only)
{
    const uint32_t window_address = call->words[0], output_address = call->words[2];
    const uint16_t code = call->words[1];
    const WindowPtr window =
        window_address ? ROMlib_carbon_guest_memory(window_address, sizeof(WindowRecord), 0) : NULL;
    int32_t error = bounds_only ? -5600 : paramErr; /* errInvalidWindowRef */
    if (!ROMlib_valid_window(window)) {
        call->words[0] = (uint32_t)error;
        return 1;
    }
    if (!output_address) {
        call->words[0] = (uint32_t)paramErr;
        return 1;
    }

    if (code != 32 && code != 33 && code != 34 && code != 40) {
        if (code < 8 || code == 35) {
            /* Title widgets and shaped-window regions belong to the WDEF. */
            ROMlib_carbon_reference reference;
            return ROMlib_carbon_reference_function(bounds_only ? "CarbonLib" : "AppearanceLib",
                                                    bounds_only ? "GetWindowBounds" : "GetWindowRegion", &reference) &&
                   ROMlib_carbon_invoke_frame(reference.function, call);
        }
        call->words[0] = (uint32_t)-30593; /* errWindowRegionCodeInvalid */
        return 1;
    }

    if ((code == 32 || code == 33) && !WINDOW_VISIBLE(window)) {
        GrafPtr saved = thePort;
        C_SetPort(MR(wmgr_port));
        WINDCALL(window, wCalcRgns, 0);
        C_SetPort(saved);
    }

    RgnHandle source = NULL;
    Rect rect;
    if (code == 40) {
        const Rect* port_bounds = &PORT_BOUNDS(window);
        rect = PORT_RECT(window);
        const int16_t h = CW(port_bounds->left), v = CW(port_bounds->top);
        rect.left = CW(CW(rect.left) - h);
        rect.right = CW(CW(rect.right) - h);
        rect.top = CW(CW(rect.top) - v);
        rect.bottom = CW(CW(rect.bottom) - v);
    }
    else {
        source = code == 32   ? WINDOW_STRUCT_REGION(window)
                 : code == 33 ? WINDOW_CONT_REGION(window)
                              : WINDOW_UPDATE_REGION(window);
        if (!source || !STARH(source)) {
            call->words[0] = (uint32_t)paramErr;
            return 1;
        }
        rect = HxX(source, rgnBBox);
    }

    if (bounds_only) {
        void* bounds = ROMlib_carbon_guest_memory(output_address, sizeof rect, 1);
        if (!bounds)
            return 0;
        memcpy(bounds, &rect, sizeof rect);
    }
    else {
        RgnHandle region = ROMlib_carbon_guest_memory(output_address, sizeof *region, 0);
        if (!region)
            return 0;
        if (source)
            C_CopyRgn(source, region);
        else
            C_RectRgn(region, &rect);
    }
    call->words[0] = noErr;
    return 1;
}

static int carbon_GetWindowBounds(ROMlib_carbon_ppc_frame* call)
{
    return carbon_window_region(call, 1);
}

static int carbon_GetWindowRegion(ROMlib_carbon_ppc_frame* call)
{
    return carbon_window_region(call, 0);
}

static int carbon_HLockHi(ROMlib_carbon_ppc_frame* call)
{
    uint32_t result;
    return ROMlib_carbon_call_register_vector(0x0590, 0x133802, 0xa064, 0, call, &result) &&
           ROMlib_carbon_call_register_vector(0x04a4, 0x133802, 0xa029, 0, call, &result);
}

static int carbon_GetIndString(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    uint8_t* length = ROMlib_carbon_guest_memory(address, 1, 1);
    if (!length)
        return 0;
    *length = 0;

    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = 0x53545223; /* STR# */
    arguments.words[1] = call->words[1];
    uint32_t handle;
    if (!ROMlib_carbon_call_toolbox_vector(0x1480, 0x2f0, 0, &arguments, &handle))
        return 0;
    const int16_t index = call->words[2];
    if (!handle || index <= 0)
        return 1;

    uint32_t extent;
    arguments.words[0] = handle;
    if (!ROMlib_carbon_call_register_vector(0x0494, 0x133832, 0xa025, 0, &arguments, &extent))
        return 0;
    if ((int32_t)extent < 2)
        return 1;
    const uint8_t* strings = carbon_application_handle(handle, extent);
    if (!strings || index > (int16_t)((uint16_t)strings[0] << 8 | strings[1]))
        return 1;

    size_t offset = 2;
    for (int item = 1; item <= index; ++item) {
        if (offset == extent)
            return 1;
        const size_t bytes = (size_t)strings[offset] + 1;
        if (bytes > extent - offset)
            return 1;
        if (item == index) {
            void* output = ROMlib_carbon_guest_memory(address, bytes, 1);
            if (!output)
                return 0;
            memmove(output, strings + offset, bytes);
            return 1;
        }
        offset += bytes;
    }
    return 1;
}

#endif
