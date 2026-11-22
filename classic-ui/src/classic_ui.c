/*+===================================================================
  File:      classic_ui.c

  Summary:   Classic WDEF/MBDF geometry shared by the QuickDraw and native adapters.

  Classes:   None.

  Functions: romaw_classic_arrow_state, romaw_classic_document_frame, romaw_classic_menu_advance,
             romaw_classic_menu_bar, romaw_classic_menu_title, romaw_classic_title_bevel,
             romaw_classic_title_rules

  Origin:    Existing repository source in executor/src/classic-ui; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic WDEF/MBDF geometry shared by the QuickDraw and native adapters. */
#include "classic-ui/classic_ui.h"
#include <limits.h>

/* Disabled controls keep their inactive artwork even if a press was pending. */
ROMAWClassicArrowState romaw_classic_arrow_state(int enabled, int pressed)
{
    if (!enabled)
        return ROMAW_CLASSIC_ARROW_INACTIVE;
    return pressed ? ROMAW_CLASSIC_ARROW_CLICKED : ROMAW_CLASSIC_ARROW_ACTIVE;
}

/* FrameRect's half-open rectangle expressed through inclusive line callbacks. */
static void outline(const ROMAWClassicCanvas* c, ROMAWClassicRect r, ROMAWClassicColor role)
{
    c->stroke_line(c->context, r.left, r.top, r.right - 1, r.top, role);
    c->stroke_line(c->context, r.right - 1, r.top, r.right - 1, r.bottom - 1, role);
    c->stroke_line(c->context, r.right - 1, r.bottom - 1, r.left, r.bottom - 1, role);
    c->stroke_line(c->context, r.left, r.bottom - 1, r.left, r.top, role);
}

/* Standard document WDEF frame, separator and outside lower/right shadow.
   Compact/modal frame variants remain with their Toolbox definitions. */
void romaw_classic_document_frame(const ROMAWClassicCanvas* c, const ROMAWClassicRect* content, int title_height)
{
    if (!c || !c->fill_rect || !c->stroke_line || !content || title_height < 1 || content->right <= content->left ||
        content->bottom <= content->top)
        return;
    ROMAWClassicRect r = {content->left - 1, content->top - title_height, content->right + 1, content->top};
    c->fill_rect(c->context, &r, ROMAW_CLASSIC_TITLE_BACKGROUND);
    r.bottom = content->bottom + 1;
    if (c->frame_rect)
        c->frame_rect(c->context, &r, ROMAW_CLASSIC_FRAME_OUTLINE);
    else
        outline(c, r, ROMAW_CLASSIC_FRAME_OUTLINE);
    c->stroke_line(c->context, content->left, content->top - 1, content->right, content->top - 1,
                   ROMAW_CLASSIC_FRAME_OUTLINE);
    c->stroke_line(c->context, content->left, content->bottom + 1, content->right + 1, content->bottom + 1,
                   ROMAW_CLASSIC_FRAME_SHADOW);
    c->stroke_line(c->context, content->right + 1, content->top - title_height + 1, content->right + 1, content->bottom,
                   ROMAW_CLASSIC_FRAME_SHADOW);
}

/* WDEF title tinge: the bottom/right edges own the disputed corner pixels. */
void romaw_classic_title_bevel(const ROMAWClassicCanvas* c, int left, int top, int right, int height)
{
    if (!c || !c->stroke_line || right - left < 3 || height < 4)
        return;
    c->stroke_line(c->context, left, top - 2, right - 1, top - 2, ROMAW_CLASSIC_TITLE_SHADOW);
    c->stroke_line(c->context, right - 1, top - 2, right - 1, top - height + 1, ROMAW_CLASSIC_TITLE_SHADOW);
    c->stroke_line(c->context, right - 2, top - height + 1, left, top - height + 1, ROMAW_CLASSIC_TITLE_LIGHT);
    c->stroke_line(c->context, left, top - height + 1, left, top - 3, ROMAW_CLASSIC_TITLE_LIGHT);
}

/* Keep the WDEF rule count and two-pixel pitch, including compact heights. */
void romaw_classic_title_rules(const ROMAWClassicCanvas* c, int left, int top, int right, int height)
{
    if (!c || !c->stroke_line || right - left < 3 || height < 5)
        return;
    for (int y = top - height + 4; y <= top - 5; y += 2)
        c->stroke_line(c->context, left + 1, y, right - 2, y, ROMAW_CLASSIC_TITLE_RULE);
}

/* MBDF background and the one-pixel separator beneath the menu bar. */
void romaw_classic_menu_bar(const ROMAWClassicCanvas* c, const ROMAWClassicRect* bounds)
{
    if (!c || !c->fill_rect || !c->stroke_line || !bounds || bounds->right <= bounds->left ||
        bounds->bottom <= bounds->top)
        return;
    ROMAWClassicRect r = *bounds;
    --r.bottom;
    c->fill_rect(c->context, &r, ROMAW_CLASSIC_MENU_BACKGROUND);
    c->stroke_line(c->context, r.left, r.bottom, r.right - 1, r.bottom, ROMAW_CLASSIC_MENU_OUTLINE);
}

/* Selection preserves the top background row and the bar's bottom separator. */
void romaw_classic_menu_title(const ROMAWClassicCanvas* c, const ROMAWClassicRect* bounds, int highlighted)
{
    romaw_classic_menu_bar(c, bounds);
    if (c && c->fill_rect && bounds && highlighted && bounds->right > bounds->left &&
        bounds->bottom - bounds->top > 2) {
        ROMAWClassicRect selected = *bounds;
        ++selected.top;
        --selected.bottom;
        c->fill_rect(c->context, &selected, ROMAW_CLASSIC_MENU_HIGHLIGHT);
    }
}

/* Native integer arithmetic; guest word conversion belongs to the caller. */
int romaw_classic_menu_advance(int text_width)
{
    if (text_width < 0)
        text_width = 0;
    return text_width > INT_MAX - ROMAW_CLASSIC_MENU_GAP ? INT_MAX : text_width + ROMAW_CLASSIC_MENU_GAP;
}
