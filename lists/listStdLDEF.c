/*+===================================================================
  File:      listStdLDEF.c

  Summary:   Implements list std ldef support for Macintosh List Manager operations.

  Classes:   None.

  Functions: C_ldef0, C_ldef19.

  Origin:    Existing repository source in executor/src/lists; original attribution and provenance comments are
             preserved below. Icon cell layout follows the SuperMario List Manager IconLDEF contract.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "ListMgr.h"
#include "MemoryMgr.h"
#include "Iconutil.h"
#include "ScriptMgr.h"
#include "ResourceMgr.h"
#include "rsys/cquick.h"
#include "rsys/list.h"

A5(PRIVATE, void, draw, BOOLEAN, sel, Rect*, rect, INTEGER, doff, INTEGER, dl, ListHandle, list)
{
    GrafPtr savePort;

    TRAPBEGIN();
    savePort = thePort;
    SetPort(HxP(list, port));
    EraseRect(rect);
    MoveTo(CW(rect->left) + Hx(list, indent.h), CW(rect->top) + Hx(list, indent.v));
    HLock((Handle)HxP(list, cells));
    DrawText((Ptr)STARH(HxP(list, cells)) + doff, 0, dl);
    HUnlock((Handle)HxP(list, cells));
    if (sel)
        InvertRect(rect);
    SetPort(savePort);
    TRAPEND();
}

/* Icon list cells contain a guest handle, optionally followed by font,
   face, size and a Pascal label. The list owns neither the icon nor suite. */
P7(PUBLIC pascal, void, ldef19, INTEGER, msg, BOOLEAN, sel, Rect*, rect, Cell, cell, INTEGER, off, INTEGER, len,
   ListHandle, list)
{
    enum { truncate_middle = 0x4000 };

    if (msg != lDrawMsg && msg != lHiliteMsg)
        return;
    Handle cells = (Handle)HxP(list, cells);
    if (!cells || !STARH(cells) || off < 0 || len < 0 || off + len > GetHandleSize(cells))
        return;

    const Byte* data = (const Byte*)STARH(cells) + off;
    Byte label[256] = {0};
    uint32 icon_address = 0;
    uint16 font = 0, size = 0;
    Byte face = 0;
    if (len >= 4)
        memcpy(&icon_address, data, 4);
    if (len >= 11) {
        memcpy(&font, data + 4, 2);
        face = data[7];
        memcpy(&size, data + 8, 2);
        label[0] = MIN(data[10], len - 11);
        memcpy(label + 1, data + 11, label[0]);
    }

    GrafPtr saved_port = thePort;
    GrafPtr port = HxP(list, port);
    SetPort(port);
    const INTEGER saved_font = PORT_TX_FONT(port), saved_size = PORT_TX_SIZE(port), saved_mode = PORT_TX_MODE(port);
    const Style saved_face = PORT_TX_FACE(port);
    FontInfo info = {0};
    if (label[0]) {
        TextFont(CW(font));
        TextFace(face);
        TextSize(CW(size));
        TextMode(srcOr);
        GetFontInfo(&info);
        TruncString(MAX(0, CW(rect->right) - CW(rect->left) - 2), label, truncate_middle);
    }
    if (msg == lDrawMsg)
        EraseRect(rect);
    const INTEGER text_height = CW(info.ascent) + CW(info.descent) + CW(info.leading);
    const INTEGER x = CW(rect->left) + (CW(rect->right) - CW(rect->left) - 32) / 2;
    const INTEGER y = CW(rect->top) + (CW(rect->bottom) - CW(rect->top) - 32 - text_height) / 2;
    Rect icon_rect;
    SetRect(&icon_rect, x, y, x + 32, y + 32);
    if (icon_address) {
        Handle icon = MR(icon_address);
        const Size bytes = GetHandleSize(icon);
        if (bytes >= 256) {
            Handle suite = NULL;
            if (!STARH(icon))
                LoadResource(icon);
            if (STARH(icon) && NewIconSuite(&suite) == noErr) {
                if (AddIconToSuite(icon, suite, TICK("ICN#")) == noErr)
                    PlotIconSuite(&icon_rect, atNone, sel ? ttSelected : ttNone, suite);
                DisposeIconSuite(suite, FALSE);
            }
        }
        else if (bytes >= 128) {
            PlotIcon(&icon_rect, icon);
            if (sel)
                InvertRect(&icon_rect);
        }
        else if (bytes > 0)
            PlotIconSuite(&icon_rect, atNone, sel ? ttSelected : ttNone, icon);
    }
    if (label[0]) {
        const INTEGER width = StringWidth(label);
        const INTEGER left = CW(rect->left) + (CW(rect->right) - CW(rect->left) - width) / 2;
        Rect text_rect;
        SetRect(&text_rect, left - 1, y + 32, left + width + 1, y + 32 + text_height);
        EraseRect(&text_rect);
        MoveTo(left, y + 32 + CW(info.leading) + CW(info.ascent));
        DrawString(label);
        if (sel)
            InvertRect(&text_rect);
    }
    TextFont(saved_font);
    TextFace(saved_face);
    TextSize(saved_size);
    TextMode(saved_mode);
    SetPort(saved_port);
}

P7(PUBLIC pascal, void, ldef0, INTEGER, msg, BOOLEAN, sel, Rect*, rect, Cell, cell, INTEGER, doff, INTEGER, dl,
   ListHandle, list) /* IMIV-276 */
{
    GrafPtr savePort;
    FontInfo fi;

    switch (msg) {
    case lInitMsg:
        savePort = thePort;
        SetPort(HxP(list, port));
        GetFontInfo(&fi);
        HxX(list, indent.h) = CWC(5);
        HxX(list, indent.v) = fi.ascent;
        SetPort(savePort);
        break;
    case lDrawMsg:
        draw(sel, rect, doff, dl, list);
        break;
    case lHiliteMsg:
        savePort = thePort;
        SetPort(HxP(list, port));
        InvertRect(rect);
        SetPort(savePort);
        break;
    case lCloseMsg: /* nothing special to do */
        break;
    default: /* weirdness */
        break;
    }
}
