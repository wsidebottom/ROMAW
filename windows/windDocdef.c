/*+===================================================================
  File:      windDocdef.c

  Summary:   Implements wind docdef support for Macintosh Window Manager operations.

  Classes:   None.

  Functions: ROMlib_window_zoomed, calc_alt_dialog_box, calc_dialog_box, calc_doc, calc_rounded_doc,
             draw_alt_dialog_box, draw_dialog_box, draw_frame, draw_grow_icon, draw_grow_lines, draw_plain_dialog_box,
             draw_rounded_doc, draw_title, draw_wind_close, draw_zoom, erase_grow_icon, hilite_rounded_window,
             hilite_window, hit_dialog_box, hit_doc, hit_rounded_doc, toggle_box_active, toggle_wind_close_box,
             toggle_zoom_box, validate_colors_for_window

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/classic_ui_qd.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "WindowMgr.h"
#include "ToolboxUtil.h"

#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/ctl.h"

#include "rsys/image.h"

#include "generated/wind_zoom.inc"
#include "generated/wind_close.inc"
#include "generated/wind_close_clicked.inc"
#include "generated/wind_grow.inc"

#if 1

PUBLIC BOOLEAN ROMlib_window_zoomed(WindowPeek wp)
{
    BOOLEAN retval;
    const Rect *portrp, *staterp, *boundsrp;

    portrp = &PORT_RECT(wp);
    staterp = &MR(*(WStateData**)WINDOW_DATA(wp))->stdState;
    boundsrp = &PORT_BOUNDS(wp);

    retval = (WINDOW_SPARE_FLAG_X(wp) && CW(portrp->top) - CW(boundsrp->top) == CW(staterp->top) &&
              CW(portrp->left) - CW(boundsrp->left) == CW(staterp->left) && RECT_WIDTH(portrp) == RECT_WIDTH(staterp) &&
              RECT_HEIGHT(portrp) == RECT_HEIGHT(staterp));
    return retval;
}

#define WINDOW_ZOOMED(w) (ROMlib_window_zoomed(w))

#endif

/* `color_p' is TRUE if the current call to the window definition
   function is for a while to be drawn in color */
static int color_p;

/* TRUE if the current window being serviced is a rounded window */
static int rounded_window_p;

/* colors to draw the current window with; has the appropriate
   black and white colors if `!color_p' */
static RGBColor* window_colors;

#define content (&window_colors[wContentColor])
/* FIXME: hack */
#undef frame
#define frame (&window_colors[wFrameColor])
#define text (&window_colors[wTextColor])
#undef hilite
#define hilite (&window_colors[wHiliteColor])
#define title_bar (&window_colors[wTitleBarColor])
#define title_bar (&window_colors[wTitleBarColor])
#define hilite_light (&window_colors[wHiliteColorLight])
#define hilite_dark (&window_colors[wHiliteColorDark])
#define title_bar_light (&window_colors[wTitleBarLight])
#define title_bar_dark (&window_colors[wTitleBarDark])
#define dialog_light (&window_colors[wDialogLight])
#define dialog_dark (&window_colors[wDialogDark])
#define tinge_light (&window_colors[wTingeLight])
#define tinge_dark (&window_colors[wTingeDark])

#define title_bar_bk (&window_colors[13])
#define title (&window_colors[14])
#define frame_outline (&window_colors[15])
#define half_tinge (&window_colors[16])
#define lines (&window_colors[17])
#define half_dialog (&window_colors[18])

/* aliases */
#define frame_shadow frame

RGBColor* validate_colors_for_window(GrafPtr w)
{
    static RGBColor color_window_colors[19];
    /* just make the unused color something noticable */
#define UNUSED {CWC(0xFFFF), CWC(0), CWC(0)}
#define WHITE {CWC(0xFFFF), CWC(0xFFFF), CWC(0xFFFF)}
#define BLACK {CWC(0), CWC(0), CWC(0)}
    static RGBColor bw_window_colors[19] = {
        WHITE,  /* content */
        BLACK,  /* frame */
        UNUSED, /* text */
        UNUSED, /* hilite */
        UNUSED, /* title_bar */
        UNUSED, /* hilite_light */
        UNUSED, /* hilite_dark */
        UNUSED, /* title_bar_light */
        UNUSED, /* title_bar_dark */
        UNUSED, /* dialog_light */
        BLACK,  /* dialog_dark */
        WHITE,  /* tinge_light */
        BLACK,  /* tinge_dark */
        WHITE,  /* title_bar_bk */
        BLACK,  /* title */
        BLACK,  /* frame_outline */
        UNUSED, /* half_tinge */
        BLACK,  /* lines */
        UNUSED, /* half_dialog */
    };
#undef BLACK
#undef WHITE
#undef UNUSED
    RGBColor* window_colors;
    AuxWinHandle t_aux_w;
    int i;

    /* FIXME: assuming that colors not specified come from the default
     color table; verify this */
    for (i = 0; i <= 12; i++)
        color_window_colors[i] = default_color_win_ctab[i].rgb;

    t_aux_w = HIDDEN_NATIVE(*lookup_aux_win(w));
    if (t_aux_w && PP_RAW(HxX(t_aux_w, awCTable))) {
        CTabHandle w_ctab;
        ColorSpec* w_ctab_table;
        int w_ctab_size;

        w_ctab = HxP(t_aux_w, awCTable);
        if (!w_ctab || !STARH(w_ctab)) {
            fprintf(stderr, "trace: validate_colors_for_window empty color table w=%p aux=%p ctab=%p trap=0x%04x\n",
                    (void*)w, (void*)t_aux_w, (void*)w_ctab, (unsigned)mostrecenttrap);
            fflush(stderr);
            goto skip_window_ctab;
        }
        w_ctab_table = CTAB_TABLE(w_ctab);
        Size table_bytes = GetHandleSize((Handle)w_ctab);
        if (table_bytes < (Size)offsetof(ColorTable, ctTable))
            goto skip_window_ctab;
        Size available_entries = (table_bytes - offsetof(ColorTable, ctTable)) / sizeof(ColorSpec);
        if (!available_entries)
            goto skip_window_ctab;
        /* this window definition function uses only the first 12
	 entries in the window color table */
        w_ctab_size = MIN(CTAB_SIZE(w_ctab), available_entries - 1);
        if (w_ctab_size > 12) {
            warning_unexpected("window color table with size `%d' > 12; ignored", w_ctab_size);
            w_ctab_size = 0;
        }

        /* blow off system6 style color tables */
        if (w_ctab_size <= 4 && 0) /* really blow off? Thresholder suggests
				    otherwise.  I'm leaving this code in
				    for now because there's probably a
				    reason it was in there in the first
				    place.  Perhaps the <= should be < */
        {
            /* same content color */
            for (i = 0; i <= w_ctab_size; i++)
                if (w_ctab_table[i].value == CW(wContentColor))
                    color_window_colors[wContentColor] = w_ctab_table[i].rgb;
        }
        else {
            for (i = 0; i <= w_ctab_size; i++) {
                ColorSpec* w_ctab_entry;
                int w_ctab_entry_index;

                w_ctab_entry = &w_ctab_table[i];
                w_ctab_entry_index = CW(w_ctab_entry->value);
                if (w_ctab_entry_index < 0 || w_ctab_entry_index > 12) {
#if !defined(CYGWIN32) /* just gets in the way of debugging under windows */
                    warning_unexpected("window color table with index `%d' > 12 or < 0; ignored", w_ctab_entry_index);
#endif
                    continue;
                }
                color_window_colors[w_ctab_entry_index] = w_ctab_entry->rgb;
            }
        }
    }
skip_window_ctab:

    if (t_aux_w && (Hx(t_aux_w, awFlags) & ROMlib_theme_window_background))
        color_window_colors[wContentColor] = ROMlib_theme_background_rgb;

    bw_window_colors[wContentColor] = color_window_colors[wContentColor];

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
        /* fall through */                           \
    done:;                                           \
    }

    DO_BLOCK_WITH_FAILURE(
        {
            RGBColor image_colors[5];
            RGBColor temp1;
            RGBColor temp2;
            RGBColor temp3;

            window_colors = color_window_colors;

            if (rounded_window_p) {
                if (WINDOW_HILITED_X(w)) {
                    *title_bar_bk = *frame;
                    *title = *title_bar;
                }
                else {
                    *title = *frame;
                    *title_bar_bk = *title_bar;
                }
                DONE;
            }

            if (!AVERAGE_COLOR(title_bar_light, title_bar_dark, 0xEEEE, &temp1))
                FAIL;
            if (!AVERAGE_COLOR(hilite_light, &ROMlib_black_rgb_color, 0x8888, &temp2))
                FAIL;
            if (!AVERAGE_COLOR(hilite_light, &ROMlib_black_rgb_color, 0x5555, &temp3))
                FAIL;

            if (WINDOW_HILITED_X(w)) {
                *title_bar_bk = temp1;
                *title = *text;
                *frame_outline = *frame;
            }
            else {
                *title = temp2;
                *title_bar_bk = *content;
                *frame_outline = temp3;
            }
            if (!AVERAGE_COLOR(tinge_light, tinge_dark, 0xAAAA, half_tinge))
                FAIL;
            if (!AVERAGE_COLOR(title_bar_light, title_bar_dark, 0xAAAA, lines))
                FAIL;
            if (!AVERAGE_COLOR(dialog_light, dialog_dark, 0x8888, half_dialog))
                FAIL;

            /* now do the image colors */
            image_colors[0] = *title_bar_bk;
            image_colors[1] = *tinge_light;
            if (!AVERAGE_COLOR(title_bar_light, title_bar_dark, 0xAAAA, &image_colors[2]))
                FAIL;
            image_colors[3] = *tinge_dark;
            /* used only in the grow icon */
            if (!AVERAGE_COLOR(title_bar_light, title_bar_dark, 0xBBBB, &image_colors[4]))
                FAIL;

            image_update_ctab(wind_close, image_colors, 3);
            image_update_ctab(wind_zoom, image_colors, 3);
            image_update_ctab(wind_close_clicked, image_colors, 3);
            image_update_ctab(wind_grow, image_colors, 4);

            color_p = TRUE;
        },
        {
            gui_assert(!rounded_window_p);

            window_colors = bw_window_colors;
            color_p = FALSE;
        });
#undef DO_BLOCK_WITH_FAILURE
#undef FAIL

    return window_colors;
}

enum { compact_title_height = 14, compact_frame_inset = 3 };

enum box_flag {
    zoom_box_flag,
    wind_close_box_flag,
};

void toggle_box_active(enum box_flag which_box, Point origin, int size)
{
    Rect dst_rect;
    pixel_image_t* box;

    /* compute the destination rectangle */
    dst_rect.top = CW(origin.v);
    dst_rect.left = CW(origin.h);
    dst_rect.bottom = CW(origin.v + size);
    dst_rect.right = CW(origin.h + size);

    /* set box */
    if (rounded_window_p)
        color_p = FALSE;

    if (which_box == zoom_box_flag)
        box = wind_zoom;
    else if (which_box == wind_close_box_flag)
        box = wind_close;
    else
        gui_abort(); /* there are no more boxes */

    if (rounded_window_p) {
        /* invert the set bits of the box */
        PORT_FG_COLOR_X(thePort) = CL((1 << PIXMAP_PIXEL_SIZE(CPORT_PIXMAP(thePort))) - 1);
        PORT_FG_COLOR_X(thePort) = CLC(0);
    }
    else {
        /* make sure the fg/bk colors are b/w so as not to confuse the
	 `CopyBits ()' */
        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);
    }

    image_copy(box, color_p, &dst_rect, srcXor);

    if (rounded_window_p) {
        RGBColor* target_color;

        /* this isn't quite what the mac does, but i couldn't figure
	 that one out.  it works for the common case (white on block
	 rounded window).  the mac function takes into account both
	 `title' and `title_bar' in some unknown fasion */
        if (((title_bar->red + title_bar->green + title_bar->blue) / 3) > 0x8000)
            target_color = &ROMlib_black_rgb_color;
        else
            target_color = &ROMlib_white_rgb_color;

        PORT_FG_COLOR_X(thePort) = CL(Color2Index(target_color) ^ Color2Index(frame));
    }

    image_copy(wind_close_clicked, color_p, &dst_rect, srcXor);
}

void toggle_zoom_box(GrafPtr w, boolean_t compact)
{
    Point origin;

    origin.h = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left) + (compact ? -12 : -21);
    origin.v = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top) - (compact ? 11 : 16);

    toggle_box_active(zoom_box_flag, origin, compact ? 9 : 13);
}

void toggle_wind_close_box(GrafPtr w, boolean_t compact)
{
    Point origin;

    origin.h = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left) + (compact ? 3 : 8);
    origin.v = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top) - (compact ? 11 : 16);

    toggle_box_active(wind_close_box_flag, origin, compact ? 9 : 13);
}

/* Map colour roles to the existing 16-bit Toolbox palette, without converting
   guest records or colours inside the shared drawing module. */
static ROMAWClassicCanvas document_canvas(RGBColor** roles)
{
    roles[ROMAW_CLASSIC_TITLE_BACKGROUND] = title_bar_bk;
    roles[ROMAW_CLASSIC_FRAME_OUTLINE] = frame_outline;
    roles[ROMAW_CLASSIC_FRAME_SHADOW] = frame_shadow;
    roles[ROMAW_CLASSIC_TITLE_LIGHT] = tinge_light;
    roles[ROMAW_CLASSIC_TITLE_SHADOW] = half_tinge;
    roles[ROMAW_CLASSIC_TITLE_RULE] = lines;
    return romaw_classic_quickdraw(roles);
}

void hilite_window(int left, int top, int right, int height)
{
    RGBColor* roles[ROMAW_CLASSIC_COLOR_COUNT] = {0};
    ROMAWClassicCanvas canvas = document_canvas(roles);
    if (color_p)
        romaw_classic_title_bevel(&canvas, left, top, right, height);
    romaw_classic_title_rules(&canvas, left, top, right, height);
}

void draw_title(GrafPtr w, int wind_close_drawn, int zoom_drawn, boolean_t compact)
{
    HIDDEN_GrafPtr tp;
    int left, top, right, bottom;
    StringHandle th;
    RgnHandle saveclip = NULL;
    int title_width;
    int title_start;
    int left_bound;
    int text_top = compact ? 11 : 16;
    int padding = compact ? 3 : 6;
    int box_space = compact ? 14 : 28;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    /* #warning "clean up this port mess in draw_title ()" */
    GetPort(&tp);

    if (wind_close_drawn)
        left_bound = left + box_space;
    else
        left_bound = left;

    title_width = WINDOW_TITLE_WIDTH(w);
    if (compact && title_width) {
        TextFont(0);
        TextSize(9);
        th = WINDOW_TITLE(w);
        LOCK_HANDLE_EXCURSION_1(th, { title_width = StringWidth(STARH(th)); });
    }
    if (title_width) {
        title_start = left + (right - left - title_width) / 2 - padding;
        saveclip = PORT_CLIP_REGION(HIDDEN_NATIVE(tp));
        FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(HIDDEN_NATIVE(tp)), RM(NewRgn()));
        if (title_start >= left_bound)
            CopyRgn(saveclip, PORT_CLIP_REGION(HIDDEN_NATIVE(tp)));
        else {
            title_start = left_bound;
            SetRectRgn(PORT_CLIP_REGION(HIDDEN_NATIVE(tp)), title_start, top - text_top,
                       right - (zoom_drawn ? box_space : 0), top - 3);
            SectRgn(PORT_CLIP_REGION(HIDDEN_NATIVE(tp)), saveclip, PORT_CLIP_REGION(HIDDEN_NATIVE(tp)));
        }

        {
            RgnHandle additional_clip_rgn;

            additional_clip_rgn = NewRgn();
            SetRectRgn(additional_clip_rgn, title_start, top - text_top, title_start + title_width + 2 * padding,
                       top - 2);
            SectRgn(PORT_CLIP_REGION(HIDDEN_NATIVE(tp)), additional_clip_rgn, PORT_CLIP_REGION(HIDDEN_NATIVE(tp)));

            /* erase the text to be drawn with the title bar background
	   color */
            RGBForeColor(title_bar_bk);
            PaintRgn(additional_clip_rgn);
            DisposeRgn(additional_clip_rgn);
        }

        RGBBackColor(title_bar_bk);
        RGBForeColor(title);

        th = WINDOW_TITLE(w);
        LOCK_HANDLE_EXCURSION_1(th, {
            PORT_TX_MODE_X(thePort) = CWC(srcCopy);
            MoveTo(title_start + padding, top - (compact ? 3 : 5));
            DrawString(STARH(th));
        });

        if (saveclip) {
            DisposeRgn(PORT_CLIP_REGION(HIDDEN_NATIVE(tp)));
            FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(HIDDEN_NATIVE(tp)), RM(saveclip));
        }
    }
}

void draw_wind_close(int h, int v, boolean_t compact)
{
    Rect r;

    if (rounded_window_p) {
        RGBForeColor(title_bar);
        RGBBackColor(title_bar_bk);
    }
    else {
        /* make sure the fg/bk colors are b/w so as not to confuse the
	 `CopyBits ()' */
        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);
    }

    if (compact)
        SetRect(&r, h + 3, v - 11, h + 12, v - 2);
    else
        SetRect(&r, h + 8, v - 16, h + 24, v);

    image_copy(wind_close, rounded_window_p ? 0 : color_p, &r, srcCopy);
}

void draw_zoom(int h, int v, boolean_t compact)
{
    Rect r;

    if (rounded_window_p) {
        RGBForeColor(title_bar);
        RGBBackColor(title_bar_bk);
    }
    else {
        /* make sure the fg/bk colors are b/w so as not to confuse the
	 `CopyBits ()' */
        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);
    }

    if (compact)
        SetRect(&r, h - 12, v - 11, h - 3, v - 2);
    else
        SetRect(&r, h - 21, v - 16, h - 5, v);

    image_copy(wind_zoom, rounded_window_p ? 0 : color_p, &r, srcCopy);
}

void draw_frame(GrafPtr w, int draw_zoom_p, boolean_t goaway_override, boolean_t compact)
{
    int left, top, right, bottom;
    Rect r;
    int inset = compact ? compact_frame_inset : 1;
    int height = compact ? compact_title_height : ROMAW_CLASSIC_TITLE_HEIGHT;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    PenSize(1, 1);
    /* draw with the current foreground color */
    PenPat(black);
    PenMode(patCopy);

    if (!compact) {
        RGBColor* roles[ROMAW_CLASSIC_COLOR_COUNT] = {0};
        ROMAWClassicCanvas canvas = document_canvas(roles);
        ROMAWClassicRect bounds = {left, top, right, bottom};
        romaw_classic_document_frame(&canvas, &bounds, height);
    }
    else {
        /* bounds of the title bar */
        SetRect(&r, left - inset, top - height, right + inset, top);
        /* clear the title bar with the titlebar background color; title bar
         color is dependent on whether or not the window is hilited,
         `validate_colors_for_window ()' takes this into account */
        RGBForeColor(title_bar_bk);
        PaintRect(&r);

        /* draw the frame; these are drawn in the frame_outline_color */
        RGBForeColor(frame_outline);
        r.bottom = CW(bottom + inset);
        FrameRect(&r);
        if (compact) {
            Rect inner;
            SetRect(&inner, left - 1, top - 1, right + 1, bottom + 1);
            FrameRect(&inner);
            RGBForeColor(title_bar_bk);
            MoveTo(left - 2, top);
            LineTo(left - 2, bottom + 1);
            LineTo(right + 1, bottom + 1);
            LineTo(right + 1, top);
            RGBForeColor(frame_outline);
        }
        MoveTo(left, top - 1);
        LineTo(right, top - 1);

        /* draw the 'shadow' */
        RGBForeColor(frame_shadow);
        MoveTo(left, bottom + inset);
        LineTo(right + inset, bottom + inset);
        MoveTo(right + inset, top - height + 1);
        LineTo(right + inset, bottom + inset - 1);
    }

    if (WINDOW_HILITED_X(w)) {
        int draw_wind_close_p = WINDOW_GO_AWAY_FLAG(w) && !goaway_override;

        hilite_window(left, top, right, height);
        if (draw_wind_close_p)
            draw_wind_close(left, top, compact);
        if (draw_zoom_p)
            draw_zoom(right, top, compact);
        draw_title(w, draw_wind_close_p, draw_zoom_p, compact);
    }
    else
        draw_title(w, FALSE, FALSE, compact);

    /* #warning "delete these, they shouldn't be necessary" */
    /* be a sneaky bastard, and set the fg/bk color to b/w */
    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);
}

void draw_grow_lines(Rect* bounds, int title_height)
{
    int left, top, right, bottom;
    Rect r;

    left = CW(bounds->left);
    top = CW(bounds->top);
    right = CW(bounds->right);
    bottom = CW(bounds->bottom);

    PenSize(1, 1);
    SetRect(&r, left - 1, top - title_height, right + 1, bottom + 1);
    FrameRect(&r); /* outside */

    MoveTo(left, top - 1);
    LineTo(right, top - 1); /* under title */

    MoveTo(left, bottom - 15);
    LineTo(right - 1, bottom - 15); /* at bottom */

    MoveTo(right - 15, top);
    LineTo(right - 15, bottom - 1); /* at side */
}

void draw_dialog_box(GrafPtr w)
{
    int left, top, right, bottom;
    Rect r;
    RGBColor middle_color;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    PenSize(1, 1);
    PenMode(patCopy);

    /* draw the outtermost rectangle; in the frame_shadow_color */
    RGBForeColor(frame_shadow);
    SetRect(&r, left - 8, top - 8, right + 8, bottom + 8);
    FrameRect(&r);

    /* draw the center rectangle */
    if (color_p) {
        middle_color.red = 0xBBBB;
        middle_color.green = 0xBBBB;
        middle_color.blue = 0xBBBB;
    }
    else
        middle_color = ROMlib_white_rgb_color;

    RGBForeColor(&middle_color);
    InsetRect(&r, 2, 2);
    FrameRect(&r);

    /* draw the innermost rectangle; in the dark dialog color */
    RGBForeColor(dialog_dark);
    InsetRect(&r, 2, 2);
    FrameRect(&r);

    /* FIXME:
     go in one more pixel, and white out a rect
     three pixels wide; having to do this worries
     me that the location of the border may be wrong,
     resolve this */
    InsetRect(&r, 1, 1);
    PenSize(3, 3);
    RGBForeColor(content);
    FrameRect(&r);
    PenSize(1, 1);

    if (color_p) {
        /* and now for the inbetween hiliting */
        RGBForeColor(dialog_light);
        MoveTo(left - 7, bottom + 6);
        LineTo(left - 7, top - 7);
        LineTo(right + 6, top - 7);

        RGBForeColor(half_dialog);
        LineTo(right + 6, bottom + 6);
        LineTo(left - 7, bottom + 6);

        /* same color; half_dialog_color */
        MoveTo(left - 5, bottom + 4);
        LineTo(left - 5, top - 5);
        LineTo(right + 4, top - 5);

        RGBForeColor(dialog_light);
        MoveTo(right + 4, top - 4);
        LineTo(right + 4, bottom + 4);
        LineTo(left - 4, bottom + 4);
    }
    else {
        RGBForeColor(&ROMlib_black_rgb_color);
        InsetRect(&r, -2, -2);
        FrameRect(&r);

        RGBForeColor(&ROMlib_white_rgb_color);
        InsetRect(&r, -2, -2);
        FrameRect(&r);
    }
}

void draw_plain_dialog_box(GrafPtr w)
{
    int left, top, right, bottom;
    Rect r;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    PenSize(1, 1);
    PenMode(patCopy);
    SetRect(&r, left - 1, top - 1, right + 1, bottom + 1);
    RGBForeColor(frame_outline);
    FrameRect(&r);
}

void draw_alt_dialog_box(GrafPtr w)
{
    int left, top, right, bottom;
    Rect r;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    PenSize(1, 1);
    PenMode(patCopy);
    SetRect(&r, left - 1, top - 1, right + 1, bottom + 1);
    RGBForeColor(frame_outline);
    FrameRect(&r);

    PenSize(2, 2);
    RGBForeColor(frame_outline);
    MoveTo(left + 1, bottom + 1);
    LineTo(right + 1, bottom + 1);
    MoveTo(right + 1, top + 1);
    LineTo(right + 1, bottom);

    PenSize(1, 1);
}

void draw_grow_icon(GrafPtr w)
{
    int left, top, right, bottom;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    PenPat(black);
    PenSize(1, 1);
    PenMode(patCopy);

    /* NOTE: IMI-302 says we should be drawing the lines here, but
     Microsoft Excel suggests otherwise (choose Number from the format
     menu and then type clover-/) */

    MoveTo(left, bottom - 15);
    LineTo(right - 1, bottom - 15); /* line at bottom */

    MoveTo(right - 15, top);
    LineTo(right - 15, bottom - 1); /* line at side */

    {
        Rect rect;

        rect.top = CW(bottom - 14);
        rect.left = CW(right - 14);
        rect.bottom = CW(bottom + 2);
        rect.right = CW(right + 2);

        image_copy(wind_grow, color_p, &rect, srcCopy);
    }
}

void erase_grow_icon(GrafPtr w)
{
    int left, top, right, bottom;
    Rect r;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    PenPat(black);
    PenSize(1, 1);
    PenMode(patCopy);
    MoveTo(left, bottom - 15);
    LineTo(right - 1, bottom - 15);
    MoveTo(right - 15, top);
    LineTo(right - 15, bottom - 1);
    SetRect(&r, right - 14, bottom - 14, right + 2, bottom + 2);
    FillRect(&r, white);
}

LONGINT
hit_doc(WindowPeek w, LONGINT parm, int growable_p, boolean_t goaway_override, boolean_t compact)
{
    Point p;
    int left, top, right, bottom;
    int inset = compact ? compact_frame_inset : 1;
    int height = compact ? compact_title_height : ROMAW_CLASSIC_TITLE_HEIGHT;
    int box_inset = compact ? 3 : 8;
    int box_far = compact ? 12 : 20;
    int box_top = compact ? 11 : 16;
    int box_bottom = compact ? 2 : 4;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    p.v = HiWord(parm);
    p.h = LoWord(parm);
    if (WINDOW_HILITED_X(w)) {
        if (PtInRgn(p, WINDOW_CONT_REGION(w))) {
            if (!growable_p || (p.h < right - 16) || (p.v < bottom - 16))
                return wInContent;
            else
                return wInGrow;
        }
        if ((p.h >= left - inset) && (p.h < right + inset) && (p.v >= top - height) && (p.v < top)) {
            if (WINDOW_GO_AWAY_FLAG_X(w) && !goaway_override && (p.h > left + box_inset) && (p.h < left + box_far) &&
                (p.v > top - box_top) && (p.v < top - box_bottom))
                return wInGoAway;
            else if (WINDOW_SPARE_FLAG_X(w) && (p.h > right - box_far) && (p.h < right - box_inset) &&
                     (p.v > top - box_top) && (p.v < top - box_bottom))
                return WINDOW_ZOOMED((WindowPeek)w) ? wInZoomIn : wInZoomOut;
            else
                return wInDrag;
        }
        else
            return wNoHit;
    }
    else {
        if (PtInRgn(p, WINDOW_CONT_REGION(w)))
            return wInContent;
        else if ((p.h >= left - inset) && (p.h < right + inset) && (p.v >= top - height) && (p.v < top))
            return wInDrag;
        else
            return wNoHit;
    }
}

LONGINT
hit_dialog_box(WindowPeek w, LONGINT parm)
{
    Point p;

    p.v = HiWord(parm);
    p.h = LoWord(parm);
    if (PtInRgn(p, WINDOW_CONT_REGION(w)))
        return wInContent;
    else
        return wNoHit;
}

void calc_doc(GrafPtr w, int title_height)
{
    RgnHandle rh;
    INTEGER* ip;
    int left, top, right, bottom;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    SetRectRgn(WINDOW_CONT_REGION(w), left, top, right, bottom);

    rh = WINDOW_STRUCT_REGION(w);
    if (title_height == compact_title_height) {
        SetRectRgn(rh, left - compact_frame_inset, top - title_height, right + compact_frame_inset + 1,
                   bottom + compact_frame_inset + 1);
        return;
    }
    ReallocHandle((Handle)rh, (Size)44);
    HxX(rh, rgnBBox.left) = CW(left - 1);
    HxX(rh, rgnBBox.top) = CW(top - title_height);
    HxX(rh, rgnBBox.right) = CW(right + 2);
    HxX(rh, rgnBBox.bottom) = CW(bottom + 2);
    HxX(rh, rgnSize) = CWC(44);
    ip = (INTEGER*)STARH(rh) + 5;

    *ip++ = CW(top - title_height);
    *ip++ = CW(left - 1);
    *ip++ = CW(right + 1);
    *ip++ = CWC(32767);

    *ip++ = CW(top - title_height + 1);
    *ip++ = CW(right + 1);
    *ip++ = CW(right + 2);
    *ip++ = CWC(32767);

    *ip++ = CW(bottom + 1);
    *ip++ = CW(left - 1);
    *ip++ = CW(left);
    *ip++ = CWC(32767);

    *ip++ = CW(bottom + 2);
    *ip++ = CW(left);
    *ip++ = CW(right + 2);
    *ip++ = CWC(32767);

    *ip++ = CWC(32767);
}

void calc_alt_dialog_box(GrafPtr w)
{
    RgnHandle rh;
    INTEGER* ip;
    int left, top, right, bottom;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    SetRectRgn(WINDOW_CONT_REGION(w), left, top, right, bottom);

    rh = WINDOW_STRUCT_REGION(w);

    ReallocHandle((Handle)rh, (Size)44);
    HxX(rh, rgnBBox.left) = CW(left - 1);
    HxX(rh, rgnBBox.top) = CW(top - 1);
    HxX(rh, rgnBBox.right) = CW(right + 3);
    HxX(rh, rgnBBox.bottom) = CW(bottom + 3);
    HxX(rh, rgnSize) = CWC(44);
    ip = (INTEGER*)STARH(rh) + 5;

    *ip++ = CW(top - 1);
    *ip++ = CW(left - 1);
    *ip++ = CW(right + 1);
    *ip++ = CWC(32767);

    *ip++ = CW(top + 1);
    *ip++ = CW(right + 1);
    *ip++ = CW(right + 3);
    *ip++ = CWC(32767);

    *ip++ = CW(bottom + 1);
    *ip++ = CW(left - 1);
    *ip++ = CW(left + 1);
    *ip++ = CWC(32767);

    *ip++ = CW(bottom + 3);
    *ip++ = CW(left + 1);
    *ip++ = CW(right + 3);
    *ip++ = CWC(32767);

    *ip++ = CWC(32767);
}

void calc_dialog_box(GrafPtr w, INTEGER n)
{
    int left, top, right, bottom;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    SetRectRgn(WINDOW_CONT_REGION(w), left, top, right, bottom);
    SetRectRgn(WINDOW_STRUCT_REGION(w), left - n, top - n, right + n, bottom + n);
}

struct draw_save {
    PenState pen_state;
    RGBColor fg_color;
    RGBColor bk_color;
    LONGINT fg;
    LONGINT bk;
};

P4(PUBLIC pascal, LONGINT, wdef0, INTEGER, varcode, WindowPtr, window, INTEGER, message, LONGINT, parm)
{
    WindowPeek w = (WindowPeek)window;
    draw_state_t draw_state;
    int zoom_bit;
    boolean_t draw_p;
    AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win(window));
    boolean_t compact = aux && (Hx(aux, awFlags) & ROMlib_compact_window_frame);
    int title_height = compact ? compact_title_height : 19;

    rounded_window_p = FALSE;

    /* extract and clean ZOOMBIT from varcode into zoom_bit */
    zoom_bit = varcode & ZOOMBIT;
    varcode &= ~ZOOMBIT;

    if (varcode > 4 && varcode != 5)
        varcode &= 3;

    draw_p = (message == wDraw || message == wDrawGIcon || message == wGrow);

    if (draw_p) {
        draw_state_save(&draw_state);

        window_colors = validate_colors_for_window((GrafPtr)w);
    }

    switch (message) {
    case wDraw:
        if (!WINDOW_VISIBLE_X(w))
            return 0;
        PenNormal();
        TRAPBEGIN();
        switch (varcode) {
        case documentProc:
        case noGrowDocProc:
        case movableDBoxProc:
            switch (parm) {
            case 0: /* draw entire window frame */
                draw_frame((GrafPtr)w, zoom_bit, varcode == movableDBoxProc, compact);
                break;
            case wInGoAway:
                toggle_wind_close_box((GrafPtr)w, compact);
                break;
            case wInZoomIn:
            case wInZoomOut:
                toggle_zoom_box((GrafPtr)w, compact);
                break;
            }
            break;
        case dBoxProc:
            draw_dialog_box((GrafPtr)w);
            break;
        case plainDBox:
            draw_plain_dialog_box((GrafPtr)w);
            break;
        case altDBoxProc:
            draw_alt_dialog_box((GrafPtr)w);
            break;
        }
        TRAPEND();
        break;
    case wHit:
        switch (varcode) {
        case documentProc:
            return hit_doc(w, parm, TRUE, FALSE, compact);
        case noGrowDocProc:
        case movableDBoxProc:
            return hit_doc(w, parm, FALSE, varcode == movableDBoxProc, compact);
        default:
            return hit_dialog_box(w, parm);
        }
        /* control never gets here */
        gui_abort();
    case wCalcRgns:
        switch (varcode) {
        case documentProc:
        case noGrowDocProc:
        case movableDBoxProc:
#warning TODO: correct frame for movableDBoxProc (toolbox essentials 4-10)
            calc_doc((GrafPtr)w, title_height);
            break;
        case dBoxProc:
            calc_dialog_box((GrafPtr)w, 8);
            break;
        case plainDBox:
            calc_dialog_box((GrafPtr)w, 1);
            break;
        case altDBoxProc:
            calc_alt_dialog_box((GrafPtr)w);
            break;
        }
        break;
    case wNew:
        if (zoom_bit) {
            WStateData* wsp;

            FIELD_ASSIGN_RAW(WINDOW_DATA_X(w), RM(NewHandle((Size)sizeof(WStateData))));
            wsp = MR(*(WStateData**)WINDOW_DATA(w));

            wsp->stdState = GD_BOUNDS(MR(TheGDevice));
            InsetRect(&wsp->stdState, 3, 3);
            wsp->stdState.top = CW(CW(wsp->stdState.top) + 38);
            wsp->userState = PORT_RECT(w);

            /* local to global */
            OffsetRect(&wsp->userState, -CW(PORT_BOUNDS(w).left), -CW(PORT_BOUNDS(w).top));

            WINDOW_SPARE_FLAG_X(w) = TRUE;
        }
        else
            WINDOW_SPARE_FLAG_X(w) = FALSE;
        break;
    case wDispose:
        if (WINDOW_SPARE_FLAG_X(w))
            DisposHandle(WINDOW_DATA(w));
        break;
    case wGrow:
        TRAPBEGIN();
        draw_grow_lines((Rect*)SYN68K_TO_US(parm), title_height);
        TRAPEND();
        break;
    case wDrawGIcon: {
        /* clip the growicon to the window vis/clip regions */
        RgnHandle temp_rgn;
        RgnHandle save_clip;

        temp_rgn = NewRgn();
        save_clip = NewRgn();

        SectRgn(PORT_VIS_REGION(w), PORT_CLIP_REGION(w), temp_rgn);

        OffsetRgn(temp_rgn, -CW(PORT_BOUNDS(w).left), -CW(PORT_BOUNDS(w).top));

        CopyRgn(PORT_CLIP_REGION(thePort), save_clip);
        SectRgn(PORT_CLIP_REGION(thePort), temp_rgn, PORT_CLIP_REGION(thePort));

        TRAPBEGIN();
        if (varcode == documentProc) {
            if (WINDOW_HILITED_X(w))
                draw_grow_icon((GrafPtr)w);
            else
                erase_grow_icon((GrafPtr)w);
        }
        TRAPEND();

        CopyRgn(save_clip, PORT_CLIP_REGION(thePort));

        DisposeRgn(temp_rgn);
        DisposeRgn(save_clip);

        break;
    }
    }

    if (draw_p)
        draw_state_restore(&draw_state);

    return 0;
}

/* begin rounded window definition functions */

void hilite_rounded_window(WindowPeek w, INTEGER left, INTEGER top, INTEGER right, INTEGER bottom)
{
    RgnHandle rh1, rh2;

    rh1 = NewRgn();
    rh2 = NewRgn();
    CopyRgn(WINDOW_STRUCT_REGION(w), rh1);
    InsetRgn(rh1, 1, 1);
    SetRectRgn(rh2, left, top - 18, right, top - 1);
    SectRgn(rh1, rh2, rh1);

    RGBForeColor(title_bar_bk);
    PenPat(black);
    PaintRgn(rh1);

    DisposeRgn(rh2);
    DisposeRgn(rh1);
}

void draw_rounded_doc(GrafPtr w)
{
    int left, top, right, bottom;
    int draw_wind_close_p;
    Rect r;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    SetRect(&r, left - 1, top - 19, right + 1, top);
    FillRect(&r, white);
    RGBForeColor(frame);
    FrameRgn(WINDOW_STRUCT_REGION(w));
    PenSize(1, 1);
    PenMode(patCopy);
    MoveTo(left, top - 1);
    LineTo(right, top - 1);

    hilite_rounded_window((WindowPeek)w, left, top, right, bottom);
    if (WINDOW_HILITED_X(w)) {
        draw_wind_close_p = WINDOW_GO_AWAY_FLAG(w);
        if (draw_wind_close_p)
            draw_wind_close(left, top, FALSE);
        draw_title(w, draw_wind_close_p, FALSE, FALSE);
    }
    else
        draw_title(w, FALSE, FALSE, FALSE);
}

LONGINT
hit_rounded_doc(GrafPtr w, LONGINT param)
{
    Point p;
    int left, top, right, bottom;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    p.v = HiWord(param);
    p.h = LoWord(param);
    if (WINDOW_HILITED_X(w)) {
        if (PtInRgn(p, WINDOW_CONT_REGION(w)))
            return wInContent;
        if (p.h >= left - 1 && p.h < right + 1 && p.v >= top - 19 && p.v < top) {
            if (WINDOW_GO_AWAY_FLAG(w) && p.h > left + 8 && p.h < left + 20 && p.v > top - 16 && p.v < top - 4)
                return wInGoAway;
            else
                return wInDrag;
        }
        else
            return wNoHit;
    }
    else {
        if (PtInRgn(p, WINDOW_CONT_REGION(w)))
            return wInContent;
        else if (p.h >= left - 1 && p.h < right + 1 && p.v >= top - 19 && p.v < top)
            return wInDrag;
        else
            return wNoHit;
    }
}

void calc_rounded_doc(GrafPtr w, INTEGER curve_code)
{
    int left, top, right, bottom;
    int curve;
    static const int curve_code_to_curve[] = {
        4, 6, 8, 10, 12, 20, 24,
    };
    RgnHandle rh;
    Rect r;

    left = CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left);
    top = CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top);
    right = CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left);
    bottom = CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top);

    SetRectRgn(WINDOW_CONT_REGION(w), left, top, right, bottom);

    if (curve_code >= 1 && curve_code <= 7)
        curve = curve_code_to_curve[curve_code - 1];
    else
        curve = 16;

    r.left = CW(left - 1);
    r.top = CW(top - 19);
    r.right = CW(right + 1);
    r.bottom = CW(bottom + 1);
    OpenRgn();
    FrameRoundRect(&r, curve, curve);
    CloseRgn(WINDOW_STRUCT_REGION(w));
    CopyRgn(WINDOW_STRUCT_REGION(w), WINDOW_CONT_REGION(w));
    InsetRgn(WINDOW_CONT_REGION(w), 1, 1);
    rh = NewRgn();
    SetRectRgn(rh, left, top, right, bottom);
    SectRgn(rh, WINDOW_CONT_REGION(w), WINDOW_CONT_REGION(w));
    DisposeRgn(rh);
}

P4(PUBLIC pascal, LONGINT, wdef16, INTEGER, varcode, WindowPtr, wp, INTEGER, message, LONGINT, param)
{
    WindowPeek w = (WindowPeek)wp;
    draw_state_t draw_state;
    boolean_t draw_p;

    rounded_window_p = TRUE;

    /* mask out zoom bit; it will be ignored */
    varcode &= ~ZOOMBIT;

    draw_p = (message == wDraw);

    if (draw_p) {
        draw_state_save(&draw_state);

        window_colors = validate_colors_for_window((GrafPtr)w);
    }

    switch (message) {
    case wDraw:
        if (!WINDOW_VISIBLE_X(w))
            break;
        TRAPBEGIN();
        switch (param) {
        case 0:
            draw_rounded_doc((GrafPtr)w);
            break;

        case wInGoAway:
            toggle_wind_close_box((GrafPtr)w, FALSE);
            break;
        }
        TRAPEND();
        break;

    case wHit:
        return hit_rounded_doc((GrafPtr)w, param);
        break;

    case wCalcRgns:
        calc_rounded_doc((GrafPtr)w, varcode);
        break;

    case wNew:
        WINDOW_SPARE_FLAG_X(w) = FALSE;
        break;

    case wDispose:
        break;

    case wGrow:
    case wDrawGIcon:
        break;
    }

    if (draw_p)
        draw_state_restore(&draw_state);

    return 0;
}
