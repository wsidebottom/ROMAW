/*+===================================================================
  File:      stdmdef.c

  Summary:   Implements stdmdef support for Macintosh Menu Manager operations.

  Classes:   None.

  Functions: C_mdef0, ROMlib_draw_host_menu_item, choose_menu, cleanup_icon_info, get_icon_info, size_menu

  Origin:    Existing repository source in executor/src/menus; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "MenuMgr.h"
#include "WindowMgr.h"
#include "FontMgr.h"
#include "MemoryMgr.h"
#include "ToolboxEvent.h"
#include "Iconutil.h"

#include "rsys/menu.h"
#include "rsys/quick.h"
#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/host.h"

#include "rsys/desk.h"

static Rect* current_menu_rect;

#define TOP_ARROW_P() (CW(TopMenuItem) < CW(current_menu_rect->top))
#define BOTTOM_ARROW_P() (CW(AtMenuBottom) > CW(current_menu_rect->bottom))

static int16 checksize, cloversize, lineheight, ascent;

#define UP (1 << 0)
#define DOWN (1 << 1)

typedef enum {
    uparrow,
    downarrow,
} arrowtype;

/*
 * I think SICNFLAG is correct.  I'm guessing from what I've seen in MacMan
 * and some sort of dollhouse construction set.  MacMan doesn't have any icon
 * sicn conflicts, but the dollhouse construction set did (though it's on a
 * CD-ROM somewhere and I don't know where).
 */

#define SICN_FLAG 0x1E
#define REDUCED_ICON_FLAG 0x1D

/* return true if there is an icon */

void cleanup_icon_info(icon_info_t* info)
{
    if (info->borrowed_type)
        return;
    if (info->icon && info->suite_p)
        DisposeIconSuite(info->icon, TRUE);
    else if (info->icon && info->color_icon_p) {
        DisposeCIcon((CIconHandle)info->icon);
    }
}

int get_icon_info(MenuHandle menu, StringPtr name, mextp item_info, icon_info_t* info, int need_icon_p)
{
    Handle h = NULL;

    /* my default */
    info->width = info->height = 0;
    info->icon = NULL;
    info->suite_p = FALSE;
    info->color_icon_p = FALSE;
    info->borrowed_type = 0;
    if (!item_info->micon) {
        Handle suite = ROMlib_desk_menu_icon(menu, name);
        if (suite) {
            info->width = info->height = 16 + ICON_PAD;
            info->suite_p = TRUE;
            if (need_icon_p)
                info->icon = suite;
            else
                DisposeIconSuite(suite, TRUE);
            return TRUE;
        }
    }

    if (item_info->micon) {
        if (item_info->mkeyeq != SICN_FLAG) {
            h = GetResource(TICK("cicn"), 256 + item_info->micon);
            if (h) {
                CIconHandle icon;
                Rect* bounds;

                info->color_icon_p = TRUE;

                icon = (CIconHandle)h;
                bounds = &(CICON_PMAP(icon).bounds);
                info->width = RECT_WIDTH(bounds) + ICON_PAD;
                info->height = RECT_HEIGHT(bounds) + ICON_PAD;
            }
            else {
                h = GetResource(TICK("ICON"), 256 + item_info->micon);
                if (h) {
                    info->color_icon_p = FALSE;
                    info->width = 32 + ICON_PAD;
                    info->height = 32 + ICON_PAD;
                }
            }
        }
        if (h && item_info->mkeyeq == REDUCED_ICON_FLAG) {
            info->width = 16 + ICON_PAD;
            info->height = 16 + ICON_PAD;
        }
        else if (!h) {
            if (item_info->mkeyeq == SICN_FLAG)
                h = GetResource(TICK("SICN"), 256 + item_info->micon);
            if (h) {
                info->color_icon_p = FALSE;
                info->width = 16 + ICON_PAD;
                info->height = 16 + ICON_PAD;
            }
            else
                return FALSE;
        }
        gui_assert(h);
        if (need_icon_p) {
            if (info->color_icon_p)
                info->icon = (Handle)GetCIcon(256 + item_info->micon);
            else {
                info->icon = h;
                if (!HIDDEN_RAW(*(h)))
                    LoadResource(h);
            }
        }
        return TRUE;
    }
    else
        return FALSE;
}

/* See IMV-236 */

PRIVATE BOOLEAN iskeyequiv(struct tableentry* tp)
{
    BOOLEAN retval;

    if (tp->options->mkeyeq)
        retval = (tp->options->mkeyeq < 0x1b || tp->options->mkeyeq > 0x1f);
    else
        retval = FALSE;

    return retval;
}

static uint32 menu_layout_property(MenuHandle menu, int16 item, uint32 tag)
{
    uint32 size, attributes;
    const void* bytes;
    if (ROMlib_menu_item_property(menu, item, TICK("macs"), tag, &size, &attributes, &bytes) || size != 4)
        return 0;
    uint32 value;
    memcpy(&value, bytes, sizeof value);
    return CL(value);
}

static int menu_item_indent(MenuHandle menu, int16 item)
{
    return MIN(menu_layout_property(menu, item, TICK("idnt")), INT16_MAX);
}

static INTEGER menu_item_font(MenuHandle menu, INTEGER item)
{
    INTEGER font = 0;
    ROMlib_get_menu_item_font(menu, item, &font);
    return CW(font);
}

static int menu_item_icon_info(MenuHandle menu, INTEGER item, const struct tableentry* entry, icon_info_t* info,
                               int drawing)
{
    Byte type;
    ULONGINT address;
    if (ROMlib_get_menu_item_icon(menu, item, &type, &address) || !type || !address)
        return get_icon_info(menu, entry->name, entry->options, info, drawing);
    const Handle icon = (Handle)SYN68K_TO_US(CL(address));
    *info = (icon_info_t){0};
    if (!HIDDEN_RAW(*icon))
        return FALSE;
    if (type == 4) {
        if (GetHandleSize(icon) < sizeof(CIcon))
            return FALSE;
        const Rect* bounds = &CICON_PMAP((CIconHandle)icon).bounds;
        info->width = RECT_WIDTH(bounds) + ICON_PAD;
        info->height = RECT_HEIGHT(bounds) + ICON_PAD;
        info->color_icon_p = TRUE;
    }
    else {
        info->width = info->height = (type == 1 ? 32 : 16) + ICON_PAD;
    }
    info->suite_p = type == 5;
    info->borrowed_type = type;
    if (drawing)
        info->icon = icon;
    return TRUE;
}

static int menu_item_icon_disabled(MenuHandle menu, int16 item)
{
    uint32 size, attributes;
    const void* bytes;
    if (ROMlib_menu_item_property(menu, item, TICK("macs"), TICK("iatm"), &size, &attributes, &bytes) || size != 8)
        return 0;
    uint32 encoded;
    memcpy(&encoded, (const Byte*)bytes + 4, sizeof encoded);
    return (CL(encoded) & 2) != 0;
}

void size_menu(MenuHandle mh, tablePtr tablep)
{
    struct tableentry *tp, *ep;
    int16 width, height, actual_height, max_height;

    width = height = actual_height = 0;
    /* the 32 is just a guess */
    max_height = CW(screenBitsX.bounds.bottom) - 32;
    for (tp = tablep->entry, ep = tp + tablep->count; tp != ep; tp++) {
        icon_info_t icon_info;
        int w;

        const INTEGER item = tp - tablep->entry + 1;
        const INTEGER font = menu_item_font(mh, item);
        const INTEGER previous_font = PORT_TX_FONT_X(thePort);
        w = checksize + 2 + menu_item_indent(mh, item);

        menu_item_icon_info(mh, item, tp, &icon_info, FALSE);
        w += icon_info.width;

        height += tp[1].top - tp[0].top;
        TextFace(tp->options->mstyle);
        if (font)
            TextFont(font);
        w += StringWidth(tp->name);
        PORT_TX_FONT_X(thePort) = previous_font;
        w += 2 * cloversize + 1;
        if (height < max_height)
            actual_height = height;
        if (width < w)
            width = MIN(w, INT16_MAX);
    }
    TextFace(0);
    HxX(mh, menuWidth) = CW(width);
    HxX(mh, menuHeight) = CW(actual_height);
}

static void draw_right_arrow(Rect* menu_rect, MenuHandle mh, int item, int invert_p)
{
    Rect dst_rect;
    BitMap arrow_bitmap;
    static char right_arrow_bits[] = {
#include "generated/menu_right_arrow.inc"
    };
    int x, y;

    FIELD_ASSIGN_RAW(arrow_bitmap.baseAddr, RM((Ptr)right_arrow_bits));
    arrow_bitmap.rowBytes = CWC(1);
    SetRect(&arrow_bitmap.bounds, 0, 0, /* right, bottom */ 6, 11);

    y = CW(menu_rect->top) + 2;
    x = CW(menu_rect->right) - 14;

    dst_rect.top = CW(y);
    dst_rect.left = CW(x);
    dst_rect.bottom = CW(y + 11);
    dst_rect.right = CW(x + 6);

    CopyBits(&arrow_bitmap, PORT_BITS_FOR_COPY(thePort), &arrow_bitmap.bounds, &dst_rect, srcCopy, NULL);
}

static void draw_arrow(Rect* menu_rect, MenuHandle mh, arrowtype arrdir)
{
    BitMap arrow_bitmap;
    Rect dst_rect, erase_rect;
    int top_of_item;
    RGBColor bk_color, title_color;
    char up_arrow_bits[] = {
#include "generated/menu_up_arrow.inc"
    };
    char down_arrow_bits[] = {
#include "generated/menu_down_arrow.inc"
    };

    if (arrdir == uparrow) {
        FIELD_ASSIGN_RAW(arrow_bitmap.baseAddr, RM((Ptr)up_arrow_bits));
        arrow_bitmap.rowBytes = CWC(2);
        SetRect(&arrow_bitmap.bounds, 0, 0, /* right, bottom */ 11, 6);

        top_of_item = CW(menu_rect->top);
    }
    else if (arrdir == downarrow) {
        FIELD_ASSIGN_RAW(arrow_bitmap.baseAddr, RM((Ptr)down_arrow_bits));
        arrow_bitmap.rowBytes = CWC(2);
        SetRect(&arrow_bitmap.bounds, 0, 0, /* right, bottom */ 11, 6);

        top_of_item = CW(menu_rect->bottom) - lineheight;
    }
    else
        gui_abort();

    menu_bk_color(MI_ID(mh), &bk_color);
    menu_title_color(MI_ID(mh), &title_color);

    RGBForeColor(&title_color);
    RGBBackColor(&bk_color);

    erase_rect.left = menu_rect->left;
    erase_rect.right = menu_rect->right;
    erase_rect.top = CW(top_of_item);
    erase_rect.bottom = CW(top_of_item + lineheight);
    EraseRect(&erase_rect);

    dst_rect.top = CW(top_of_item + 5);
    dst_rect.left = CW(CW(menu_rect->left) + checksize);
    dst_rect.bottom = CW(top_of_item + 5 + /* arrows are `6' tall */ 6);
    dst_rect.right = CW(CW(menu_rect->left) + checksize + /* arrows are `11' wide */ 11);
    CopyBits(&arrow_bitmap, PORT_BITS_FOR_COPY(thePort), &arrow_bitmap.bounds, &dst_rect, srcCopy, NULL);

    /* resent the fg/bk colors */
    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);
}

A3(PRIVATE, void, erasearrow, Rect*, rp, tablePtr, tablep, BOOLEAN, upordown)
{
    Rect r;
    INTEGER x, y;

    x = CW(rp->left) + checksize;
    if (upordown == UP)
        y = CW(rp->top) + 5;
    else
        y = CW(rp->bottom) - lineheight + 5;
    r.top = CW(y);
    r.left = CW(x);
    r.bottom = CW(y + 6);
    r.right = CW(x + 11);
    EraseRect(&r);
}

static void draw_item_with_shortcut(Rect* rp, struct tableentry* tp, int item, MenuHandle mh, int invert_p,
                                    int icon_width, StringPtr shortcut, int separator)
{
    RGBColor bk_color, mark_color, title_color, command_color;
    int16 top, bottom, v;
    Rect rtmp;
    int draw_right_arrow_p;
    icon_info_t icon_info;
    int draw_icon_p;
    int divider_p;
    int active_p;
    boolean_t dither_p, dither_cmd_p;

    dither_p = FALSE;
    dither_cmd_p = FALSE;
    draw_right_arrow_p = tp->options->mkeyeq == 0x1B;
    divider_p = separator < 0 ? tp->name[0] && (tp->name[1] == '-') : separator;
    /* active vs grayed out */
    active_p = ROMlib_menu_item_enabled(mh, 0) && ROMlib_menu_item_enabled(mh, item) && !divider_p;
    const int content_left = CW(rp->left) + checksize + 2 + menu_item_indent(mh, item);

    top = tp[0].top + CW(TopMenuItem);
    bottom = tp[1].top + CW(TopMenuItem);

    v = top + ascent;

    draw_icon_p = menu_item_icon_info(mh, item, tp, &icon_info, TRUE);
    if (!draw_icon_p)
        icon_info.width = icon_width;
    if (draw_icon_p)
        v += (icon_info.height - lineheight) / 2;

    menu_item_colors(MI_ID(mh), item, &bk_color, &title_color, &mark_color, &command_color);
#define FAIL goto failure
#define DONE goto done
#define DO_BLOCK_WITH_FAILURE(try_block, fail_block) \
    {                                                \
        {                                            \
            try_block                                \
        }                                            \
        goto done;                                   \
    failure: {                                       \
        fail_block                                   \
    }                                                \
    /* fall through */                               \
    done:;                                           \
    }
    if (!active_p) {
        gui_assert(!invert_p);

        DO_BLOCK_WITH_FAILURE(
            {
                if (!AVERAGE_COLOR(&bk_color, &title_color, 0x8000, &title_color))
                    FAIL;
                if (!AVERAGE_COLOR(&bk_color, &mark_color, 0x8000, &mark_color))
                    FAIL;
                if (!AVERAGE_COLOR(&bk_color, &command_color, 0x8000, &command_color))
                    FAIL;
            },
            {
                dither_p = TRUE;
                dither_cmd_p = TRUE;

                if (memcmp(&bk_color, &ROMlib_white_rgb_color, sizeof(RGBColor)))
                    warning_unexpected("can't dither menu item");
                title_color = mark_color = command_color = ROMlib_black_rgb_color;
            });
    }
    RGBBackColor(invert_p ? &title_color : &bk_color);

    rtmp.top = CW(top);
    rtmp.bottom = CW(bottom);
    rtmp.left = rp->left;
    rtmp.right = rp->right;

    EraseRect(&rtmp);

    if (checksize && tp->options->mmarker && !divider_p && !draw_right_arrow_p) {
        RGBForeColor(invert_p ? &bk_color : &mark_color);
        MoveTo(CW(rp->left) + 2, v);
        DrawChar(tp->options->mmarker);
    }

    /* draw bw icons with the text color (?) */
    RGBForeColor(invert_p ? &bk_color : &title_color);

    /* draw the icon */
    if (draw_icon_p && content_left < CW(rp->right)) {
        rtmp.top = CW(top + ICON_PAD / 2);
        rtmp.left = CW(content_left);
        rtmp.bottom = CW(CW(rtmp.top) + (icon_info.height - ICON_PAD));
        rtmp.right = CW(MIN(INT16_MAX, content_left + icon_info.width - ICON_PAD));

        if (icon_info.suite_p)
            PlotIconSuite(&rtmp, 0, 0, icon_info.icon);
        else if (icon_info.color_icon_p)
            PlotCIcon(&rtmp, (CIconHandle)icon_info.icon);
        else if (icon_info.borrowed_type == 3)
            PlotSICNHandle(&rtmp, 0, 0, icon_info.icon);
        else
            PlotIcon(&rtmp, icon_info.icon);
        if (!dither_p && menu_item_icon_disabled(mh, item)) {
            PenMode(notPatBic);
            PenPat(gray);
            PaintRect(&rtmp);
            PenPat(black);
            PenMode(patCopy);
        }
    }

    if (divider_p) {
        RGBForeColor(&title_color);
        MoveTo(CW(rp->left), v - 4);
        LineTo(CW(rp->right), v - 4);
    }
    else if (content_left + icon_info.width < CW(rp->right)) {
        RGBForeColor(invert_p ? &bk_color : &title_color);

        const INTEGER font = menu_item_font(mh, item);
        const INTEGER previous_font = PORT_TX_FONT_X(thePort);
        int baseline = v;
        TextFace(tp->options->mstyle);
        if (font) {
            FontInfo metrics;
            TextFont(font);
            GetFontInfo(&metrics);
            baseline = top + (bottom - top + CW(metrics.ascent) - CW(metrics.descent) - CW(metrics.leading)) / 2;
        }
        MoveTo(content_left + icon_info.width, baseline);
        DrawString(tp->name);
        PORT_TX_FONT_X(thePort) = previous_font;
        TextFace(0);
    }
    rtmp.left = rp->left;
    rtmp.right = rp->right;
    rtmp.top = CW(top);
    rtmp.bottom = CW(bottom);
    if ((iskeyequiv(tp) || draw_right_arrow_p || (shortcut && shortcut[0])) && !divider_p) {
        if (draw_right_arrow_p) {
            RGBForeColor(invert_p ? &bk_color : &title_color);
            draw_right_arrow(&rtmp, mh, item, invert_p);
        }
        else {
            INTEGER new_left;

            RGBForeColor(invert_p ? &bk_color : &command_color);

            new_left = CW(rp->right) - (shortcut ? StringWidth(shortcut) + 4 : 2 * cloversize + 1);
            MoveTo(new_left, v);
            if (shortcut)
                DrawString(shortcut);
            else {
                DrawChar(commandMark);
                DrawChar(tp->options->mkeyeq);
            }
            if (dither_cmd_p && !dither_p) {
                Rect r;

                r = rtmp;
                r.left = CW(new_left);
                PenMode(notPatBic);
                PenPat(gray);
                PaintRect(&r);
                PenPat(black);
                PenMode(patCopy);
            }
        }
    }
    /* if the entire table is disabled, so is every entry */
    if (dither_p) {
        PenMode(notPatBic);
        PenPat(gray);
        PaintRect(&rtmp);
        PenPat(black);
        PenMode(patCopy);
    }
    cleanup_icon_info(&icon_info);

    /* resent the fg/bk colors */
    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);
}

static void draw_item(Rect* rp, struct tableentry* tp, int item, MenuHandle mh, int invert_p)
{
    draw_item_with_shortcut(rp, tp, item, mh, invert_p, 0, NULL, -1);
}

/* Reuse StandardMDEF's colours, marks, divider, arrow and text drawing for a
   native row. No MenuList/MBSaveLoc mutation or guest input tracking is needed.
   The ordinary MDEF continues to use its original command-key glyph path. */
void ROMlib_draw_host_menu_item(MenuHandle menu, Rect* bounds, StringPtr shortcut, int icon_width, int selected,
                                int separator, int draw, int* width, int* height)
{
    int16 saved_check = checksize, saved_clover = cloversize;
    int16 saved_height = lineheight, saved_ascent = ascent;
    INTEGER saved_top = TopMenuItem;
    FontInfo info;
    GetFontInfo(&info);
    checksize = CharWidth(checkMark) + 1;
    cloversize = CharWidth(commandMark);
    lineheight = CW(info.ascent) + CW(info.descent) + CW(info.leading);
    ascent = CW(info.ascent);
    SignedByte state = HGetState((Handle)menu);
    HLock((Handle)menu);
    struct tableentry entries[2] = {0};
    entries[0].name = (StringPtr)((char*)STARH(menu) + SIZEOFMINFO + Hx(menu, menuData[0]));
    entries[0].options = (mextp)(entries[0].name + entries[0].name[0] + 1);
    *height = separator ? 8 : MAX(lineheight, icon_width);
    *width = checksize + 2 + icon_width + StringWidth(entries[0].name) +
             MAX(2 * cloversize + 1, shortcut && shortcut[0] ? StringWidth(shortcut) + 12 : 0);
    if (draw) {
        entries[1].top = RECT_HEIGHT(bounds);
        ascent += (entries[1].top - lineheight) / 2;
        TopMenuItem = bounds->top;
        draw_item_with_shortcut(bounds, entries, 1, menu, selected, icon_width, shortcut, separator);
    }
    HSetState((Handle)menu, state);
    TopMenuItem = saved_top;
    checksize = saved_check;
    cloversize = saved_clover;
    lineheight = saved_height;
    ascent = saved_ascent;
}

static void draw_menu(MenuHandle mh, Rect* rp, tablePtr tablep)
{
    struct tableentry *tp, *ep;
    int16 topcutoff, bottomcutoff;

    if (CW(TopMenuItem) < CW(rp->top))
        topcutoff = CW(rp->top) - CW(TopMenuItem) + lineheight;
    else
        topcutoff = 0;

    if (CW(AtMenuBottom) > CW(rp->bottom))
        bottomcutoff = CW(rp->bottom) - CW(TopMenuItem) - lineheight;
    else
        bottomcutoff = 32767;

    for (tp = tablep->entry, ep = tp + tablep->count; tp[0].top < bottomcutoff && tp != ep; tp++)
        if (tp[1].top > topcutoff) {
            const int nitem = (tp - tablep->entry) + 1;
            draw_item(rp, tp, nitem, mh, FALSE);
        }
    if (CW(rp->top) > CW(TopMenuItem))
        draw_arrow(rp, mh, uparrow);
    if (CW(rp->bottom) < CW(AtMenuBottom))
        draw_arrow(rp, mh, downarrow);
    HxX(MBSAVELOC, mbUglyScroll) = CWC(0);
}

static void fliprect(Rect* rp, int16 i, tablePtr tablep, Rect* flipr)
{
    struct tableentry* tp;

    tp = &tablep->entry[i - 1];
    flipr->left = rp->left;
    flipr->right = rp->right;
    flipr->top = CW(tp[0].top + CW(TopMenuItem));
    flipr->bottom = CW(tp[1].top + CW(TopMenuItem));
}

static void doupdown(MenuHandle mh, Rect* rp, tablePtr tablep, BOOLEAN upordown, int16* itemp)
{
    INTEGER offset;
    Rect scrollr, updater, rtmp;
    struct tableentry *tp, *ep;
    RgnHandle updatergn;

    if (*itemp) {
        /* flip (rp, CW (*itemp), tablep); */
        draw_item(rp, &tablep->entry[CW(*itemp) - 1], CW(*itemp), mh, FALSE);
        *itemp = CWC(0);
    }
    if (HxX(MBSAVELOC, mbUglyScroll)) {
        /* don't sroll the scroll arrows */
        scrollr = *rp;
        if (TOP_ARROW_P())
            scrollr.top = CW(CW(scrollr.top) + lineheight);
        if (BOTTOM_ARROW_P())
            scrollr.bottom = CW(CW(scrollr.bottom) - lineheight);

        updater = *rp;

        if (upordown == UP) {
            offset = MIN(lineheight, CW(rp->top) - CW(TopMenuItem));
            TopMenuItem = CW(CW(TopMenuItem) + offset);
            AtMenuBottom = CW(CW(AtMenuBottom) + offset);
            if (TOP_ARROW_P()) {
                updater.top = scrollr.top;
                updater.bottom = CW(CW(updater.top) + lineheight);
            }
            else {
                updater.top = rp->top;
                updater.bottom = CW(CW(updater.top) + 2 * lineheight);
                erasearrow(rp, tablep, UP);
            }
        }
        else {
            offset = MAX(-lineheight, CW(rp->bottom) - CW(AtMenuBottom));
            TopMenuItem = CW(CW(TopMenuItem) + offset);
            AtMenuBottom = CW(CW(AtMenuBottom) + offset);
            if (BOTTOM_ARROW_P()) {
                updater.bottom = scrollr.bottom;
                updater.top = CW(CW(updater.bottom) - lineheight);
            }
            else {
                updater.bottom = rp->bottom;
                updater.top = CW(CW(updater.bottom) - 2 * lineheight);
                erasearrow(rp, tablep, DOWN);
            }
        }
        updatergn = NewRgn();
        ScrollRect(&scrollr, 0, offset, updatergn);
        DisposeRgn(updatergn);
        ClipRect(&updater);
        for (tp = tablep->entry, ep = tp + tablep->count; tp != ep && tp[0].top < CW(updater.bottom) - CW(TopMenuItem);
             tp++)
            if (tp[1].top > CW(updater.top) - CW(TopMenuItem))
                draw_item(rp, tp, tp - tablep->entry + 1, mh, FALSE);
        rtmp.top = rtmp.left = CWC(-32767);
        rtmp.bottom = rtmp.right = CWC(32767);
        ClipRect(&rtmp);
    }
    else
        HxX(MBSAVELOC, mbUglyScroll) = CWC(1);
    if (upordown == DOWN) {
        if (CW(AtMenuBottom) >= CW(rp->bottom))
            draw_arrow(rp, mh, downarrow);
    }
    else {
        if (CW(TopMenuItem) <= CW(rp->top))
            draw_arrow(rp, mh, uparrow);
    }
}

void choose_menu(MenuHandle mh, Rect* rp, Point p, int16* itemp, tablePtr tablep)
{
    int32 nitem;
    struct tableentry *tp, *ep;
    Rect valid_rect, clip_rect;
    INTEGER menu_id;

    menu_id = MI_ID(mh);

    valid_rect.left = rp->left;
    valid_rect.right = rp->right;
    valid_rect.top = CW(MAX(CW(rp->top), CW(TopMenuItem)));
    valid_rect.bottom = CW(MIN(CW(rp->bottom), CW(AtMenuBottom)));

    clip_rect.left = rp->left;
    clip_rect.right = rp->right;
    clip_rect.top = CW(TOP_ARROW_P() ? CW(rp->top) + lineheight : CW(rp->top));
    clip_rect.bottom = CW(BOTTOM_ARROW_P() ? CW(rp->bottom) - lineheight : CW(rp->bottom));
    ClipRect(&clip_rect);

    if (CW(*itemp) < 0)
        *itemp = CWC(0);
    if (PtInRect(p, &valid_rect)) {
        if (BOTTOM_ARROW_P() && p.v >= CW(rp->bottom) - lineheight)
            doupdown(mh, rp, tablep, DOWN, itemp);
        else if (TOP_ARROW_P() && p.v < CW(rp->top) + lineheight)
            doupdown(mh, rp, tablep, UP, itemp);
        else {
            for (tp = tablep->entry, ep = tp + tablep->count; tp != ep && p.v >= tp->top + CW(TopMenuItem); tp++)
                ;
            nitem = tp - tablep->entry;
            MenuDisable = CL((menu_id << 16) | (uint16)nitem);

            if (!nitem || !ROMlib_menu_item_enabled(mh, 0) || !ROMlib_menu_item_enabled(mh, nitem) ||
                (tp[-1].name[0] && tp[-1].name[1] == '-'))
                nitem = 0;
            if (CW(*itemp) != nitem) {
                if (*itemp)
                    /* redraw this guy normally */
                    draw_item(rp, &tablep->entry[CW(*itemp) - 1], CW(*itemp), mh, FALSE);
                if (nitem)
                    draw_item(rp, &tablep->entry[nitem - 1], nitem, mh, TRUE);
                *itemp = CW(nitem);
            }
            if (nitem)
                fliprect(rp, nitem, tablep, &HxX(MBSAVELOC, mbItemRect));
        }
    }
    else if (*itemp) {
        nitem = CW(*itemp);
        draw_item(rp, &tablep->entry[nitem - 1], nitem, mh, FALSE);
        *itemp = CWC(0);
    }
    clip_rect.top = clip_rect.left = CWC(-32767);
    clip_rect.bottom = clip_rect.right = CWC(32767);
    ClipRect(&clip_rect);
}

A5(PRIVATE, void, popuprect, MenuHandle, mh, Rect*, rp, Point, p, INTEGER*, itemp, tablePtr, tablep)
{
    struct tableentry* tp;
    INTEGER vmax;

    if (Hx(mh, menuWidth) == -1 || Hx(mh, menuHeight) == -1)
        CalcMenuSize(mh);
    rp->top = CW(p.v - tablep->entry[CW(*itemp) - 1].top);
    rp->left = CW(p.h);
    rp->right = CW(CW(rp->left) + Hx(mh, menuWidth));
    *itemp = rp->top;

    for (tp = tablep->entry; CW(rp->top) < CW(MBarHeight); tp++)
        rp->top = CW(CW(rp->top) + (tp[1].top - tp[0].top));

    rp->bottom = CW(CW(rp->top) + Hx(mh, menuHeight));

    vmax = CW(screenBitsX.bounds.bottom) - 2; /* subtract 2 for frame */
    for (tp = tablep->entry + tablep->count - 1; CW(rp->bottom) > vmax; --tp)
        rp->bottom = CW(CW(rp->bottom) - (tp[1].top - tp[0].top));
    rp->top = CW(CW(rp->bottom) - Hx(mh, menuHeight));
    for (tp = tablep->entry; CW(rp->top) < CW(MBarHeight); tp++)
        rp->top = CW(CW(rp->top) + (tp[1].top - tp[0].top));
}

P5(PUBLIC, pascal void, mdef0, INTEGER, mess, MenuHandle, mh, Rect*, rp, Point, p, INTEGER*, item)
{
    FontInfo fi;
    char* sp;
    INTEGER count, v;
    tableHandle th;
    tablePtr tp;
    struct tableentry* tabp;
    HIDDEN_GrafPtr saveport;

    GetPort(&saveport);
    SignedByte menu_state = HGetState((Handle)mh);
    HLock((Handle)mh);
    SetPort(MR(wmgr_port));

    current_menu_rect = rp;

    const INTEGER old_font = PORT_TX_FONT_X(thePort), old_size = PORT_TX_SIZE_X(thePort);
    const INTEGER old_face = PORT_TX_FACE_X(thePort), old_mode = PORT_TX_MODE_X(thePort);
#define MSWTEST
#if defined(MSWTEST)
    PORT_TX_FONT_X(thePort) = SysFontFam;
    PORT_TX_FACE_X(thePort) = CWC(0);
    PORT_TX_MODE_X(thePort) = CWC(srcOr);
#endif /* MSWTEST */

    const uint32 font = menu_layout_property(mh, 0, TICK("fnsz"));
    if (font) {
        PORT_TX_FONT_X(thePort) = CW((uint16)(font >> 16));
        PORT_TX_SIZE_X(thePort) = CW((uint16)font);
    }
    GetFontInfo(&fi);
    checksize = menu_layout_property(mh, 0, TICK("mark")) ? 0 : CharWidth(checkMark) + 1;
    lineheight = CW(fi.ascent) + CW(fi.descent) + CW(fi.leading);
    ascent = CW(fi.ascent);
    cloversize = CharWidth(commandMark);

    for (sp = (char*)STARH(mh) + SIZEOFMINFO + Hx(mh, menuData[0]), count = 0; *sp;
         sp += (unsigned char)*sp + SIZEOFMEXT, count++)
        ;
    th = (tableHandle)NewHandle((Size)sizeof(table) + count * sizeof(struct tableentry));
    HLock((Handle)th);
    tp = MR(*th);
    tp->lasttick = TickCount();
    tp->count = count;
    v = 0;
    for (sp = (char*)STARH(mh) + SIZEOFMINFO + Hx(mh, menuData[0]), tabp = tp->entry; *sp;
         sp += (unsigned char)*sp + SIZEOFMEXT, tabp++) {
        icon_info_t icon_info;

        tabp->name = (StringPtr)sp;
        tabp->options = (mextp)(sp + (unsigned char)*sp + 1);
        tabp->top = v;
        const INTEGER item = tabp - tp->entry + 1;
        const INTEGER font = menu_item_font(mh, item);
        int height = lineheight;
        if (font) {
            const INTEGER previous_font = PORT_TX_FONT_X(thePort);
            const INTEGER previous_face = PORT_TX_FACE_X(thePort);
            FontInfo metrics;
            TextFont(font);
            TextFace(tabp->options->mstyle);
            GetFontInfo(&metrics);
            height = MAX(height, CW(metrics.ascent) + CW(metrics.descent) + CW(metrics.leading));
            PORT_TX_FONT_X(thePort) = previous_font;
            PORT_TX_FACE_X(thePort) = previous_face;
        }
        menu_item_icon_info(mh, item, tabp, &icon_info, FALSE);
        v += MAX(icon_info.height, height);
    }
    tabp->top = v;
    AtMenuBottom = CW(CW(TopMenuItem) + v);

    switch (mess) {
    case mDrawMsg:
        draw_menu(mh, rp, tp);
        break;
    case mChooseMsg:
        choose_menu(mh, rp, p, item, tp);
        break;
    case mSizeMsg:
        size_menu(mh, tp);
        break;
    case mPopUpRect: {
        /*
     * MacWriteII seems to like the 'h' and 'v' reversed.
     */
        INTEGER temp;

        temp = p.h;
        p.h = p.v;
        p.v = temp;
    }
        popuprect(mh, rp, p, item, tp);
        break;
    }
    HUnlock((Handle)th);
    DisposHandle((Handle)th);
    HSetState((Handle)mh, menu_state);
    PORT_TX_FONT_X(thePort) = old_font;
    PORT_TX_SIZE_X(thePort) = old_size;
    PORT_TX_FACE_X(thePort) = old_face;
    PORT_TX_MODE_X(thePort) = old_mode;
    SetPort(HIDDEN_NATIVE(saveport));
}
