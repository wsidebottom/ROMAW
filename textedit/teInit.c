/*+===================================================================
  File:      teInit.c

  Summary:   Implements te init support for Macintosh TextEdit operations.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/textedit; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in TextEdit.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "ToolboxUtil.h"
#include "FontMgr.h"
#include "TextEdit.h"
#include "MemoryMgr.h"

#include "rsys/cquick.h"
#include "rsys/mman.h"
#include "rsys/tesave.h"

P0(PUBLIC pascal trap, void, TEInit)
{
    HIDDEN_RAW_SET(TEScrpHandle_H, RM(NewHandle(0)));
    TEScrpLength = CWC(0);
    HIDDEN_RAW_SET(TEDoText_H, RM((ProcPtr)P_ROMlib_dotext));
}

/* This code just does "moveql #1,d0 ; rts".  We use it because
 * DNA strider chains to the old clikLoop handler.
 */
static uint16 default_clik_loop[2] = {CWC(0x7001), CWC(0x4E75)};

P2(PUBLIC pascal trap, TEHandle, TENew, Rect*, dst, Rect*, view)
{
    TEHandle teh;
    FontInfo finfo;
    Handle hText;
    tehiddenh temptehiddenh;
    int16* tehlinestarts;
    int te_size;
    INTEGER tx_font;
    Style tx_face;
    INTEGER tx_mode;
    INTEGER tx_size;

    te_size = ((sizeof(TERec) - sizeof TE_LINE_STARTS(teh)) + 4 * sizeof *TE_LINE_STARTS(teh));
    teh = (TEHandle)NewHandle(te_size);
    if (!teh)
        return NULL;

    Handle text = NewHandle(0);
    tehiddenh hidden = text ? (tehiddenh)NewHandleClear(sizeof(tehidden)) : NULL;
    if (!hidden) {
        if (text)
            DisposHandle(text);
        DisposHandle((Handle)teh);
        MemErr = CWC(memFullErr);
        return NULL;
    }
    hText = RM(text);
    GetFontInfo(&finfo);
    if (thePort) {
        tx_font = PORT_TX_FONT_X(thePort);
        tx_face = PORT_TX_FACE(thePort);
        tx_mode = PORT_TX_MODE_X(thePort);
        tx_size = PORT_TX_SIZE_X(thePort);
    }
    else {
        tx_font = CWC(systemFont);
        tx_face = 0;
        tx_mode = CWC(srcOr);
        tx_size = CWC(12);
    }
    /* zero the te record */
    memset(STARH(teh), 0, te_size);
    /* ### find out what to assign to the `selRect', `selPoint'
     and `clikStuff' fields */
    HASSIGN_15(teh, destRect, *dst, viewRect, *view, lineHeight,
               CW(CW(finfo.ascent) + CW(finfo.descent) + CW(finfo.leading)), fontAscent, finfo.ascent, active, FALSE,
               caretState, CWC(caret_invis), just, CWC(teFlushDefault), crOnly, CWC(1), clikLoop,
               (ProcPtr)RM(&default_clik_loop[0]), inPort, thePortX, txFont, tx_font, txFace, tx_face, txMode, tx_mode,
               txSize, tx_size, hText, hText);

    tehlinestarts = HxX(teh, lineStarts);
    tehlinestarts[0] = 0;
    tehlinestarts[1] = 0; /* this one is only for mix & match w/mac */

    temptehiddenh = RM(hidden);
    /* don't merge with line above */
    HIDDEN_RAW_SET(*(HIDDEN_tehiddenh*)&STARH(teh)->recalBack, temptehiddenh);

    TE_SLAM(teh);

    return teh;
}

P1(PUBLIC pascal trap, void, TEDispose, TEHandle, teh)
{
    DisposHandle((Handle)TEHIDDENH(teh));
    DisposHandle(TE_HTEXT(teh));
    DisposHandle((Handle)teh);
}
