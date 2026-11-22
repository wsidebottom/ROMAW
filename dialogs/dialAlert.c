/*+===================================================================
  File:      dialAlert.c

  Summary:   Implements dial alert support for Macintosh Dialog Manager operations.

  Classes:   None.

  Functions: ROMlib_standard_alert

  Origin:    Existing repository source in dialogs; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DialogMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "ResourceMgr.h"
#include "WindowMgr.h"
#include "DialogMgr.h"
#include "EventMgr.h"
#include "ToolboxEvent.h"
#include "MemoryMgr.h"

#include "OSUtil.h"
#include "rsys/itm.h"
#include "rsys/resource.h"
#include "rsys/cquick.h"
#include "rsys/ctl.h"
#include "rsys/mman.h"
#include "rsys/options.h"
#include "rsys/dial.h"
#include "rsys/ctopflags.h"
#include "rsys/wind.h"
#include "rsys/osevent.h"
#include "rsys/system_version_request.h"
#include "synppc/romaw.h"

static boolean_t alert_system_requirement(const unsigned char* text, size_t length)
{
    char expanded[1024];
    size_t used = 0;
    for (size_t at = 0; at < length && used < sizeof expanded; ++at) {
        if (text[at] == '^' && at + 1 < length && text[at + 1] >= '0' && text[at + 1] <= '3') {
            unsigned index = text[++at] - '0';
            Handle parameter = MR(HIDDEN_RAW(DAStrings_H[index]));
            if (parameter && STARH(parameter) && GetHandleSize(parameter) >= 1) {
                const unsigned char* bytes = (const unsigned char*)STARH(parameter);
                size_t count = bytes[0];
                if (count + 1 <= (size_t)GetHandleSize(parameter) && count <= sizeof expanded - used) {
                    memcpy(expanded + used, bytes + 1, count);
                    used += count;
                }
            }
        }
        else
            expanded[used++] = text[at];
    }
    uint32 required = ROMlib_required_version_message(expanded, used);
    return required && ROMlib_ppc_request_system(required);
}

static boolean_t alert_items_require_system(Handle items)
{
    if (!items || !STARH(items) || GetHandleSize(items) < 2)
        return FALSE;
    const unsigned char* bytes = (const unsigned char*)STARH(items);
    size_t size = GetHandleSize(items), at = 2;
    unsigned count = ((unsigned)bytes[0] << 8 | bytes[1]) + 1;
    for (unsigned item = 0; item < count; ++item) {
        if (size - at < 14)
            return FALSE;
        unsigned length = bytes[at + 13];
        size_t next = at + 14 + ((length + 1) & ~1U);
        if (next > size)
            return FALSE;
        if ((bytes[at + 12] & 0x7f) == statText && alert_system_requirement(bytes + at + 14, length))
            return TRUE;
        at = next;
    }
    return FALSE;
}

/* Appearance Manager StandardAlert, built with ordinary Dialog Manager items.
   Parameter layout and button numbering follow Apple's Dialog Manager Reference. */
static unsigned char* standard_alert_item(unsigned char* bytes, int type, const Rect* bounds,
                                          const unsigned char* payload, unsigned length)
{
    memset(bytes, 0, 4);
    memcpy(bytes + 4, bounds, sizeof *bounds);
    bytes[12] = type;
    bytes[13] = length;
    if (length)
        memcpy(bytes + 14, payload, length);
    if (length & 1)
        bytes[14 + length] = 0;
    return bytes + 14 + ((length + 1) & ~1u);
}

static int standard_alert_text_height(StringPtr text, int width, int line_height)
{
    int height = 0, pixels = 0;
    for (unsigned i = 1; i <= text[0]; ++i) {
        if (text[i] == '\r') {
            height += (pixels / width + 1) * line_height;
            pixels = 0;
        }
        else
            pixels += MAX(0, CharWidth(text[i]));
    }
    return height + (pixels / width + 2) * line_height;
}

OSErr ROMlib_standard_alert(INTEGER type, StringPtr message, StringPtr explanation,
                            const AlertStdAlertParamRec* parameters, INTEGER* item_hit)
{
    Str255 labels[4] = {{0}}, texts[2] = {{0}};
    static const unsigned char defaults[3][12] = {"\002OK", "\006Cancel", "\012Don't Save"};
    boolean_t present[4] = {FALSE};
    int default_button = 1, cancel_button = 0, position = alertPositionParentWindowScreen;
    ProcPtr filter = NULL;
    int widths[4] = {0}, span = 0;

    if (item_hit)
        *item_hit = 0;
    if (type < 0 || type > 3)
        return paramErr;
    if ((message && alert_system_requirement(message + 1, message[0])) ||
        (explanation && alert_system_requirement(explanation + 1, explanation[0])))
        return -128; /* The startup request is replayed in a fresh process. */
    if (message)
        memcpy(texts[0], message, message[0] + 1);
    if (explanation)
        memcpy(texts[1], explanation, explanation[0] + 1);
    if (parameters) {
        const uint32 raw[] = {PP_RAW(parameters->defaultText), PP_RAW(parameters->cancelText),
                              PP_RAW(parameters->otherText)};
        StringPtr strings[] = {PPR(parameters->defaultText), PPR(parameters->cancelText), PPR(parameters->otherText)};
        for (int i = 0; i < 3; ++i) {
            if (!raw[i])
                continue;
            present[i] = TRUE;
            StringPtr text = raw[i] == UINT32_MAX ? (StringPtr)defaults[i] : strings[i];
            memcpy(labels[i], text, text[0] + 1);
        }
        present[3] = parameters->helpButton != 0;
        memcpy(labels[3], "\001?", 2);
        default_button = CW(parameters->defaultButton);
        cancel_button = CW(parameters->cancelButton);
        position = CW(parameters->position);
        if (!position)
            position = alertPositionParentWindowScreen;
        filter = PPR(parameters->filterProc);
    }
    else {
        present[0] = TRUE;
        memcpy(labels[0], defaults[0], 3);
    }
    if (default_button < 0 || default_button > 3 || cancel_button < 0 || cancel_button > 3 ||
        (default_button && !present[default_button - 1]) || (cancel_button && !present[cancel_button - 1]) ||
        !(present[0] || present[1] || present[2] || present[3]))
        return paramErr;

    GrafPtr saved_port = thePort;
    SetPort(MR(WMgrPort));
    draw_state_t drawing;
    draw_state_save(&drawing);
    TextFont(0);
    TextSize(12);
    TextFace(0);
    for (int i = 0; i < 4; ++i)
        if (present[i]) {
            widths[i] = i == 3 ? 24 : MAX(68, StringWidth(labels[i]) + 24);
            span += widths[i] + 12;
        }
    int width = MAX(420, span + 20);
    int text_left = type == 3 ? 16 : 72;
    FontInfo font;
    GetFontInfo(&font);
    int line_height = MAX(14, CW(font.ascent) + CW(font.descent) + CW(font.leading));
    int message_height = standard_alert_text_height(texts[0], width - text_left - 16, line_height);
    int explanation_height =
        texts[1][0] ? standard_alert_text_height(texts[1], width - text_left - 16, line_height) : 0;
    int height = MAX(104, 16 + message_height + explanation_height + 48);
    draw_state_restore(&drawing);
    SetPort(saved_port);

    Handle items = NewHandle(4096);
    if (!items)
        return memFullErr;
    *(INTEGER*)STARH(items) = CWC(6);
    unsigned char* bytes = (unsigned char*)STARH(items) + 2;
    Rect bounds;
    int right = width - 16;
    for (int i = 0; i < 4; ++i) {
        if (present[i]) {
            if (i == 3)
                SetRect(&bounds, 16, height - 36, 40, height - 16);
            else {
                SetRect(&bounds, right - widths[i], height - 36, right, height - 16);
                right -= widths[i] + 12;
            }
            bytes = standard_alert_item(bytes, ctrlItem | btnCtrl, &bounds, labels[i] + 1, labels[i][0]);
        }
        else {
            SetRect(&bounds, 0, 0, 0, 0);
            bytes = standard_alert_item(bytes, statText | itemDisable, &bounds, NULL, 0);
        }
    }
    SetRect(&bounds, 16, 16, 48, 48);
    INTEGER icon = CW(type);
    bytes = standard_alert_item(bytes, type == 3 ? statText | itemDisable : iconItem | itemDisable, &bounds,
                                (unsigned char*)&icon, type == 3 ? 0 : 2);
    SetRect(&bounds, text_left, 16, width - 16, 16 + message_height);
    bytes = standard_alert_item(bytes, statText | itemDisable, &bounds, texts[0] + 1, texts[0][0]);
    SetRect(&bounds, text_left, 16 + message_height, width - 16, 16 + message_height + explanation_height);
    standard_alert_item(bytes, statText | itemDisable, &bounds, texts[1] + 1, texts[1][0]);

    SetRect(&bounds, 0, 0, width, height);
    Rect placed;
    dialog_compute_rect(&bounds, &placed, position);
    EventRecord* event = (EventRecord*)NewPtrClear(sizeof(EventRecord) + sizeof(INTEGER));
    if (!event) {
        DisposHandle(items);
        return memFullErr;
    }
    INTEGER* hit = (INTEGER*)(event + 1);
    /* Use the movable-modal implementation for internal dialogs. */
    DialogPtr dialog =
        (DialogPtr)NewCDialog(NULL, &placed, (StringPtr) "", FALSE, 1045, (WindowPtr)-1, FALSE, 0, items);
    if (!dialog) {
        DisposPtr((Ptr)event);
        DisposHandle(items);
        return memFullErr;
    }
    ((DialogPeek)dialog)->aDefItem = CW(default_button);
    ShowWindow((WindowPtr)dialog);
    SetPort((GrafPtr)dialog);
    while (!*hit) {
        GetNextEvent(everyEvent, event);
        if (filter && CToPascalCall(filter, CTOP_ROMlib_myfilt, dialog, event, hit)) {
            int item = CW(*hit);
            if (item >= 1 && item <= 4 && present[item - 1])
                break;
            *hit = 0;
        }
        int what = CW(event->what);
        unsigned key = CL(event->message) & 255;
        if (what == keyDown || what == autoKey) {
            if ((key == '\r' || key == NUMPAD_ENTER) && default_button)
                *hit = CW(default_button);
            else if ((key == 27 || (key == '.' && (CW(event->modifiers) & cmdKey))) && cancel_button)
                *hit = CW(cancel_button);
        }
        if (*hit)
            break;
        if (what == mouseDown) {
            HIDDEN_WindowPtr window;
            Point point = SWAP_POINT(event->where);
            if (FindWindow(point, &window) == inDrag && HIDDEN_NATIVE(window) == (WindowPtr)dialog) {
                Rect drag = GD_RECT(MR(MainDevice));
                DragWindow((WindowPtr)dialog, point, &drag);
                continue;
            }
        }
        HIDDEN_DialogPtr selected;
        INTEGER selected_hit = 0;
        if (IsDialogEvent(event) && DialogSelect(event, &selected, &selected_hit)) {
            int item = CW(selected_hit);
            if (item >= 1 && item <= 4 && present[item - 1])
                *hit = selected_hit;
        }
    }
    DisposDialog(dialog);
    SetPort(saved_port);
    if (item_hit)
        *item_hit = *hit;
    DisposPtr((Ptr)event);
    return noErr;
}

int16 alert_extra_icon_id = -32768;

static icon_item_template_t icon_item_template = {
    /* item count - 1 */
    CWC(0),
    CLC_NULL,
    {
        CWC(10),
        CWC(20),
        CWC(42),
        CWC(52),
    },
    CBC((1 << 7) | (iconItem)),
    CBC(2),

    /* to be filled in later */ CWC(-1),
};

P2(PUBLIC pascal trap, INTEGER, Alert, INTEGER, id, /* IMI-418 */
   ProcPtr, fp)
{
    alth ah;
    Handle h;
    Handle ih;
    INTEGER n, hit, defbut;
    Handle alert_ctab_res_h;
    Handle item_ctab_res_h;

    if (id != Cx(ANumber)) {
        ANumber = CW(id);
        ACount = 0;
    }
    ah = (alth)GetResource(TICK("ALRT"), id);
    if (!ah) {
        BEEP(1);
        return -1;
    }

    LoadResource((Handle)ah);
    n = (Hx(ah, altstag) >> (4 * CW(ACount))) & 0xF;
    ACount = CW(CW(ACount) + 1);
    if (CW(ACount) > 3)
        ACount = CWC(3);
    BEEP(n & 3);
    if (!(n & 4))
        return -1;

    ih = GetResource(TICK("DITL"), Hx(ah, altiid));
    if (!ih)
        return -1;
    LoadResource(ih);
    if (alert_items_require_system(ih))
        return -1;
    alert_ctab_res_h = ROMlib_getrestid(TICK("actb"), Hx(ah, altiid));
    FIELD_ASSIGN_RAW(item_ctab_res_h, ROMlib_getrestid(TICK("ictb"), Hx(ah, altiid)));
    h = ih;
    HandToHand(&h);

    THEGDEVICE_SAVE_EXCURSION(MR(MainDevice), {
        Rect adjusted_rect;
        boolean_t color_p;
        DialogPeek dp;
        SignedByte flags;
        itmp ip;

        LOCK_HANDLE_EXCURSION_1(ah, {
            dialog_compute_rect(&HxX(ah, altr), &adjusted_rect,
                                (ALERT_RES_HAS_POSITION_P(ah) ? ALERT_RES_POSITION(ah) : noAutoCenter));
        });

        color_p = ((alert_ctab_res_h && CTAB_SIZE((CTabHandle)alert_ctab_res_h) != -1) || item_ctab_res_h);
        if (color_p)
            dp = ((DialogPeek)NewCDialog(NULL, &adjusted_rect, (StringPtr) "", FALSE, dBoxProc, (WindowPtr)-1, FALSE,
                                         0L, h));
        else
            dp = ((DialogPeek)NewDialog(NULL, &adjusted_rect, (StringPtr) "", FALSE, dBoxProc, (WindowPtr)-1, FALSE, 0L,
                                        h));

        if (color_p) {
            THEPORT_SAVE_EXCURSION(thePort, { SetWinColor(DIALOG_WINDOW(dp), (CTabHandle)alert_ctab_res_h); });

            if (item_ctab_res_h) {
                AuxWinHandle aux_win_h;

                aux_win_h = HIDDEN_NATIVE(*lookup_aux_win(DIALOG_WINDOW(dp)));
                gui_assert(aux_win_h);

                PP_RAW(HxX(aux_win_h, dialogCItem)) = RM(item_ctab_res_h);
            }
        }

        if (alert_extra_icon_id != -32768) {
            Handle icon_item_h;

            icon_item_h = NewHandle(sizeof icon_item_template);
            icon_item_template.res_id = CW(alert_extra_icon_id);
            memcpy(STARH(icon_item_h), &icon_item_template, sizeof icon_item_template);

            AppendDITL((DialogPtr)dp, icon_item_h, overlayDITL);

            alert_extra_icon_id = -32768;

            DisposHandle(icon_item_h);
        }

        ShowWindow((WindowPtr)dp);

        THEPORT_SAVE_EXCURSION(FrontWindow(), {
            defbut = 1 + ((n & 8) >> 3);
            ip = ROMlib_dpnotoip(dp, defbut, &flags);
            if (ip) {
                Rect r;

                r = ip->itmr;
                PenSize(3, 3);
                InsetRect(&r, -4, -4);
                if (!(ROMlib_options & ROMLIB_RECT_SCREEN_BIT))
                    ROMlib_frame_default_button(&r);
                else
                    FrameRect(&r);
            }
            dp->aDefItem = CW(defbut);
            ModalDialog(fp, &hit);
        });
        HSetState(DIALOG_ITEMS(dp), flags);
        DisposDialog((DialogPtr)dp);
    });
    return CW(hit);
}

P2(PUBLIC pascal trap, INTEGER, StopAlert, INTEGER, id, /* IMI-419 */
   ProcPtr, fp)
{
    alert_extra_icon_id = stopIcon;
    return Alert(id, fp);
}

P2(PUBLIC pascal trap, INTEGER, NoteAlert, INTEGER, id, /* IMI-420 */
   ProcPtr, fp)
{
    alert_extra_icon_id = noteIcon;
    return Alert(id, fp);
}

P2(PUBLIC pascal trap, INTEGER, CautionAlert, INTEGER, id, /* IMI-420 */
   ProcPtr, fp)
{
    alert_extra_icon_id = cautionIcon;
    return Alert(id, fp);
}

A3(PRIVATE, Handle, lockres, ResType, rt, INTEGER, id, BOOLEAN, flag)
{
    Handle retval;

    SetResLoad(TRUE);
    if ((retval = GetResource(rt, id))) {
        if (flag)
            HNoPurge(retval);
        else
            HPurge(retval);
    }
    return retval;
}

#define RESCTL (ctrlItem | resCtrl)

A2(PRIVATE, void, lockditl, INTEGER, id, BOOLEAN, flag)
{
    Handle ih, h;
    INTEGER nitem, procid;
    itmp ip;

    if ((ih = lockres(TICK("DITL"), id, flag))) {
        nitem = CW(*MR(*(INTEGER**)ih));
        ip = (itmp)((INTEGER*)STARH(ih) + 1);
        while (nitem-- >= 0) {
            if ((CB(ip->itmtype) & RESCTL) == RESCTL) {
                h = lockres(TICK("CNTL"), CW(*(INTEGER*)(&(ip->itmlen) + 1)), flag);
                procid = CW(*MR(*(INTEGER**)h)) + 8;
                lockres(TICK("CDEF"), procid >> 4, flag);
            }
            else if (CB(ip->itmtype) & iconItem)
                lockres(TICK("ICON"), CW(*(INTEGER*)(&(ip->itmlen) + 1)), flag);
            else if (CB(ip->itmtype) & picItem)
                lockres(TICK("PICT"), CW(*(INTEGER*)(&(ip->itmlen) + 1)), flag);
            BUMPIP(ip);
        }
    }
}

A2(PRIVATE, void, lockalert, INTEGER, id, BOOLEAN, flag)
{
    alth ah;

    if ((ah = (alth)lockres(TICK("ALRT"), id, flag))) {
        lockditl(Hx(ah, altiid), flag);
        lockres(TICK("WDEF"), dBoxProc >> 4, flag);
        lockres(TICK("ICON"), stopIcon, flag);
        lockres(TICK("ICON"), noteIcon, flag);
        lockres(TICK("ICON"), cautionIcon, flag);
    }
}

P1(PUBLIC pascal trap, void, CouldAlert, INTEGER, id) /* IMI-420 */
{
    lockalert(id, TRUE);
}

P1(PUBLIC pascal trap, void, FreeAlert, INTEGER, id) /* IMI-420 */
{
    lockalert(id, FALSE);
}

A2(PRIVATE, void, lockdialog, INTEGER, id, BOOLEAN, flag)
{
    dlogh dh;

    if ((dh = (dlogh)lockres(TICK("DLOG"), id, flag))) {
        lockditl(Hx(dh, dlgditl), flag);
        lockres(TICK("WDEF"), Hx(dh, dlgprocid) >> 4, flag);
    }
}

P1(PUBLIC pascal trap, void, CouldDialog, INTEGER, id) /* IMI-415 */
{
    lockdialog(id, TRUE);
}

P1(PUBLIC pascal trap, void, FreeDialog, INTEGER, id) /* IMI-415 */
{
    lockdialog(id, FALSE);
}
