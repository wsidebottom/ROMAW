/*+===================================================================
  File:      toolbox_link_stubs.c

  Summary:   Fail fast for Toolbox services outside the standalone Carbon fixture.

  Classes:   None.

  Functions: Fail-fast Toolbox entry points and runtime hooks for the standalone fixture.
             These include QuickDraw state, Apple Event object accessors, and menu queries.

  Origin:    Standalone Carbon regression fixture for Executor Toolbox dependencies.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "rsys/everything.h"
#include "rsys/osevent.h"
#include "Gestalt.h"
#include "carbonlib.h"
#include <stdio.h>
#include <stdlib.h>

// These services have real implementations in ROMAW. The standalone fixture
// does not exercise them; an unexpected call must never silently succeed.
static _Noreturn void unexpected_toolbox_call(const char* name)
{
    fprintf(stderr, "Unexpected Toolbox service in Carbon fixture: %s\n", name);
    abort();
}

LONGINT C_GetEnvirons(INTEGER verb)
{
    unexpected_toolbox_call("GetEnvirons");
}

OSErrRET Gestalt(OSType selector, LONGINT* response)
{
    unexpected_toolbox_call("Gestalt");
}

Ptr C_GetPixBaseAddr(PixMapHandle pixmap)
{
    unexpected_toolbox_call("GetPixBaseAddr");
}

Ptr ROMlib_kchr_ptr(void)
{
    unexpected_toolbox_call("ROMlib_kchr_ptr");
}

LONGINT C_KeyTrans(Ptr map, unsigned short code, LONGINT* state)
{
    unexpected_toolbox_call("KeyTrans");
}

INTEGER C_FontScript(void)
{
    unexpected_toolbox_call("FontScript");
}

void Delay(LONGINT ticks, LONGINT* final_ticks)
{
    unexpected_toolbox_call("Delay");
}

void C_SectRgn(RgnHandle first, RgnHandle second, RgnHandle destination)
{
    unexpected_toolbox_call("SectRgn");
}

void C_InsetRgn(RgnHandle region, INTEGER horizontal, INTEGER vertical)
{
    unexpected_toolbox_call("InsetRgn");
}

void C_UnionRect(Rect* first, Rect* second, Rect* destination)
{
    unexpected_toolbox_call("UnionRect");
}

BOOLEAN C_PtInRect(Point point, Rect* rect)
{
    unexpected_toolbox_call("PtInRect");
}

OSErr ROMlib_ae_object_init(void)
{
    unexpected_toolbox_call("AEObjectInit");
}

OSErr C_AEDisposeToken(AEDesc* token)
{
    unexpected_toolbox_call("AEDisposeToken");
}

OSErr C_AEREesolve(AEDesc* specifier, INTEGER flags, AEDesc* token)
{
    unexpected_toolbox_call("AEResolve");
}

OSErr C_AERemoveObjectAccessor(DescType desired, DescType container, ProcPtr accessor, BOOLEAN system)
{
    unexpected_toolbox_call("AERemoveObjectAccessor");
}

OSErr C_AEInstallObjectAccessor(DescType desired, DescType container, ProcPtr accessor, LONGINT refcon, BOOLEAN system)
{
    unexpected_toolbox_call("AEInstallObjectAccessor");
}

OSErr C_AEGetObjectAccessor(DescType desired, DescType container, HIDDEN_ProcPtr* accessor, LONGINT* refcon, BOOLEAN system)
{
    unexpected_toolbox_call("AEGetObjectAccessor");
}

OSErr C_AECallObjectAccessor(DescType desired, AEDesc* token, DescType container, DescType key_form, AEDesc* data,
                             AEDesc* output)
{
    unexpected_toolbox_call("AECallObjectAccessor");
}

OSErr C_AESetObjectCallbacks(ProcPtr compare, ProcPtr count, ProcPtr dispose, ProcPtr get_mark, ProcPtr mark,
                              ProcPtr adjust, ProcPtr get_error)
{
    unexpected_toolbox_call("AESetObjectCallbacks");
}

LONGINT ROMlib_get_submenu_parent_choosable(MenuHandle menu, INTEGER item, BOOLEAN* choosable)
{
    unexpected_toolbox_call("GetSubmenuParentChoosable");
}

ColorSpec ROMlib_white_cspec = {CWC(0), {CWC(0xffff), CWC(0xffff), CWC(0xffff)}};
ColorSpec ROMlib_black_cspec = {CWC(1), {0, 0, 0}};
ColorSpec ROMlib_gray_cspec = {CWC(0), {CWC(0x8000), CWC(0x8000), CWC(0x8000)}};
const RGBColor ROMlib_theme_background_rgb = {CWC(0xdddd), CWC(0xdddd), CWC(0xdddd)};

#define UNUSED_TOOLBOX_ENTRY(name, type)          \
    void C_##name(type argument)                  \
    {                                             \
        (void)argument;                           \
        unexpected_toolbox_call(#name);           \
    }

UNUSED_TOOLBOX_ENTRY(BackPat, Pattern)
UNUSED_TOOLBOX_ENTRY(BackPixPat, PixPatHandle)
UNUSED_TOOLBOX_ENTRY(GetBackColor, RGBColor*)
UNUSED_TOOLBOX_ENTRY(GetForeColor, RGBColor*)
UNUSED_TOOLBOX_ENTRY(GetPenState, PenState*)
UNUSED_TOOLBOX_ENTRY(InvalRect, Rect*)
UNUSED_TOOLBOX_ENTRY(RGBBackColor, RGBColor*)
UNUSED_TOOLBOX_ENTRY(SetPenState, PenState*)
UNUSED_TOOLBOX_ENTRY(TextMode, INTEGER)

#undef UNUSED_TOOLBOX_ENTRY

HIDDEN_AuxWinHandle* lookup_aux_win(WindowPtr window)
{
    (void)window;
    unexpected_toolbox_call("lookup_aux_win");
}

Point ROMlib_pattern_origin(void)
{
    unexpected_toolbox_call("ROMlib_pattern_origin");
}

ROMlib_carbon_theme_state** ROMlib_carbon_theme_state_slot(void)
{
    unexpected_toolbox_call("ROMlib_carbon_theme_state_slot");
}

ROMlib_carbon_debug_component** ROMlib_carbon_debug_component_slot(void)
{
    unexpected_toolbox_call("ROMlib_carbon_debug_component_slot");
}

uint32_t* ROMlib_carbon_debug_handler_slot(void)
{
    unexpected_toolbox_call("ROMlib_carbon_debug_handler_slot");
}

int32 ROMlib_SetModalDialogEventMask(DialogPtr dialog, uint16 mask)
{
    unexpected_toolbox_call("ROMlib_SetModalDialogEventMask");
}

int32 ROMlib_GetModalDialogEventMask(DialogPtr dialog, uint16* mask)
{
    unexpected_toolbox_call("ROMlib_GetModalDialogEventMask");
}

int32 ROMlib_SetDialogTimeout(DialogPtr dialog, INTEGER item, uint32 duration)
{
    unexpected_toolbox_call("ROMlib_SetDialogTimeout");
}

int32 ROMlib_GetDialogTimeout(DialogPtr dialog, INTEGER* item, uint32* duration, uint32* remaining)
{
    unexpected_toolbox_call("ROMlib_GetDialogTimeout");
}

void ROMlib_menu_validate_bar(void)
{
    unexpected_toolbox_call("ROMlib_menu_validate_bar");
}

void ROMlib_menu_invalidate_sizes(void)
{
    unexpected_toolbox_call("ROMlib_menu_invalidate_sizes");
}

void ROMlib_menu_set_bar_height(INTEGER height)
{
    unexpected_toolbox_call("ROMlib_menu_set_bar_height");
}
