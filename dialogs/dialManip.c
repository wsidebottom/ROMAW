/*+===================================================================
  File:      dialManip.c

  Summary:   Implements dial manip support for Macintosh Dialog Manager operations.

  Classes:   None.

  Functions: GetAlrtStage, ROMlib_dpnotoip, ROMlib_dpntoteh, ROMlib_move_dialog_item, ROMlib_size_dialog_item,
             ResetAlrtStage

  Origin:    Existing repository source in executor/src/dialogs; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DialogMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "ControlMgr.h"
#include "DialogMgr.h"
#include "OSUtil.h"
#include "MemoryMgr.h"

#include "rsys/itm.h"
#include "rsys/wind.h"
#include "rsys/ctl.h"
#include "rsys/cquick.h"
#include "rsys/mman.h"

P4(PUBLIC pascal trap, void, ParamText, StringPtr, p0, /* IMI-421 */
   StringPtr, p1, StringPtr, p2, StringPtr, p3)
{
    if (p0)
        PtrToXHand((Ptr)p0, MR(HIDDEN_RAW(DAStrings_H[0])), (LONGINT)U(p0[0]) + 1);
    if (p1)
        PtrToXHand((Ptr)p1, MR(HIDDEN_RAW(DAStrings_H[1])), (LONGINT)U(p1[0]) + 1);
    if (p2)
        PtrToXHand((Ptr)p2, MR(HIDDEN_RAW(DAStrings_H[2])), (LONGINT)U(p2[0]) + 1);
    if (p3)
        PtrToXHand((Ptr)p3, MR(HIDDEN_RAW(DAStrings_H[3])), (LONGINT)U(p3[0]) + 1);
}

A3(PUBLIC, itmp, ROMlib_dpnotoip, DialogPeek, dp, /* INTERNAL */
   INTEGER, itemno, SignedByte*, flags)
{
    Handle items;
    INTEGER* intp;
    itmp retval;

    *flags = 0;
    if (!dp)
        return 0;
    items = DIALOG_ITEMS(dp);
    if (!items || !STARH(items))
        return 0;
    *flags = hlock_return_orig_state(items);
    intp = (INTEGER*)STARH(items);
    if (itemno <= 0 || itemno > CW(*intp) + 1)
        retval = 0;
    else {
        retval = (itmp)(intp + 1);
        while (--itemno)
            BUMPIP(retval);
    }
    return retval;
}

A4(PRIVATE, itmp, htoip, Handle, h, WindowPeek*, wp_return, int16*, nop_return, SignedByte*, flags_return)
{
    WindowPeek wp;
    int16 *ip, i, nop;
    itmp retval;

    for (wp = MR(WindowList); wp; wp = WINDOW_NEXT_WINDOW(wp)) {
        if (WINDOW_KIND_X(wp) == CWC(dialogKind) || WINDOW_KIND(wp) < 0) {
            Handle items;
            SignedByte flags;

            items = DIALOG_ITEMS(wp);
            flags = hlock_return_orig_state(items);

            ip = (int16*)STARH(items);
            retval = (itmp)(ip + 1);
            for (i = CW(*ip) + 1, nop = 1; i--; BUMPIP(retval)) {
                if (PPR(retval->itmhand) == h) {
                    *wp_return = wp;
                    *nop_return = nop;
                    *flags_return = flags;
                    return retval;
                }
                nop++;
            }
            HSetState(items, flags);
        }
    }
    return NULL;
}

P5(PUBLIC pascal trap, void, GetDItem, DialogPtr, dp, /* IMI-421 */
   INTEGER, itemno, INTEGER*, itype, HIDDEN_Handle*, item, Rect*, r)
{
    SignedByte flags;
    itmp ip = ROMlib_dpnotoip((DialogPeek)dp, itemno, &flags);

    if (ip) {
        if (itype)
            *itype = CW((INTEGER)ip->itmtype);
        else {
            /* test on Mac shows unconditional write of *itype */
            /* of course this is very rude, but that's what the Mac did
			   when we tested it.  Perhaps they've fixed that now and we
			   too should fix it.  ARGH! */
            *(INTEGER*)SYN68K_TO_US(0) = CW((INTEGER)ip->itmtype);
        }
        if (item) /* We didn't test what happens when item is 0 on Mac */
            HIDDEN_RAW_SET((*item), PP_RAW(ip->itmhand));
        if (r) /* test on Mac shows r will not be written if 0 */
            *r = ip->itmr;
    }
    if (dp && PP_RAW(((DialogPeek)dp)->items))
        HSetState(PPR(((DialogPeek)dp)->items), flags);
}

static void settexth(DialogPeek dp, itmp ip, int item_no)
{
    GrafPtr current_port;
    Handle item_text_h;
    TEHandle te;
    TEPtr tep;
    int16 length;

    current_port = thePort;

    te = DIALOG_TEXTH(dp);
    tep = STARH(te);

    TEP_DEST_RECT(tep) = TEP_VIEW_RECT(tep) = ITEM_RECT(ip);

    item_text_h = ITEM_H(ip);
    length = GetHandleSize(item_text_h);
    TEP_LENGTH_X(tep) = CW(length);
    /* this is not a leak, always a duplicate */
    PP_RAW(TEP_HTEXT_X(tep)) = RM(item_text_h);

    /* set up the text styles */
    {
        int16 te_style_size, te_style_font;
        Style te_style_face;
        RGBColor te_style_color;
        uint16 flags;
        item_style_info_t style_info;

        te_style_font = PORT_TX_FONT(dp);
        te_style_face = PORT_TX_FACE(dp);
        te_style_size = PORT_TX_SIZE(dp);

        THEPORT_SAVE_EXCURSION((GrafPtr)dp, { GetForeColor(&te_style_color); });

        if (get_item_style_info((DialogPtr)dp, item_no, &flags, &style_info)) {
            if (flags & TEdoFont)
                te_style_font = CW(style_info.font);
            if (flags & TEdoFace)
                te_style_face = CB(style_info.face);
            if (flags & TEdoSize)
                te_style_size = CW(style_info.size);

            if (flags & TEdoColor)
                te_style_color = style_info.foreground;
        }

        /* #### this code should be somehow unified with `TESetText ()',
		   although that cannot be called directly */

        if (TEP_STYLIZED_P(tep)) {
            TEStyleHandle te_style;
            STHandle style_table;
            FontInfo finfo;
            int16 tx_font_save_x, tx_size_save_x;
            Style tx_face_save;

            te_style = TE_GET_STYLE(te);
            HASSIGN_2(te_style, nRuns, CWC(1), nStyles, CWC(1));

            SetHandleSize((Handle)te_style, TE_STYLE_SIZE_FOR_N_RUNS(1));
            HxX(te_style, runs[0].startChar) = CWC(0);
            HxX(te_style, runs[0].styleIndex) = CWC(0);
            HxX(te_style, runs[1].startChar) = CW(length + 1);
            HxX(te_style, runs[1].styleIndex) = CWC(-1);
            style_table = TE_STYLE_STYLE_TABLE(te_style);
            SetHandleSize((Handle)style_table, STYLE_TABLE_SIZE_FOR_N_STYLES(1));

            tx_font_save_x = PORT_TX_FONT_X(current_port);
            tx_size_save_x = PORT_TX_SIZE_X(current_port);
            tx_face_save = PORT_TX_FACE(current_port);

            PORT_TX_FONT_X(current_port) = CW(te_style_font);
            PORT_TX_SIZE_X(current_port) = CW(te_style_size);
            PORT_TX_FACE(current_port) = CB(te_style_face);

            GetFontInfo(&finfo);

            PORT_TX_FONT_X(current_port) = tx_font_save_x;
            PORT_TX_SIZE_X(current_port) = tx_size_save_x;
            PORT_TX_FACE(current_port) = tx_face_save;

            HASSIGN_7(style_table, stCount, CWC(1), stFont, CW(te_style_font), stFace, te_style_face, stSize,
                      CW(te_style_size), stColor, te_style_color, stHeight,
                      CW(CW(finfo.ascent) + CW(finfo.descent) + CW(finfo.leading)), stAscent, finfo.ascent);
        }
        else {
            TEP_TX_FONT_X(tep) = CW(te_style_font);
            TEP_TX_SIZE_X(tep) = CW(te_style_size);
            TEP_TX_FACE(tep) = CB(te_style_face);
        }
    }

    TEP_SEL_START_X(tep) = TEP_SEL_END_X(tep) = CWC(0);

    TECalText(te);
    if (WINDOW_VISIBLE_X(dp))
        TEActivate(te);

    DIALOG_EDIT_FIELD_X(dp) = CW(item_no - 1);
    DIALOG_EDIT_OPEN_X(dp) = CW(!(ITEM_TYPE(ip) & itemDisable));
}

P5(PUBLIC pascal trap, void, SetDItem, DialogPtr, dp, /* IMI-421 */
   INTEGER, itemno, INTEGER, itype, Handle, item, Rect*, r)
{

    SignedByte flags;
    itmp ip = ROMlib_dpnotoip((DialogPeek)dp, itemno, &flags);

    if (ip) {
        ip->itmtype = CB(itype);
        PP_RAW(ip->itmhand) = RM(item);
        ip->itmr = *r;
        if (itemno - 1 == DIALOG_EDIT_FIELD(dp) && (itype & editText) && !(itype & itemDisable)) {
            TEDeactivate(DIALOG_TEXTH(dp));
            if (item)
                settexth((DialogPeek)dp, ip, itemno);
            else
                warning_unexpected(NULL_STRING);
        }
    }
    if (dp && PP_RAW(((DialogPeek)dp)->items))
        HSetState(PPR(((DialogPeek)dp)->items), flags);
}

static OSErr change_dialog_item_geometry(DialogPtr dialog, int16 item, int16 horizontal, int16 vertical, int sizing)
{
    if (!dialog || !ROMlib_valid_window(dialog) || (WINDOW_KIND(dialog) != dialogKind && WINDOW_KIND(dialog) >= 0))
        return paramErr;
    Handle items = DIALOG_ITEMS(dialog);
    if (!items || !STARH(items))
        return paramErr;

    SignedByte flags;
    itmp record = ROMlib_dpnotoip((DialogPeek)dialog, item, &flags);
    if (!record) {
        HSetState(items, flags);
        return paramErr;
    }
    const Handle contents = ITEM_H(record);
    const INTEGER type = CB(record->itmtype);
    const Rect old = ITEM_RECT(record);
    const int hidden = CW(old.left) > 8192;
    const int left = CW(old.left) - (hidden ? 16384 : 0), top = CW(old.top);
    const int width = CW(old.right) - CW(old.left), height = CW(old.bottom) - top;
    const int new_left = sizing ? left : horizontal, new_top = sizing ? top : vertical;
    const int new_width = sizing ? horizontal : width, new_height = sizing ? vertical : height;
    const int stored_left = new_left + (hidden ? 16384 : 0);
    if (new_width < 0 || new_height < 0 || stored_left < INT16_MIN || stored_left > INT16_MAX ||
        (stored_left > 8192) != hidden || stored_left + new_width > INT16_MAX || new_top + new_height > INT16_MAX) {
        HSetState(items, flags);
        return paramErr;
    }
    Rect bounds = {CW(new_top), CW(stored_left), CW(new_top + new_height), CW(stored_left + new_width)};
    HSetState(items, flags);

    SetDItem(dialog, item, type, contents, &bounds);
    if (!hidden && WINDOW_VISIBLE(dialog))
        THEPORT_SAVE_EXCURSION(dialog, {
            Rect damage = old;
            const int inset = type & editText ? -3 : item == DIALOG_ADEF_ITEM(dialog) ? -4 : 0;
            InsetRect(&damage, inset, inset);
            EraseRect(&damage);
            InvalRect(&damage);
            InsetRect(&bounds, inset, inset);
            InvalRect(&bounds);
        });
    if (contents && (type & ctrlItem)) {
        if (sizing)
            SizeControl((ControlHandle)contents, horizontal, vertical);
        else
            MoveControl((ControlHandle)contents, horizontal, vertical);
    }
    return noErr;
}

pascal OSErr ROMlib_move_dialog_item(DialogPtr dialog, int16 item, int16 horizontal, int16 vertical)
{
    return change_dialog_item_geometry(dialog, item, horizontal, vertical, 0);
}

pascal OSErr ROMlib_size_dialog_item(DialogPtr dialog, int16 item, int16 width, int16 height)
{
    return change_dialog_item_geometry(dialog, item, width, height, 1);
}

P2(PUBLIC pascal trap, void, GetIText, Handle, item, /* IMI-422 */
   StringPtr, text)
{
    Size hs;

    if (text) {
        if (!item)
            text[0] = 0;
        else {
            hs = GetHandleSize(item);
            if (hs > 255) /* can't strassign with no leading count */
                hs = 255;
            text[0] = hs;
            BlockMove(STARH(item), (Ptr)text + 1, hs);
        }
    }
}

P2(PUBLIC pascal trap, void, SetIText, Handle, item, /* IMI-422 */
   StringPtr, text)
{
    if (item) /* put this test in for Golf 6.0's sake */
    {
        WindowPeek wp;
        SignedByte flags;
        int16 no;
        itmp ip;
        Size hs;

        hs = text[0];
        /* test on Mac suggests not reallochandle */
        SetHandleSize(item, hs);

        /* test on Mac shows that if the size can't be set, the copy
	   isn't done, but the rest is */
        if (MemErr == CWC(noErr))
            BlockMove((Ptr)&text[1], STARH(item), hs);
        ip = htoip(item, &wp, &no, &flags);
        if (ip) {
            THEPORT_SAVE_EXCURSION((WindowPtr)wp, {
                ROMlib_drawiptext((DialogPtr)wp, ip, no);
                if (no == DIALOG_EDIT_FIELD(wp) + 1) {
                    TEHandle text_h;

                    text_h = DIALOG_TEXTH(wp);

                    TE_CARET_STATE_X(text_h) = CWC(255);
                    TESetText((Ptr)&text[1], text[0], text_h);
                }
                ValidRect(&ip->itmr);
            });
            HSetState(DIALOG_ITEMS(wp), flags);
        }
    }
}

/*
 * fields of TERec that need to be updated:
 * destRect, viewRect, selStart, selEnd, caretState, teLength, hText, nLines,
 * lineStarts
 */

A2(PUBLIC, void, ROMlib_dpntoteh, DialogPeek, dp, INTEGER, no) /* INTERNAL */
{
    SignedByte flags;
    itmp ip;
    INTEGER *intp, num;

    if (no == 0) {
        /* special case ... find next */
        intp = (INTEGER*)STARH(PPR(dp->items));
        num = Cx(*intp) + 1;
        ip = ROMlib_dpnotoip(dp, no = Cx(dp->editField) + 1, &flags);
        do {
            if (ip)
                BUMPIP(ip);
            else
                warning_unexpected("no (inherited from editField) = %d", no);
            if (!ip || ++no > num) {
                ip = (itmp)(intp + 1);
                no = 1;
            }
        } while (!(CB(ip->itmtype) & editText));
    }
    else
        ip = ROMlib_dpnotoip(dp, no, &flags);
    if (ip && (Cx(dp->editField) != no - 1)) {
        if (Cx(dp->editField) != -1)
            TEDeactivate(PPR(dp->textH));
        settexth(dp, ip, no);
    }
    HSetState(PPR(((DialogPeek)dp)->items), flags);
}

P4(PUBLIC pascal trap, void, SelIText, DialogPtr, dp, /* IMI-422 */
   INTEGER, itemno, INTEGER, start, INTEGER, stop)
{
    ROMlib_dpntoteh((DialogPeek)dp, itemno);
    TESetSelect(start, stop, DIALOG_TEXTH(dp));
}

A0(PUBLIC, INTEGER, GetAlrtStage) /* IMI-422 */
{
    return Cx(ACount);
}

A0(PUBLIC, void, ResetAlrtStage) /* IMI-423 */
{
    ACount = 0;
}

P2(PUBLIC pascal trap, void, HideDItem, DialogPtr, dp, /* IMIV-59 */
   INTEGER, item)
{
    itmp ip;
    Rect r;
    SignedByte flags;

    ip = ROMlib_dpnotoip((DialogPeek)dp, item, &flags);
    if (ip && CW(ip->itmr.left) < 8192) {
        r = ip->itmr;
        ip->itmr.left = CW(CW(ip->itmr.left) + 16384);
        ip->itmr.right = CW(CW(ip->itmr.right) + 16384);
        if (CB(ip->itmtype) & editText) {
            InsetRect(&r, -3, -3);
            if (item - 1 == DIALOG_EDIT_FIELD(dp)) {
                TEDeactivate(DIALOG_TEXTH(dp));
                DIALOG_EDIT_FIELD_X(dp) = CWC(-1);
            }
        }
        else if (CB(ip->itmtype) & ctrlItem) {
            ControlHandle ctl;

            ctl = (ControlHandle)PPR(ip->itmhand);
            CTL_VIS(ctl) = 0;

            if (item == DIALOG_ADEF_ITEM(dp))
                InsetRect(&r, -4, -4);
        }
        THEPORT_SAVE_EXCURSION(dp, {
            EraseRect(&r);
            InvalRect(&r);
        });
    }
    HSetState(DIALOG_ITEMS(dp), flags);
}

P2(PUBLIC pascal trap, void, ShowDItem, DialogPtr, dp, /* IMIV-59 */
   INTEGER, item)
{
    itmp ip;
    Rect r;
    SignedByte flags;

    ip = ROMlib_dpnotoip((DialogPeek)dp, item, &flags);
    if (ip && CW(ip->itmr.left) > 8192) {
        ip->itmr.left = CW(CW(ip->itmr.left) - 16384);
        ip->itmr.right = CW(CW(ip->itmr.right) - 16384);
        r = ip->itmr;
        if (CB(ip->itmtype) & editText) {
            InsetRect(&r, -3, -3);
            if (item - 1 == DIALOG_EDIT_FIELD(dp))
                TEActivate(DIALOG_TEXTH(dp));
        }
        else if (CB(ip->itmtype) & ctrlItem) {
            ControlHandle ctl;

            ctl = (ControlHandle)PPR(ip->itmhand);
            CTL_VIS(ctl) = 255;

            if (item == DIALOG_ADEF_ITEM(dp))
                InsetRect(&r, -4, -4);
        }
        THEPORT_SAVE_EXCURSION(dp, { InvalRect(&r); });
    }
    if (CB(ip->itmtype) & ctrlItem) {
        ControlHandle ctl;

        ctl = (ControlHandle)PPR(ip->itmhand);
        ShowControl(ctl);
    }
    HSetState(DIALOG_ITEMS(dp), flags);
}
