/*+===================================================================
  File:      romaw_services.h

  Summary:   CarbonLib frame adapters for shared classic Toolbox and Appearance services.

  Classes:   None.

  Functions: Private adapters for the native export registry.

  Origin:    CarbonLib 1.4 PEF re-exports and Executor's built-in PPC service table.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_ROMAW_SERVICES_H
#define ROMAW_CARBON_ROMAW_SERVICES_H

#include "carbonlib.h"
#include "DialogMgr.h"
#include "rsys/wind.h"

#define CARBON_ROMAW_SERVICE(name)                                      \
    static int carbon_romaw_##name(ROMlib_carbon_ppc_frame* call_frame) \
    {                                                                   \
        return ROMlib_carbon_call_romaw_service(#name, call_frame);     \
    }

CARBON_ROMAW_SERVICE(PostEvent)
CARBON_ROMAW_SERVICE(FlushEvents)
CARBON_ROMAW_SERVICE(AppendDITL)
CARBON_ROMAW_SERVICE(CountDITL)
CARBON_ROMAW_SERVICE(ShortenDITL)
CARBON_ROMAW_SERVICE(GetSysDirection)
CARBON_ROMAW_SERVICE(SetSysDirection)
CARBON_ROMAW_SERVICE(KeyTranslate)
CARBON_ROMAW_SERVICE(GetMBarHeight)
CARBON_ROMAW_SERVICE(NewGestalt)
CARBON_ROMAW_SERVICE(ReplaceGestalt)
CARBON_ROMAW_SERVICE(GetAppFont)
CARBON_ROMAW_SERVICE(GetSysFont)
CARBON_ROMAW_SERVICE(GetIndPattern)
CARBON_ROMAW_SERVICE(MaxMem)
CARBON_ROMAW_SERVICE(GetFrontProcess)
CARBON_ROMAW_SERVICE(WakeUpProcess)
CARBON_ROMAW_SERVICE(HSetFInfo)
CARBON_ROMAW_SERVICE(SetScriptManagerVariable)
CARBON_ROMAW_SERVICE(SndSoundManagerVersion)
CARBON_ROMAW_SERVICE(TSMGetActiveDocument)
CARBON_ROMAW_SERVICE(UpperString)
CARBON_ROMAW_SERVICE(DebugStr)
CARBON_ROMAW_SERVICE(MakeDataExecutable)

static int carbon_romaw_MakeExecutable(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[0] = call->words[1];
    arguments.words[1] = call->words[2];
    return carbon_romaw_MakeDataExecutable(&arguments);
}

CARBON_ROMAW_SERVICE(StandardAlert)
CARBON_ROMAW_SERVICE(GetMenuItemModifiers)
CARBON_ROMAW_SERVICE(SetMenuItemModifiers)
CARBON_ROMAW_SERVICE(GetMenuItemKeyGlyph)
CARBON_ROMAW_SERVICE(SetMenuItemKeyGlyph)
CARBON_ROMAW_SERVICE(SetMenuItemRefCon)
CARBON_ROMAW_SERVICE(GetMenuItemRefCon)
CARBON_ROMAW_SERVICE(IsSystemMenu)
CARBON_ROMAW_SERVICE(MenuEvent)
CARBON_ROMAW_SERVICE(SetControlFontStyle)
CARBON_ROMAW_SERVICE(SetImageWellContentInfo)
CARBON_ROMAW_SERVICE(GetImageWellContentInfo)
CARBON_ROMAW_SERVICE(SetBevelButtonContentInfo)
CARBON_ROMAW_SERVICE(GetBevelButtonContentInfo)
CARBON_ROMAW_SERVICE(SetBevelButtonMenuValue)
CARBON_ROMAW_SERVICE(GetBevelButtonMenuValue)
CARBON_ROMAW_SERVICE(SetBevelButtonGraphicAlignment)
CARBON_ROMAW_SERVICE(SetBevelButtonTextAlignment)
CARBON_ROMAW_SERVICE(SetBevelButtonTextPlacement)
CARBON_ROMAW_SERVICE(CreateRootControl)
CARBON_ROMAW_SERVICE(GetRootControl)
CARBON_ROMAW_SERVICE(EmbedControl)
CARBON_ROMAW_SERVICE(AutoEmbedControl)
CARBON_ROMAW_SERVICE(SetControlVisibility)
CARBON_ROMAW_SERVICE(FindControlUnderMouse)
CARBON_ROMAW_SERVICE(DrawThemeMenuSeparator)
CARBON_ROMAW_SERVICE(SetThemeTextColor)
CARBON_ROMAW_SERVICE(DrawControlInCurrentPort)
CARBON_ROMAW_SERVICE(DrawThemeFocusRect)
CARBON_ROMAW_SERVICE(GetThemeMenuSeparatorHeight)
CARBON_ROMAW_SERVICE(GetThemeMenuItemExtra)

#undef CARBON_ROMAW_SERVICE

static DialogPtr carbon_dialog(ROMlib_carbon_ppc_frame* call)
{
    return call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(WindowRecord), 0) : NULL;
}

static int carbon_dialog_result(ROMlib_carbon_ppc_frame* call, int32 result)
{
    call->words[0] = (uint32)result;
    return 1;
}

static bool carbon_saved_dialog(DialogPtr dialog, const char* name, ROMlib_carbon_reference* reference)
{
    return dialog && !ROMlib_window_get_metadata(dialog) &&
           ROMlib_carbon_load_reference("MacDialogsLib", name, reference);
}

static int carbon_SetModalDialogEventMask(ROMlib_carbon_ppc_frame* call)
{
    DialogPtr dialog = carbon_dialog(call);
    ROMlib_carbon_reference reference;
    if (carbon_saved_dialog(dialog, "SetModalDialogEventMask", &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return carbon_dialog_result(call, ROMlib_SetModalDialogEventMask(dialog, (uint16)call->words[1]));
}

static int carbon_GetModalDialogEventMask(ROMlib_carbon_ppc_frame* call)
{
    DialogPtr dialog = carbon_dialog(call);
    uint16* mask = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *mask, 1) : NULL;
    if (!mask)
        return carbon_dialog_result(call, paramErr);
    ROMlib_carbon_reference reference;
    if (carbon_saved_dialog(dialog, "GetModalDialogEventMask", &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return carbon_dialog_result(call, ROMlib_GetModalDialogEventMask(dialog, mask));
}

static int carbon_SetDialogTimeout(ROMlib_carbon_ppc_frame* call)
{
    DialogPtr dialog = carbon_dialog(call);
    ROMlib_carbon_reference reference;
    if (carbon_saved_dialog(dialog, "SetDialogTimeout", &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return carbon_dialog_result(call, ROMlib_SetDialogTimeout(dialog, (int16)call->words[1], call->words[2]));
}

static int carbon_GetDialogTimeout(ROMlib_carbon_ppc_frame* call)
{
    DialogPtr dialog = carbon_dialog(call);
    INTEGER* item = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *item, 1) : NULL;
    uint32* duration = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], sizeof *duration, 1) : NULL;
    uint32* remaining = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], sizeof *remaining, 1) : NULL;
    if ((call->words[1] && !item) || (call->words[2] && !duration) || (call->words[3] && !remaining))
        return carbon_dialog_result(call, paramErr);
    ROMlib_carbon_reference reference;
    if (carbon_saved_dialog(dialog, "GetDialogTimeout", &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return carbon_dialog_result(call, ROMlib_GetDialogTimeout(dialog, item, duration, remaining));
}

#endif
