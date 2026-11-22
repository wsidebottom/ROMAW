/*+===================================================================
  File:      syserr.c

  Summary:   Implements syserr support for ROMAW diagnostics and tracing.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/diagnostics; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1988 - 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in SysErr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "EventMgr.h"
#include "ToolboxEvent.h"
#include "DialogMgr.h"
#include "OSUtil.h"
#include "SegmentLdr.h"
#include "SysErr.h"
#include "MemoryMgr.h"
#include "BinaryDecimal.h"

#include "rsys/cquick.h"
#include "rsys/quick.h"
#include "rsys/segment.h"
#include "rsys/flags.h"
#include "rsys/version.h"
#include "rsys/vdriver.h"
#include "rsys/osevent.h"
#include "rsys/options.h"
#include "rsys/syserr.h"

/* Writable because dobuttons temporarily offsets the button rectangles. */
#include "generated/syserr_table.inc"

char syserr_msg[256];

static INTEGER* find_table_id(unsigned char* table, INTEGER id)
{
    unsigned char* cursor = table + 2;
    INTEGER count;
    memcpy(&count, table, sizeof(count));
    for (int i = 0; i < CW(count); ++i) {
        INTEGER identity, length;
        memcpy(&identity, cursor, sizeof(identity));
        memcpy(&length, cursor + 2, sizeof(length));
        if (CW(identity) == id)
            return (INTEGER*)cursor;
        cursor += 4 + (unsigned short)CW(length);
    }
    return NULL;
}

A1(PRIVATE, INTEGER*, findid, INTEGER, id)
{
    int i;
    INTEGER* ip;

    for (i = CW(*(INTEGER*)MR(DSAlertTab)), ip = (INTEGER*)MR(DSAlertTab) + 1; i > 0 && CW(*ip) != id;
         --i, ip = (INTEGER*)((char*)ip + CW(ip[1]) + 2 * sizeof(INTEGER)))
        ;
    return i > 0 ? ip : (INTEGER*)0;
}

A3(PRIVATE, void, drawtextstring, INTEGER, id, INTEGER, offsetx, INTEGER, offsety)
{
    struct tdef* tp;

    if (id && (tp = (struct tdef*)findid(id))) {
        INTEGER horizontal = CW(tp->loc.h) + offsetx;
        INTEGER vertical = CW(tp->loc.v) + offsety;
        unsigned length = (unsigned short)CW(tp->alen);
        if (length < sizeof(Point))
            return;
        length -= sizeof(Point);
        MoveTo(horizontal, vertical);
        unsigned first = 0, position = 0;
        for (; position < length && tp->text[position]; ++position) {
            if (tp->text[position] == '/') {
                DrawText((Ptr)tp->text, first, position - first);
                vertical += 14;
                MoveTo(horizontal, vertical);
                first = position + 1;
            }
        }
        DrawText((Ptr)tp->text, first, position - first);
    }
}

A3(PRIVATE, void, drawicon, INTEGER, id, INTEGER, offsetx, INTEGER, offsety)
{
    struct idef* ip;
    BitMap bm;
    Rect old_loc;

    ip = (struct idef*)findid(id);
    if (id && ip) {
        FIELD_ASSIGN_RAW(bm.baseAddr, RM((Ptr)ip->ike));
        bm.rowBytes = CWC(4);
        bm.bounds.left = bm.bounds.top = CWC(0);
        bm.bounds.right = bm.bounds.bottom = CWC(32);
        old_loc = ip->loc;
        C_OffsetRect(&ip->loc, offsetx, offsety);
        CopyBits(&bm, PORT_BITS_FOR_COPY(thePort), &bm.bounds, &ip->loc, srcCopy, NULL);
        ip->loc = old_loc;
    }
}

static BOOLEAN native_alert_table(void)
{
    return (unsigned char*)MR(DSAlertTab) == myalerttab || (unsigned char*)MR(DSAlertTab) == myalerttab2;
}

static void run_alert_procedure(INTEGER identity, ROMlib_syserr_context* context)
{
    INTEGER* record;
    if (!identity || !(record = findid(identity)))
        return;
    if (native_alert_table()) {
        if (CW(record[1]) == 4) {
            LONGINT handler;
            memcpy(&handler, record + 2, sizeof(handler));
            ROMlib_syserr_procedure(CL(handler), context);
        }
    }
    else {
        /* Real DSAT procedures are inline 68K code, not host function pointers. */
        CALL_EMULATOR((syn68k_addr_t)US_TO_SYN68K(record + 2));
    }
}

static void dobuttons(INTEGER id, INTEGER offsetx, INTEGER offsety, ROMlib_syserr_context* context)
{
    struct bdef* bp;
    struct sdef* sp;
    int i;
    EventRecord evt;
    int done;
    int tcnt, twid;
    Point p;
    INTEGER h, v;
    char* textp;

    if ((bp = (struct bdef*)findid(id))) {
        for (i = 0; i < Cx(bp->nbut); i++) {

            /* Offset buttons; this hack is to center the splash screen
	     * on non-512x342 root windows...yuck!
	     */

            C_OffsetRect(&bp->buts[i].butloc, offsetx, offsety);
            if ((sp = (struct sdef*)findid(CW(bp->buts[i].butstrid)))) {
                textp = sp->text;
                tcnt = 0;
                while (tcnt < (unsigned short)CW(sp->alen) && textp[tcnt])
                    ++tcnt;
                twid = TextWidth((Ptr)textp, 0, tcnt);
                MoveTo((CW(bp->buts[i].butloc.left) + CW(bp->buts[i].butloc.right) - twid) / 2,
                       (CW(bp->buts[i].butloc.top) + CW(bp->buts[i].butloc.bottom)) / 2 + 4);
                DrawText((Ptr)textp, 0, tcnt);
            }
            h = CW(bp->buts[i].butloc.right) - CW(bp->buts[i].butloc.left);
            v = (CW(bp->buts[i].butloc.bottom) - CW(bp->buts[i].butloc.top)) / 2;
            if (h > v)
                h = v;
            if (!(ROMlib_options & ROMLIB_RECT_SCREEN_BIT))
                FrameRoundRect(&bp->buts[i].butloc, h, v);
            else
                FrameRect(&bp->buts[i].butloc);
        }

        for (done = 0; !done;) {
            C_GetNextEvent(mDownMask | keyDownMask, &evt);
            if (evt.what == CWC(mouseDown) || evt.what == CWC(keyDown)) {
                p.h = CW(evt.where.h);
                p.v = CW(evt.where.v);
                for (i = 0; !done && i < CW(bp->nbut); i++) {
                    if (PtInRect(p, &bp->buts[i].butloc) ||
                        ((evt.what == CWC(keyDown)) && (((CL(evt.message) & charCodeMask) == '\r') ||
                                                        ((CL(evt.message) & charCodeMask) == NUMPAD_ENTER)))) {
                        run_alert_procedure(CW(bp->buts[i].butprocid), context);
                        done = 1;
                    }
                }
                if (!done)
                    SysBeep(1);
            }
        }
        if (evt.what == CWC(mouseDown))
            while (!C_GetNextEvent(mUpMask, &evt))
                ;

        /* Move all buttons back. */
        for (i = 0; i < Cx(bp->nbut); i++)
            C_OffsetRect(&bp->buts[i].butloc, -offsetx, -offsety);
    }
}

P1(PUBLIC pascal, void, SysError, short, errorcode)
{
    GrafPort alertport;
    Region viscliprgn;
    HIDDEN_RgnPtr rp;
    Rect r;
    struct adef* ap;
    char quickbytes[grafSize];
    INTEGER offsetx, offsety;
    Rect main_gd_rect;
    GrafPtr saved_port = thePort;
    WindowPtr saved_wmgr_port = WMgrPort;
    CWindowPtr saved_wmgr_cport = WMgrCPort;

    ROMlib_syserr_context context = {0};
    context.guest_a5 = EM_A5;
    context.guest_a7 = EM_A7;
    context.resource_map = CurMap;

    LONGINT tmpa5;
    LONGINT saved_a5 = a5;
    Ptr saved_current_a5 = CurrentA5;

    main_gd_rect = PIXMAP_BOUNDS(GD_PMAP(MR(MainDevice)));

    INTEGER lookup_error = errorcode < 0 ? -errorcode : errorcode;
    BOOLEAN builtin = !DSAlertTab || native_alert_table();
    if (builtin) {
        unsigned char* table = myalerttab;
        if (errorcode < 0 && find_table_id(table, errorcode))
            lookup_error = errorcode;
        if (!find_table_id(table, lookup_error) && find_table_id(myalerttab2, lookup_error)) {
            table = myalerttab2;
            context.table = 2;
        }
        INTEGER screen_width = CW(main_gd_rect.right);
        INTEGER screen_height = CW(main_gd_rect.bottom);

        HIDDEN_RAW_SET(DSAlertTab_H, RM((Ptr)table));
        DSAlertRect.top = CW((screen_height - 126) / 3);
        DSAlertRect.left = CW((screen_width - 448) / 2);
        DSAlertRect.bottom = CW(CW(DSAlertRect.top) + 126);
        DSAlertRect.right = CW(CW(DSAlertRect.left) + 448);

        offsetx = CW(DSAlertRect.left) - 32;
        offsety = CW(DSAlertRect.top) - 64;
    }
    else {
        offsetx = offsety = 0;
    }

    context.offsetx = offsetx;
    context.offsety = offsety;
    context.bounds = DSAlertRect;

    /* IM-362 */
    /* 1. Save registers and Stack Pointer */
    /*	  NOT DONE YET... signal handlers sort of do that anyway */

    /* 2. Store errorcode in DSErrCode */
    DSErrCode = CW(errorcode);

    /* 3. If no Cx(DSAlertTab), bitch */
    if (!DSAlertTab) {
        write(2, "This machine thinks its a sadmac\n", sizeof("This machine thinks its a sadmac\n") - 1);
        exit(255);
    }

    /* 4. Allocate and re-initialize QuickDraw */
    a5 = (LONGINT)(long)US_TO_SYN68K(&tmpa5);
    HIDDEN_RAW_SET(CurrentA5_H, (Ptr)(uintptr_t)CL(a5));
    InitGraf((Ptr)quickbytes + sizeof(quickbytes) - 4);
    ROMlib_initport(&alertport);
    SetPort(&alertport);
    InitCursor();
    HIDDEN_RAW_SET(rp, RM(&viscliprgn));
    PP_ASSIGN(alertport.visRgn, &rp);
    PP_ASSIGN(alertport.clipRgn, &rp);
    viscliprgn.rgnSize = CWC(10);
    viscliprgn.rgnBBox = main_gd_rect;

    /* find appropriate entry */

    ap = (struct adef*)findid(lookup_error);
    if (!ap)
        ap = (struct adef*)((INTEGER*)MR(DSAlertTab) + 1);

    /* Extended alerts carry their own bounds in the sixth record word. */
    if (CW(ap->alen) >= 12) {
        INTEGER rectangle_id;
        memcpy(&rectangle_id, (unsigned char*)ap + 14, sizeof(rectangle_id));
        INTEGER* rectangle = findid(CW(rectangle_id));
        if (rectangle && CW(rectangle[1]) == sizeof(Rect)) {
            memcpy(&context.bounds, rectangle + 2, sizeof(Rect));
            C_OffsetRect(&context.bounds, offsetx, offsety);
            /* Extended records use coordinates local to their custom alert box. */
            offsetx = context.offsetx = CW(context.bounds.left);
            offsety = context.offsety = CW(context.bounds.top);
        }
    }

    /* 5, 6. Draw alert box if the errorcode is >= 0 */
    TRAPBEGIN();
    if (errorcode < 0)
        errorcode = -errorcode;
    else {
        r = context.bounds;
        FillRect(&r, white);
        FrameRect(&r);
        InsetRect(&r, 3, 3);
        PenSize(2, 2);
        FrameRect(&r);
        PenSize(1, 1);
    }

    /* 7. text strings */
    drawtextstring(CW(ap->primetextid), offsetx, offsety);
    drawtextstring(CW(ap->secondtextid), offsetx, offsety);

    /* 8. icon */
    drawicon(CW(ap->iconid), offsetx, offsety);

    /* 9. Draw the native/guest procedure after ordinary text and artwork. */
    run_alert_procedure(CW(ap->procid), &context);

    /* 10-13. Adjacent button sets contain the optional Continue choice. */
    INTEGER button_id = CW(ap->buttonid);
    if (ResumeProc && findid(button_id + 1))
        ++button_id;
    if (button_id)
        dobuttons(button_id, offsetx, offsety, &context);

    TRAPEND();
    a5 = saved_a5;
    HIDDEN_RAW_SET(CurrentA5_H, saved_current_a5);
    SetPort(saved_port);
    HIDDEN_RAW_SET(WMgrPort_H, saved_wmgr_port);
    HIDDEN_RAW_SET(WMgrCPort_H, saved_wmgr_cport);
}
