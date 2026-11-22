/*+===================================================================
  File:      theme_cursor.h

  Summary:   Native Appearance cursor selection through the live QuickDraw traps.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    System 9.2.1 AppearanceLib 0xcf1c..0xd578 and its cursor tables,
             CarbonLib 1.4 re-exports, and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_THEME_CURSOR_H
#define ROMAW_CARBON_THEME_CURSOR_H

#include "internal.h"
#include "toolbox_vectors.h"

enum { theme_invalid_cursor = -30565, theme_animated = 1, theme_special_install = 2 };

/* Default Platinum CURS identifiers from AppearanceLib. Entries with
   color-only resources use the saved Appearance Manager. */
static const int16_t theme_cursor_resources[] = {0,  -20486, -20487, -20488, 1,  2,  3,      4,      -1,
                                                 -1, -1,     -1,     -1,     -1, -1, -20852, -20851, -20850};

static int theme_cursor_result(ROMlib_carbon_ppc_frame* call, int32_t status)
{
    call->words[0] = status;
    return 1;
}

static int theme_cursor_fallback(ROMlib_carbon_ppc_frame* call, const char* name, int32_t unavailable)
{
    ROMlib_carbon_reference reference;
    if (ROMlib_carbon_probe_reference("AppearanceLib", name, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return theme_cursor_result(call, unavailable);
}

static int theme_cursor_frame_id(uint32_t cursor, uint32_t step)
{
    static const int16_t watch[] = {4, -20721, -20720, -20719, -20718, -20717, -20716, -20715};
    if (cursor == 7)
        return watch[step % (sizeof watch / sizeof *watch)];
    return -20708 + (int)(step % 8);
}

static int theme_apply_cursor(ROMlib_carbon_ppc_frame* call, const char* name, uint32_t options,
                              uint32_t animation_step)
{
    const uint32_t cursor = call->words[0];
    if (cursor >= sizeof theme_cursor_resources / sizeof *theme_cursor_resources)
        return theme_cursor_result(call, theme_invalid_cursor);

    const int animated = cursor == 7 || (cursor >= 11 && cursor <= 14);
    if ((options & theme_animated) && !animated)
        return theme_cursor_result(call, theme_invalid_cursor);
    if ((options & theme_special_install) || (cursor >= 8 && cursor <= 13))
        return theme_cursor_fallback(call, name, resNotFound);

    const int id = animated ? theme_cursor_frame_id(cursor, animation_step) : theme_cursor_resources[cursor];
    ROMlib_carbon_ppc_frame arguments = *call;
    if (!id)
        arguments.words[0] = US_TO_SYN68K(&arrowX);
    else {
        arguments.words[0] = id;
        if (!carbon_vector_GetCursor(&arguments))
            return 0;
        const uint32_t handle = arguments.words[0];
        if (!handle)
            return theme_cursor_fallback(call, name, resNotFound);
        if (!ROMlib_carbon_try_guest_memory(handle, sizeof(uint32_t), 0))
            return theme_cursor_result(call, resNotFound);
        const Handle master = SYN68K_TO_US(handle);
        if (!carbon_vector_GetResourceSizeOnDisk(&arguments))
            return 0;
        if ((int32_t)arguments.words[0] < (int32_t)sizeof(Cursor))
            return theme_cursor_fallback(call, name, resNotFound);
        arguments.words[0] = US_TO_SYN68K_CHECK0(STARH(master));
        if (!arguments.words[0] || !ROMlib_carbon_try_guest_memory(arguments.words[0], sizeof(Cursor), 0))
            return theme_cursor_fallback(call, name, resNotFound);
    }
    if (!carbon_vector_SetCursor(&arguments))
        return 0;
    return theme_cursor_result(call, noErr);
}

static int carbon_SetThemeCursor(ROMlib_carbon_ppc_frame* call)
{
    return theme_apply_cursor(call, "SetThemeCursor", 0, 0);
}

static int carbon_SetAnimatedThemeCursor(ROMlib_carbon_ppc_frame* call)
{
    return theme_apply_cursor(call, "SetAnimatedThemeCursor", theme_animated, call->words[1]);
}

static int carbon_SetThemeCursorWithOptions(ROMlib_carbon_ppc_frame* call)
{
    return theme_apply_cursor(call, "SetThemeCursorWithOptions", call->words[1],
                              (call->words[1] & theme_animated) ? call->words[2] : 0);
}

#endif
