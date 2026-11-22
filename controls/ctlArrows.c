/*+===================================================================
  File:      ctlArrows.c

  Summary:   Implements ctl arrows support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: draw_arrow, draw_page, draw_thumb, sb_ctl_init, thumb_rect, where

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986 - 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "classic-ui/classic_ui.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "MemoryMgr.h"
#include "ToolboxUtil.h"

#include "rsys/cquick.h"
#include "rsys/ctl.h"

#include "rsys/image.h"
#include "rsys/host.h"
#include "rsys/wind.h"

#include "generated/wind_scroll_arrow_active.inc"
#include "generated/wind_scroll_arrow_clicked.inc"
#include "generated/wind_scroll_arrow_inactive.inc"
#include "generated/wind_scroll_thumb.inc"

#define SB_VERT_P(h, w) ((h) > (w))

/* one if the control (scrollbar) currently being drawn is color.
   this is set by `validate_colors_for_ctl ()' called from `cdef16 ()',
   the single entry point to this function if drawing may ensue */
static int current_ctl_color_p;

/* control colors */
enum {
    arrow_bk,
    first_arrow = arrow_bk,
    arrow_tl_shadow,
    arrow_br_shadow,
    /* also thumb_br_shadow */
    arrow_outline,
    first_thumb = arrow_outline,
    arrow_inactive,
    /* arrow and thumb use the same frame color */
    sb_frame,

    thumb_tl_edge,
    /* also light strip color */
    thumb_tl_shadow,
    thumb_dk_strip,
    thumb_bk,

    page_fg,
    page_bk,

    n_ctl_colors,
};

/* The native painter uses these same host RGB values. Only the stock
   black/white control palette is normalized; application cctb colors survive. */
static RGBColor scroll_track_color(unsigned rgb)
{
    RGBColor result = {CW(((rgb >> 16) & 255) * 257), CW(((rgb >> 8) & 255) * 257), CW((rgb & 255) * 257)};
    return result;
}

static boolean_t stock_color_pair(const RGBColor* light, const RGBColor* dark)
{
    return !memcmp(light, &ROMlib_white_rgb_color, sizeof *light) &&
           !memcmp(dark, &ROMlib_black_rgb_color, sizeof *dark);
}

static RGBColor current_ctl_colors[n_ctl_colors];

/* indexed by [up_or_left_p][vert_p][ROMAWClassicArrowState] */
pixel_image_t* sb_arrow_images[2][2][3];

/* indexed by [vert_p] */
pixel_image_t* sb_thumb_images[2];

#define ARROW_PART_P(part) ((part) == inUpButton || (part) == inDownButton)
#define UP_OR_LEFT_P(part) ((part) == inUpButton)
#define SB_IMAGE(part, vert_p, active_p)                                        \
    (ARROW_PART_P(part) ? sb_arrow_images[UP_OR_LEFT_P(part)][vert_p][active_p] \
                        : sb_thumb_images[vert_p] /* active_p unused */)

void sb_ctl_init()
{
#define VERT 1
#define HORIZ 0

#define UP_OR_LEFT 1
#define DOWN_OR_RIGHT 0

    /* wow, is it just me, or is this really... ugly.
     but hey, it works */
#define UP    UP_OR_LEFT][VERT
#define DOWN  DOWN_OR_RIGHT][VERT
#define LEFT  UP_OR_LEFT][HORIZ
#define RIGHT DOWN_OR_RIGHT][HORIZ

#define CTL_ACTIVE 1
#define CTL_INACTIVE 0

    sb_arrow_images[UP][CTL_ACTIVE] = wind_scroll_arrow_active_up;
    sb_arrow_images[LEFT][CTL_ACTIVE] = wind_scroll_arrow_active_left;
    sb_arrow_images[DOWN][CTL_ACTIVE] = wind_scroll_arrow_active_down;
    sb_arrow_images[RIGHT][CTL_ACTIVE] = wind_scroll_arrow_active;

    sb_arrow_images[UP][CTL_INACTIVE] = wind_scroll_arrow_inactive_up;
    sb_arrow_images[LEFT][CTL_INACTIVE] = wind_scroll_arrow_inactive_left;
    sb_arrow_images[DOWN][CTL_INACTIVE] = wind_scroll_arrow_inactive_down;
    sb_arrow_images[RIGHT][CTL_INACTIVE] = wind_scroll_arrow_inactive;
    sb_arrow_images[UP][ROMAW_CLASSIC_ARROW_CLICKED] = wind_scroll_arrow_clicked_up;
    sb_arrow_images[LEFT][ROMAW_CLASSIC_ARROW_CLICKED] = wind_scroll_arrow_clicked_left;
    sb_arrow_images[DOWN][ROMAW_CLASSIC_ARROW_CLICKED] = wind_scroll_arrow_clicked_down;
    sb_arrow_images[RIGHT][ROMAW_CLASSIC_ARROW_CLICKED] = wind_scroll_arrow_clicked;

#undef CTL_INACTIVE
#undef CTL_ACTIVE

#undef RIGHT
#undef LEFT
#undef DOWN
#undef UP

#undef DOWN_OR_RIGHT
#undef UP_OR_LEFT

    sb_thumb_images[VERT] = wind_scroll_thumb_vertical;
    sb_thumb_images[HORIZ] = wind_scroll_thumb;

#undef HORIZ
#undef VERT
}

static void validate_colors_for_control(ControlHandle ctl)
{
    int hilited_p;
    RGBColor ctl_ctab_colors[15];
    AuxCtlHandle t_aux_c;
    int i;

    /* FIXME: tmp hack */
    current_ctl_color_p = (CGrafPort_p(thePort) != 0);

    hilited_p = (CTL_HILITE_X(ctl) != 255 && ROMlib_control_has_range(ctl));

    /*
   * NOTE: the use of default_ctl_colors to fill in gaps is incorrect.
   * To see this, try using a 'cctb' on a Mac that only fills in entries
   * 1, 2 and 3 (like LogDig does).
   */

    for (i = 0; i <= 14; i++)
        ctl_ctab_colors[i] = default_ctl_colors[i].rgb;

    t_aux_c = HIDDEN_NATIVE(*lookup_aux_ctl(ctl));
    if (t_aux_c && HxZ(t_aux_c, acCTable)) {
        CTabHandle c_ctab;
        ColorSpec* c_ctab_table;
        int c_ctab_size;

        c_ctab = (CTabHandle)HxP(t_aux_c, acCTable);
        c_ctab_table = CTAB_TABLE(c_ctab);
        c_ctab_size = CTAB_SIZE(c_ctab);

        for (i = c_ctab_size; i >= 0; i--) {
            ColorSpec* c_ctab_entry;
            int index;

            c_ctab_entry = &c_ctab_table[i];
            index = CW(c_ctab_entry->value);
            if (index >= 0 && index < NELEM(ctl_ctab_colors))
                ctl_ctab_colors[index] = c_ctab_entry->rgb;
        }
    }

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
            RGBColor temp1;
            RGBColor temp2;
            RGBColor temp3;
            RGBColor temp4;
            RGBColor temp5;
            RGBColor temp6;
            int vert_p;
            int up_or_left_p;
            int active_p;

            if (!current_ctl_color_p)
                FAIL;

            if (!AVERAGE_COLOR(&ctl_ctab_colors[cArrowsColorLight], &ctl_ctab_colors[cArrowsColorDark], 0xDDDD,
                               &current_ctl_colors[arrow_bk]))
                FAIL;

            current_ctl_colors[arrow_outline] = ctl_ctab_colors[cTingeDark];
            current_ctl_colors[sb_frame] = ctl_ctab_colors[cFrameColor];
            /* page colors are the only two colors which are actually used
	 outside of this function */
            if (!AVERAGE_COLOR(&ctl_ctab_colors[cHiliteLight], &ctl_ctab_colors[cHiliteDark], 0x7777,
                               &current_ctl_colors[page_bk]))
                FAIL;

            if (!AVERAGE_COLOR(&ctl_ctab_colors[cArrowsColorLight], &ctl_ctab_colors[cArrowsColorDark], 0x7777, &temp1))
                FAIL;
            if (!AVERAGE_COLOR(&ctl_ctab_colors[cTingeLight], &ctl_ctab_colors[cTingeDark], 0xAAAA, &temp2))
                FAIL;
            if (!AVERAGE_COLOR(&ctl_ctab_colors[cThumbLight], &ctl_ctab_colors[cThumbDark], 0x5555, &temp3))
                FAIL;
            if (!AVERAGE_COLOR(&ctl_ctab_colors[cTingeLight], &ctl_ctab_colors[cThumbDark], 0x8888, &temp4))
                FAIL;
            if (!AVERAGE_COLOR(&ctl_ctab_colors[cThumbLight], &ctl_ctab_colors[cThumbDark], 0xAAAA, &temp5))
                FAIL;
            if (!AVERAGE_COLOR(&ctl_ctab_colors[cHiliteLight], &ctl_ctab_colors[cHiliteDark], 0xDDDD, &temp6))
                FAIL;
            boolean_t stock_track = stock_color_pair(&ctl_ctab_colors[cHiliteLight], &ctl_ctab_colors[cHiliteDark]);
            if (stock_track) {
                current_ctl_colors[page_bk] = scroll_track_color(ROMAW_CLASSIC_SCROLL_TRACK_DARK_RGB);
                temp6 = scroll_track_color(ROMAW_CLASSIC_SCROLL_TRACK_LIGHT_RGB);
            }
            if (hilited_p) {
                current_ctl_colors[arrow_tl_shadow] = ctl_ctab_colors[cArrowsColorLight];
                current_ctl_colors[arrow_br_shadow] = temp1;
                current_ctl_colors[arrow_inactive] = temp2;

                /* thumb colors are only used when hilited */
                current_ctl_colors[thumb_tl_edge] = temp3;
                current_ctl_colors[thumb_tl_shadow] = ctl_ctab_colors[cTingeLight];
                current_ctl_colors[thumb_dk_strip] = temp4;
                current_ctl_colors[thumb_bk] = temp5;
                current_ctl_colors[page_fg] = temp6;
            }
            else {
                current_ctl_colors[arrow_tl_shadow] = current_ctl_colors[arrow_bk];
                current_ctl_colors[arrow_br_shadow] = current_ctl_colors[arrow_bk];
                current_ctl_colors[arrow_inactive] = current_ctl_colors[arrow_bk];
                current_ctl_colors[page_fg] = current_ctl_colors[page_bk] = current_ctl_colors[arrow_bk];
                if (stock_track &&
                    stock_color_pair(&ctl_ctab_colors[cArrowsColorLight], &ctl_ctab_colors[cArrowsColorDark]))
                    current_ctl_colors[page_fg] = current_ctl_colors[page_bk] =
                        scroll_track_color(ROMAW_CLASSIC_SCROLL_TRACK_LIGHT_RGB);
            }

            for (vert_p = 0; vert_p < 2; vert_p++) {
                for (up_or_left_p = 0; up_or_left_p < 2; up_or_left_p++)
                    for (active_p = 0; active_p < 3; active_p++)
                        image_update_ctab(sb_arrow_images[up_or_left_p][vert_p][active_p],
                                          &current_ctl_colors[first_arrow], 5);
                if (hilited_p)
                    image_update_ctab(sb_thumb_images[vert_p], &current_ctl_colors[first_thumb], 6);
            }
        },
        {
            current_ctl_color_p = FALSE;

            /* only the ones that matter */
            current_ctl_colors[page_fg] = ROMlib_white_rgb_color;
            if (hilited_p)
                current_ctl_colors[page_bk] = ROMlib_black_rgb_color;
            else
                current_ctl_colors[page_bk] = ROMlib_white_rgb_color;
        });
}

void draw_arrow(ControlHandle ctl, int part)
{
    int height, width;
    int vert_p;
    ROMAWClassicArrowState arrow_state;
    Rect r;

    r = CTL_RECT(ctl);

    height = RECT_HEIGHT(&r);
    width = RECT_WIDTH(&r);
    vert_p = SB_VERT_P(height, width);
    if (vert_p) {
        if (part == inUpButton)
            r.bottom = CW(CW(r.top) + width);
        else
            r.top = CW(CW(r.bottom) - width);
    }
    else {
        if (part == inUpButton)
            r.right = CW(CW(r.left) + height);
        else
            r.left = CW(CW(r.right) - height);
    }
    arrow_state =
        romaw_classic_arrow_state(CTL_HILITE(ctl) != 255 && ROMlib_control_has_range(ctl), CTL_HILITE(ctl) == part);

    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);

    image_copy(SB_IMAGE(part, vert_p, arrow_state), current_ctl_color_p, &r, srcCopy);
}

void draw_page(ControlHandle ctl)
{
    Rect r, *rp;
    int height, width;

    r = CTL_RECT(ctl);

    height = RECT_HEIGHT(&r);
    width = RECT_WIDTH(&r);
    if (SB_VERT_P(height, width)) {
        r.top = CW(CW(r.top) + width);
        r.bottom = CW(CW(r.bottom) - width);
        r.left = CW(CW(r.left) + 1);
        r.right = CW(CW(r.right) - 1);
    }
    else {
        r.left = CW(CW(r.left) + height);
        r.right = CW(CW(r.right) - height);
        r.top = CW(CW(r.top) + 1);
        r.bottom = CW(CW(r.bottom) - 1);
    }

    /* page_fg supplies the light field, page_bk the sparse dark dots.
       Both colors depend on whether the scrollbar is currently hilited. */
    RGBForeColor(&current_ctl_colors[page_fg]);
    RGBBackColor(&current_ctl_colors[page_bk]);
    FillRect(&r, ltGray);

    rp = &HxX(CTL_DATA(ctl), rgnBBox);
    rp->top = rp->bottom = 0;
}

void thumb_rect(ControlHandle ctl, Rect* thumb_rect_out)
{
    int32 range[4];
    ROMlib_control_read_range(ctl, HIDDEN_NATIVE(*lookup_aux_ctl(ctl)), range);
    const int64 diff = (int64)range[control_maximum] - range[control_minimum];
    if (diff > 0 && CTL_HILITE(ctl) != 255) {
        const Rect r = CTL_RECT(ctl);
        const int height = RECT_HEIGHT(&r), width = RECT_WIDTH(&r);
        const boolean_t vertical = SB_VERT_P(height, width);
        const int arrow = vertical ? width : height;
        const int groove = (vertical ? height : width) - 2 * arrow;
        int thumb = arrow;
        const int32 view = range[control_view_size];
        if (view > 0 && groove > arrow)
            thumb = MAX(arrow, (int)((int64)groove * view / (diff + view)));
        const int offset = ((int64)range[control_value] - range[control_minimum]) * (groove - thumb) / diff;
        if (vertical) {
            thumb_rect_out->top = CW((short)(CW(r.top) + arrow + offset));
            thumb_rect_out->bottom = CW(CW(thumb_rect_out->top) + thumb);
            thumb_rect_out->left = CW(CW(r.left) + 1);
            thumb_rect_out->right = CW(CW(r.right) - 1);
        }
        else {
            thumb_rect_out->left = CW((short)(CW(r.left) + arrow + offset));
            thumb_rect_out->right = CW(CW(thumb_rect_out->left) + thumb);
            thumb_rect_out->top = CW(CW(r.top) + 1);
            thumb_rect_out->bottom = CW(CW(r.bottom) - 1);
        }
    }
    else {
        SetRect(thumb_rect_out, 0, 0, 0, 0);
    }
}

PRIVATE void GlobalToLocalRect(Rect* rp)
{
    GlobalToLocal((Point*)&rp->top);
    GlobalToLocal((Point*)&rp->bottom);
}

PRIVATE void LocalToGlobalRect(Rect* rp)
{
    LocalToGlobal((Point*)&rp->top);
    LocalToGlobal((Point*)&rp->bottom);
}

PRIVATE void GlobalToLocalRgn(RgnHandle rgn)
{
    OffsetRgn(rgn, CW(PORT_BOUNDS(thePort).left), CW(PORT_BOUNDS(thePort).top));
}

typedef struct {
    ControlHandle ctl;
    LONGINT param;
} device_loop_param;

void draw_thumb(ControlHandle ctl)
{
    Rect old_thumb, new_thumb;
    Rect dst_rect, *ctl_rect;

    old_thumb = HxX(CTL_DATA(ctl), rgnBBox);
    GlobalToLocalRect(&old_thumb);

    thumb_rect(ctl, &new_thumb);
    if (memcmp(&old_thumb, &new_thumb, sizeof old_thumb)) {
        if (!EmptyRect(&new_thumb)) {
            int vert_p;

            ctl_rect = &CTL_RECT(ctl);
            vert_p = SB_VERT_P(RECT_HEIGHT(ctl_rect), RECT_WIDTH(ctl_rect));
            if (vert_p) {
                dst_rect.top = new_thumb.top;
                dst_rect.bottom = new_thumb.bottom;
                dst_rect.left = CW(CW(new_thumb.left) - 1);
                dst_rect.right = CW(CW(new_thumb.right) + 1);
            }
            else {
                dst_rect.left = new_thumb.left;
                dst_rect.right = new_thumb.right;
                dst_rect.top = CW(CW(new_thumb.top) - 1);
                dst_rect.bottom = CW(CW(new_thumb.bottom) + 1);
            }

            /* if the old_thumb rect is empty, ie., the thumb was not
	     previously drawn (the ctl was not active), then redraw
	     the arrows as well */
            if (current_ctl_color_p && EmptyRect(&old_thumb)) {
                draw_arrow(ctl, inUpButton);
                draw_arrow(ctl, inDownButton);
            }

            /* previously only the old_thumb rect was redrawn
	     if it was not empty */
            draw_page(ctl);

            RGBForeColor(&ROMlib_black_rgb_color);
            RGBBackColor(&ROMlib_white_rgb_color);

            image_copy(SB_IMAGE(inThumb, vert_p,
                                /* dummy */ -1),
                       current_ctl_color_p, &dst_rect, srcCopy);
        }
        else
            draw_page(ctl);
        LocalToGlobalRect(&new_thumb);
        RectRgn(CTL_DATA(ctl), &new_thumb);
    }
}

P4(PUBLIC pascal, void, new_draw_scroll, INTEGER, depth, INTEGER, flags, GDHandle, target, LONGINT, l)
{
    device_loop_param* dlp;
    ControlHandle ctl;
    INTEGER part;
    Rect r;

    dlp = (device_loop_param*)SYN68K_TO_US(l);
    ctl = dlp->ctl;
    part = dlp->param;

    if (CTL_VIS_X(ctl) == CWC(0))
        return;
    switch (part) {
    case ENTIRECONTROL:
        r = CTL_RECT(ctl);
        RGBForeColor(&current_ctl_colors[sb_frame]);
        FrameRect(&r);
        draw_arrow(ctl, inUpButton);
        draw_page(ctl);
        draw_arrow(ctl, inDownButton);
        if (CTL_HILITE_X(ctl) != 255)
            draw_thumb(ctl);
        break;
    case inUpButton:
    case inDownButton:
        draw_arrow(ctl, part);
        break;
    case inThumb: /* that is all indicators ... values may have changed */
        draw_thumb(ctl);
        break;
    case inPageUp:
    case inPageDown:
        break;
    default: /* someone is asking us to do weird things */
        break;
    }
}

LONGINT
where(ControlHandle ctl, Point p)
{
    int height, width;
    Rect r;
    Rect thumbr;

    thumbr = HxX(CTL_DATA(ctl), rgnBBox);
    GlobalToLocalRect(&thumbr);
    if (PtInRect(p, &thumbr))
        return inThumb;
    else {
        r = CTL_RECT(ctl);
        height = RECT_HEIGHT(&r);
        width = RECT_WIDTH(&r);
        if (SB_VERT_P(height, width)) {
            if (p.v <= CW(r.top) + width)
                return inUpButton;
            else if (p.v >= CW(r.bottom) - width)
                return inDownButton;
            else if (p.v < CW(thumbr.top))
                return inPageUp;
            else
                return inPageDown;
        }
        else {
            if (p.h <= CW(r.left) + height)
                return inUpButton;
            else if (p.h >= CW(r.right) - height)
                return inDownButton;
            else if (p.h < CW(thumbr.left))
                return inPageUp;
            else
                return inPageDown;
        }
    }
}

P4(PUBLIC pascal, void, new_pos_ctl, INTEGER, depth, INTEGER, flags, GDHandle, target, LONGINT, l)
{
    device_loop_param* dlp;
    ControlHandle ctl;
    LONGINT p;
    int height, width;
    INTEGER top, left, bottom, right;
    Rect thumbr;
    Rect r;

    dlp = (device_loop_param*)SYN68K_TO_US(l);
    ctl = dlp->ctl;
    p = dlp->param;

    r = CTL_RECT(ctl);

    top = CW(r.top);
    left = CW(r.left);
    bottom = CW(r.bottom);
    right = CW(r.right);

    height = bottom - top;
    width = right - left;
    thumbr = HxX(CTL_DATA(ctl), rgnBBox);
    GlobalToLocalRect(&thumbr);

    const boolean_t vertical = SB_VERT_P(height, width);
    const int arrow = vertical ? width : height;
    const int thumb = vertical ? RECT_HEIGHT(&thumbr) : RECT_WIDTH(&thumbr);
    const int travel = (vertical ? height : width) - 2 * arrow - thumb;
    if (travel < 0)
        return;
    const int offset = vertical ? CW(thumbr.top) + HiWord(p) - top - arrow : CW(thumbr.left) + LoWord(p) - left - arrow;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(ctl));
    int32 range[4];
    ROMlib_control_read_range(ctl, aux, range);
    const int32 min = range[control_minimum], max = MAX(min, range[control_maximum]);
    const int32 value = min + (int64)MAX(0, MIN(travel, offset)) * ((int64)max - min + 1) / (travel + 1);
    ROMlib_control_store_value(ctl, aux, value);
    if (CTL_VIS_X(ctl) == 255)
        draw_thumb(ctl);
}

typedef struct {
    GrafPtr port;
    CGrafPort cp;
} save_t;

PRIVATE Handle CopyMacHandle(Handle h)
{
    Handle retval;
    Size s;

    h = MR(h);
    s = GetHandleSize(h);
    retval = NewHandle(s);
    if (retval)
        memcpy(STARH(retval), STARH(h), s);
    retval = RM(retval);
    return retval;
}

PRIVATE boolean_t save_and_switch_to_color_port_if_needed(save_t* sp)
{
    boolean_t retval;

    if (CGrafPort_p(thePort))
        retval = FALSE;
    else {
        CGrafPtr wp;

        sp->port = thePort;
        sp->cp = *(CGrafPtr)thePort;
        wp = (CGrafPtr)MR(wmgr_port);
        FIELD_ASSIGN_RAW(sp->cp.portPixMap, (PixMapHandle)CopyMacHandle((Handle)PP_RAW(wp->portPixMap)));
        PIXMAP_BOUNDS(PPR(sp->cp.portPixMap)) = thePort->portBits.bounds;
        sp->cp.portVersion = wp->portVersion;
        PP_RAW(sp->cp.grafVars) = PP_RAW(wp->grafVars);
        sp->cp.chExtra = wp->chExtra;
        sp->cp.pnLocHFrac = wp->pnLocHFrac;
        PP_RAW(sp->cp.bkPixPat) = PP_RAW(wp->bkPixPat);
        sp->cp.rgbFgColor = wp->rgbFgColor;
        sp->cp.rgbBkColor = wp->rgbBkColor;
        PP_RAW(sp->cp.pnPixPat) = PP_RAW(wp->pnPixPat);
        PP_RAW(sp->cp.fillPixPat) = PP_RAW(wp->fillPixPat);
        PP_RAW(sp->cp.grafProcs) = PP_RAW(wp->grafProcs);
        SetPort((GrafPtr)&sp->cp);
        retval = TRUE;
    }

    return retval;
}

PRIVATE void restore(const save_t* sp)
{
    SetPort(sp->port);
    DisposHandle((Handle)PPR(sp->cp.portPixMap));
}

P4(PUBLIC pascal, LONGINT, cdef16, /* IMI-328 */
   INTEGER, var, ControlHandle, c, INTEGER, mess, LONGINT, guest_param)
{
    intptr_t param = guest_param;
    int thumb_region = mess == calcCRgns && ((uint32)guest_param & 0x80000000U);

    Point p;
    PenState ps;
    Rect r, *rp, tempr;
    struct lsastr* pl;
    int height, width;
    Handle temph;
    draw_state_t draw_state;
    int draw_p;
    Rect thumbr;
    save_t save;
    boolean_t need_to_restore_p;

    switch (mess) {
    case calcCRgns:
    case calcCntlRgn:
    case calcThumbRgn:
    case thumbCntl:
        param = (intptr_t)SYN68K_TO_US(thumb_region ? guest_param & 0x7FFFFFFF : guest_param);
        break;
    default:
        break;
    }

    /* if drawing can occur, validate the color state */
    draw_p = (mess == drawCntl
              /* || mess == thumbCntl REALLY? */
              || mess == posCntl);

    if (draw_p) {
        need_to_restore_p = save_and_switch_to_color_port_if_needed(&save);
        draw_state_save(&draw_state);
    }
#if !defined(LETGCCWAIL)
    else
        need_to_restore_p = FALSE;
#endif

    switch (mess) {
    case drawCntl:
        if (Hx(c, contrlVis) && SectRect(&HxX(PORT_VIS_REGION(thePort), rgnBBox), &HxX(c, contrlRect), &r)) {
            validate_colors_for_control(c);
            TRAPBEGIN();
            GetPenState(&ps);
            PenNormal();
            {
                RgnHandle rh;
                device_loop_param dlp;

                rh = NewRgn();
                RectRgn(rh, &HxX(c, contrlRect));
                dlp.ctl = c;
                dlp.param = param;
                DeviceLoop(rh, P_new_draw_scroll, (LONGINT)US_TO_SYN68K(&dlp), 0);
                DisposeRgn(rh);
            }
            SetPenState(&ps);
            TRAPEND();
            break;
        }
    case testCntl:
        p.v = HiWord(param);
        p.h = LoWord(param);
        if (U(Hx(c, contrlHilite)) != 255 && ROMlib_control_has_range(c) && PtInRect(p, &(HxX(c, contrlRect))))
            return where(c, p);
        else
            return 0;
    case calcCRgns:
        if (thumb_region) /* ICK! */
        {
            /* IMI-331: strip the flag before translating the guest address. */
        case calcThumbRgn:
            CopyRgn((RgnHandle)HxP(c, contrlData), (RgnHandle)param);
            GlobalToLocalRgn((RgnHandle)param);
            break;
        }
        else {
        case calcCntlRgn:
            r = HxX(c, contrlRect);
            RectRgn((RgnHandle)param, &r);
            break;
        }
        break;
    case initCntl:
        temph = RM((Handle)NewRgn());
        FIELD_ASSIGN_RAW(PP_RAW(HxX(c, contrlData)), temph);
        thumb_rect(c, &tempr);
        LocalToGlobalRect(&tempr);
#if 1
        /* MacBreadboard's behaviour suggests the following line is needed */
        tempr.top = tempr.bottom = 0;
#endif
        RectRgn((RgnHandle)HxP(c, contrlData), &tempr);
        break;
    case dispCntl:
        DisposHandle((Handle)HxP(c, contrlData));
        break;
    case posCntl:
        validate_colors_for_control(c);
        {
            RgnHandle rh;
            device_loop_param dlp;

            rh = NewRgn();
            RectRgn(rh, &HxX(c, contrlRect));
            dlp.ctl = c;
            dlp.param = param;
            DeviceLoop(rh, P_new_pos_ctl, (LONGINT)US_TO_SYN68K(&dlp), 0);
            DisposeRgn(rh);
        }
        break;
    case thumbCntl:
        pl = (struct lsastr*)param;
        p.v = CW(pl->limitRect.top);
        p.h = CW(pl->limitRect.left);
        pl->slopRect = pl->limitRect = CTL_RECT(c);
        thumbr = HxX(CTL_DATA(c), rgnBBox);
        GlobalToLocalRect(&thumbr);
        rp = &thumbr;
        height = CW(pl->slopRect.bottom) - CW(pl->slopRect.top);
        width = CW(pl->slopRect.right) - CW(pl->slopRect.left);
        if (SB_VERT_P(height, width)) {
            pl->axis = CWC(vAxisOnly);
            pl->limitRect.top = CW(CW(pl->limitRect.top) + (width - (CW(rp->top) - p.v)));
            pl->limitRect.bottom = CW(CW(pl->limitRect.bottom) - (width - (p.v - CW(rp->bottom)) - 1));
        }
        else {
            pl->axis = CWC(hAxisOnly);
            pl->limitRect.left = CW(CW(pl->limitRect.left) + height - (CW(rp->left) - p.h));
            pl->limitRect.right = CW(CW(pl->limitRect.right) - height - (p.h - CW(rp->right)) - 1);
        }
        InsetRect(&pl->slopRect, -20, -20);
        break;
    case dragCntl:  /* NOT NEEDED */
    case autoTrack: /* NOT NEEDED */
        break;
    default:
        warning_unexpected("unknown message `%d'", mess);
        break;
    }

    if (draw_p) {
        draw_state_restore(&draw_state);
        if (need_to_restore_p)
            restore(&save);
    }

    return 0;
}
