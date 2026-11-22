/*+===================================================================
  File:      syserr_procedures.c

  Summary:   Implements native C equivalents of the extracted DSAT procedures.

  Classes:   None.

  Functions: ROMlib_syserr_procedure

  Origin:    C translation of Procedure bodies extracted from the user's
             DSAT.rsrc resource fork; native services replace ROM boot operations.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "QuickDraw.h"
#include "DialogMgr.h"
#include "SegmentLdr.h"
#include "ShutDown.h"
#include "rsys/mman.h"
#include "rsys/syserr.h"

/* Shared by DSAT 0 and 2, procedure -21103. */
static const struct {
    unsigned error;
    const char* description;
} error_descriptions[] = {
    {1, "bus error"},
    {2, "address error"},
    {3, "illegal instruction"},
    {4, "divide by zero"},
    {5, "CHK error"},
    {6, "numeric overflow"},
    {12, "unimplemented trap"},
    {13, "unserviceable interrupt"},
    {14, "Device Manager error"},
    {15, "Segment Loader error"},
    {16, "SANE error"},
    {17, "List Manager not present"},
    {18, "BitEdit not present"},
    {19, "Disk Initialization package not present"},
    {20, "Standard File not present"},
    {21, "SANE not present"},
    {22, "SANE transcendental functions not present"},
    {23, "International Utilities not present"},
    {24, "Binary-Decimal Conversion package not present"},
    {27, "file system map inconsistent"},
    {28, "stack collision with heap"},
    {33, "Memory Manager error"},
    {51, "unserviceable slot interrupt"},
    {81, "bad SANE opcode"},
    {84, "menu purged while in use"},
    {85, "couldn\325t load MBDF"},
    {86, "couldn\325t find hierarchical menu\325s parent"},
    {87, "couldn\325t load WDEF"},
    {88, "couldn\325t load CDEF"},
};

static void* guest_bytes(uint32 address, uint32 length)
{
    if (!address || address > ROMlib_guest_memory_size || length > ROMlib_guest_memory_size - address)
        return NULL;
    return SYN68K_TO_US(address);
}

static uint32 guest_long(uint32 address)
{
    uint32 value = 0;
    const void* bytes = guest_bytes(address, sizeof(value));
    if (bytes)
        memcpy(&value, bytes, sizeof(value));
    return CL(value);
}

/* Longword pushes in the 68K bodies place vertical before horizontal on the Pascal stack. */
static void move_to(ROMlib_syserr_context* context, INTEGER vertical, INTEGER horizontal)
{
    C_MoveTo(horizontal + context->offsetx, vertical + context->offsety);
}

static void draw_text(const char* text)
{
    C_DrawText((Ptr)text, 0, strlen(text));
}

static void draw_guest_string(uint32 address)
{
    const unsigned char* text = guest_bytes(address, 1);
    if (text && guest_bytes(address, 1 + text[0]))
        C_DrawText((Ptr)(text + 1), 0, text[0]);
}

/* -21108: show the application name and, for a damaged name, restart advice. */
static void draw_application(ROMlib_syserr_context* context)
{
    uint32 address = 0x910;
    const unsigned char* name = (const unsigned char*)CurApName;
    BOOLEAN damaged = (signed char)name[0] < 0;
    move_to(context, 108, 114);
    if (damaged) {
        address = guest_long(0x914);
        uint32 top = guest_long(0x108);
        uint32 limit = (top >> 1);
        limit = (limit & 0xffff0000U) | ((limit + 0x400) & 0xffffU);
        name = address < limit && !(address & 1) ? guest_bytes(address, 1) : NULL;
    }
    if (name && name[0] < 32 && guest_bytes(address, 1 + name[0])) {
        C_DrawChar(0xd2);
        draw_guest_string(address);
        C_DrawChar(0xd3);
    }
    if (damaged) {
        move_to(context, 136, 114);
        draw_text("To temporarily turn off extensions, restart and");
        move_to(context, 150, 114);
        draw_text("hold down the shift key.");
    }
}

/* -21103; -21106 performs its stack normalization in the native caller. */
static void draw_error(ROMlib_syserr_context* context)
{
    unsigned error = (unsigned short)CW(DSErrCode);
    if (error >= 100)
        return;
    if (context->table == 2)
        draw_application(context);
    move_to(context, 122, 130);
    for (unsigned i = 0; i < sizeof(error_descriptions) / sizeof(error_descriptions[0]); ++i) {
        if (error_descriptions[i].error == error) {
            draw_text(error_descriptions[i].description);
            return;
        }
    }
    draw_text("error type ");
    if (error / 10)
        C_DrawChar((error / 10) | '0');
    C_DrawChar((error % 10) | '0');
}

/* -21112: the ROM unions into a private stack frame; retain native bounds here. */
static void extend_alert_bounds(ROMlib_syserr_context* context)
{
    Rect bounds = DSAlertRect;
    bounds.top = CW(CW(bounds.top) & 0x7fff);
    C_UnionRect(&bounds, &context->bounds, &context->bounds);
}

/* -21111 draws a supplied Pascal module/disk name between curly quotes. */
static void draw_module_name(ROMlib_syserr_context* context, uint32 name)
{
    move_to(context, 40, 72);
    C_DrawChar(0xd2);
    draw_guest_string(name);
    C_DrawChar(0xd3);
}

/* These offsets refer to guest-owned module state; never create a substitute. */
static uint32 module_state(void)
{
    uint32 state = guest_long(0xbb8);
    return state && guest_bytes(state, 0x4c) ? guest_long(state + 0x48) : 0;
}

void ROMlib_syserr_procedure(INTEGER identity, ROMlib_syserr_context* context)
{
    switch (identity) {
    case -21000:
        /* Both original bodies eject volumes and enter ROMBase+10 to reboot.
           ROMAW's Shutdown Manager owns restart/exit and volume teardown. */
        if (context->table == 0)
            C_ExitToShell(); /* User changed this table's button to Quit. */
        else
            C_ShutDwnStart();
        break;
    case -21101:
        move_to(context, 24, 72);
        draw_text("Please insert the disk:");
        draw_module_name(context, module_state());
        break;
    case -21102: {
        /* The native ExitToShell service performs guest context/VM teardown. */
        unsigned char* flags = guest_bytes(0xbff, 1);
        if (flags) {
            if (*flags == 0xff)
                flags = guest_bytes(0x120, 1);
            if (flags)
                *flags &= 0x7f;
        }
        EM_A5 = context->guest_a5;
        C_ExitToShell();
        break;
    }
    case -21103:
    case -21106:
        draw_error(context);
        break;
    case -21104:
        /* Native context replaces the ROM's fixed-offset saved-stack frame. */
        if (ResumeProc) {
            CurMap = context->resource_map;
            EM_A5 = context->guest_a5;
            EM_A7 = context->guest_a7;
            CALL_EMULATOR((syn68k_addr_t)US_TO_SYN68K(MR(ResumeProc)));
        }
        break;
    case -21105:
        /* ShutDwnPower owns shutdown in ROMAW, including ExitToShell. */
        C_ShutDwnPower();
        break;
    case -21107:
        move_to(context, 94, 114);
        draw_text("Force ");
        C_DrawChar(0xd2);
        C_DrawText((Ptr)(CurApName + 1), 0, MIN(CurApName[0], sizeof(CurApName) - 1));
        C_DrawChar(0xd3);
        draw_text(" to quit?");
        extend_alert_bounds(context);
        break;
    case -21108:
        draw_application(context);
        break;
    case -21109: {
        uint32 state = module_state();
        uint32 name = state && guest_bytes(state, 6) ? guest_long(state + 2) : 0;
        move_to(context, 24, 72);
        draw_text(name ? "Please eject the disk:" : "Please eject the disk.");
        if (name)
            draw_module_name(context, name);
        move_to(context, name ? 56 : 40, 72);
        draw_text("Press the eject button.");
        break;
    }
    case -21110:
        move_to(context, 24, 72);
        draw_text("Please insert the disk:");
        move_to(context, 56, 72);
        draw_text("This disk contains files or");
        move_to(context, 72, 72);
        draw_text("programs that are in use.");
        draw_module_name(context, module_state());
        break;
    case -21111:
        /* Original is a subroutine with a stack argument, not an alert entry. */
        draw_module_name(context, EM_D0);
        break;
    case -21112:
        extend_alert_bounds(context);
        break;
    default:
        warning_unimplemented("Unknown native system-error procedure %d", identity);
        break;
    }
}
