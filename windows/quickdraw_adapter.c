/*+===================================================================
  File:      quickdraw_adapter.c

  Summary:   Guest memory, byte order and the current GrafPort stay in this adapter.

  Classes:   None.

  Functions: romaw_classic_graphics, romaw_classic_quickdraw

  Origin:    Existing repository source in executor/src/classic-ui; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Guest memory, byte order and the current GrafPort stay in this adapter. */
#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "rsys/aboutbox.h"
#include "rsys/blockinterrupts.h"
#include "rsys/classic_ui_qd.h"

static RGBColor* role_color(void* context, ROMAWClassicColor role)
{
    RGBColor* const* roles = context;
    return roles[role];
}

/* SetRect encodes the guest Rect; the renderer never sees packed Toolbox data. */
static void qd_fill(void* context, const ROMAWClassicRect* bounds, ROMAWClassicColor role)
{
    RGBColor* color = role_color(context, role);
    if (!color)
        return;
    Rect rect;
    SetRect(&rect, bounds->left, bounds->top, bounds->right, bounds->bottom);
    if (role == ROMAW_CLASSIC_MENU_BACKGROUND || role == ROMAW_CLASSIC_MENU_HIGHLIGHT) {
        RGBBackColor(color);
        EraseRect(&rect);
    }
    else {
        RGBForeColor(color);
        PaintRect(&rect);
    }
}

/* QuickDraw already includes the final pixel of a one-pixel line. */
static void qd_line(void* context, int x, int y, int end_x, int end_y, ROMAWClassicColor role)
{
    RGBColor* color = role_color(context, role);
    if (!color)
        return;
    RGBForeColor(color);
    MoveTo(x, y);
    LineTo(end_x, end_y);
}

/* Preserve the original WDEF FrameRect dispatch, including guest overrides. */
static void qd_frame(void* context, const ROMAWClassicRect* bounds, ROMAWClassicColor role)
{
    RGBColor* color = role_color(context, role);
    if (!color)
        return;
    Rect rect;
    SetRect(&rect, bounds->left, bounds->top, bounds->right, bounds->bottom);
    RGBForeColor(color);
    FrameRect(&rect);
}

ROMAWClassicCanvas romaw_classic_quickdraw(RGBColor* const* roles)
{
    ROMAWClassicCanvas canvas = {(void*)roles, qd_fill, qd_line, qd_frame};
    return canvas;
}

/* Native presentation uses the real WDEF/CDEF/MBDF, not a second painter.
   All guest handles, byte order and manager state stay on this side of the API. */
#include "classic-ui/host_graphics.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "MenuMgr.h"
#include "FontMgr.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "rsys/wind.h"
#include "rsys/ctl.h"
#include "rsys/menu.h"
#include "rsys/desktop.h"
#include "rsys/mman.h"

struct host_surface {
    struct host_surface* next;
    uint64_t key;
    uint32_t kind, flags;
    int width, height;
    GWorldPtr world;
    GWorldPtr image_world;
    WindowPtr window;
    ControlHandle vertical, horizontal;
};
static struct host_surface* host_surfaces;

/* Cached objects are genuine Toolbox objects. Their windows remain hidden in
   the Window Manager; Win32 owns presentation, activation and input delivery. */
static void host_surface_dispose(struct host_surface* surface)
{
    if (surface->window) {
        WINDOW_VISIBLE_X(surface->window) = FALSE;
        if (surface->vertical)
            CTL_VIS_X(surface->vertical) = FALSE;
        if (surface->horizontal)
            CTL_VIS_X(surface->horizontal) = FALSE;
        C_DisposeWindow(surface->window);
    }
    if (surface->world)
        C_DisposeGWorld(surface->world);
    if (surface->image_world)
        C_DisposeGWorld(surface->image_world);
    free(surface);
}

static void host_release(uint64_t key)
{
    if (!ROMlib_desktop_is_ui_thread())
        return;
    virtual_int_state_t interrupts = block_virtual_ints();
    struct host_surface** link = &host_surfaces;
    while (*link) {
        struct host_surface* surface = *link;
        if (surface->key == key) {
            *link = surface->next;
            host_surface_dispose(surface);
        }
        else
            link = &surface->next;
    }
    restore_virtual_ints(interrupts);
}

static struct host_surface* host_surface_get(const ROMAWGraphicsRequest* request)
{
    struct host_surface** link = &host_surfaces;
    while (*link && ((*link)->key != request->key || (*link)->kind != request->kind))
        link = &(*link)->next;
    struct host_surface* surface = *link;
    if (surface && (surface->width != request->width || surface->height != request->height)) {
        *link = surface->next;
        host_surface_dispose(surface);
        surface = NULL;
    }
    if (!surface) {
        surface = calloc(1, sizeof *surface);
        if (!surface)
            return NULL;
        Rect bounds;
        /* PixMap rowBytes reserves its upper two bits. Render wide host
           windows in horizontal bands instead of overflowing that field. */
        C_SetRect(&bounds, 0, 0, request->width > 4095 ? 2048 : request->width, request->height);
        HIDDEN_GWorldPtr world;
        HIDDEN_RAW_SET(world, 0);
        if (C_NewGWorld(&world, 32, &bounds, NULL, NULL, 0)) {
            free(surface);
            return NULL;
        }
        surface->world = HIDDEN_NATIVE(world);
        surface->key = request->key;
        surface->kind = request->kind;
        surface->width = request->width;
        surface->height = request->height;
        surface->next = host_surfaces;
        host_surfaces = surface;
    }
    return surface;
}

/* These coordinates are the standard WDEF port rectangle and the CDEF's
   16-pixel control rectangles. The far control edge shares the WDEF outline;
   the final arrow row/column also shares the grow box's separator. */
static void host_document_metrics(const ROMAWGraphicsRequest* r, ROMAWGraphicsMetrics* m)
{
    m->client = (ROMAWGraphicsRect){1, ROMAW_CLASSIC_TITLE_HEIGHT, r->width - 1, r->height - 1};
    int right = r->width - 17, bottom = r->height - 17;
    m->vertical = (ROMAWGraphicsRect){right, m->client.top + r->status_height, r->width - 1, bottom + 1};
    m->horizontal = (ROMAWGraphicsRect){1, bottom, right + 1, r->height - 1};
    m->grow = (ROMAWGraphicsRect){right, bottom, r->width - 1, r->height - 1};
}

static int host_scroll(ControlHandle* out, WindowPtr owner, ROMAWGraphicsRect box, const ROMAWGraphicsScroll* state)
{
    Rect bounds;
    C_SetRect(&bounds, box.left, box.top, box.right, box.bottom);
    if (!*out) {
        *out = C_NewControl(owner, &bounds, (StringPtr) "", FALSE, 0, 0, 32767, scrollBarProc, 0);
        if (!*out)
            return memFullErr;
    }
    CTL_RECT(*out) = bounds;
    CTL_VIS_X(*out) = 255;
    CTL_MIN_X(*out) = CW(state->minimum);
    CTL_MAX_X(*out) = CW(state->maximum);
    CTL_VALUE_X(*out) = CW(state->value);
    CTL_HILITE_X(*out) = !state->enabled       ? 255
                         : state->pressed == 0 ? inUpButton
                         : state->pressed == 1 ? inDownButton
                                               : 0;
    /* Force a full draw on this surface, not an incremental screen update
       based on the previous thumb's region. All sprites retain all 16 pixels. */
    C_SetEmptyRgn(CTL_DATA(*out));
    C_cdef16(0, *out, drawCntl, ENTIRECONTROL);
    return noErr;
}

static int host_document(struct host_surface* s, const ROMAWGraphicsRequest* r, ROMAWGraphicsMetrics* m)
{
    host_document_metrics(r, m);
    s->flags = r->flags;
    Rect bounds;
    C_SetRect(&bounds, 1, ROMAW_CLASSIC_TITLE_HEIGHT, r->width - 2, r->height - 2);
    if (!s->window) {
        s->window = (WindowPtr)C_NewCWindow(NULL, &bounds, (StringPtr)(r->title ? r->title : (const unsigned char*)""),
                                            FALSE, documentProc | 8, (CWindowPtr)-1L, TRUE, 0);
        if (!s->window)
            return memFullErr;
    }
    else
        C_SetWTitle(s->window, (StringPtr)(r->title ? r->title : (const unsigned char*)""));
    C_SetGWorld(s->world, NULL);
    WINDOW_SPARE_FLAG_X(s->window) = !!(r->flags & ROMAW_GRAPHICS_ZOOM);
    WINDOW_VISIBLE_X(s->window) = TRUE;
    WINDOW_HILITED_X(s->window) = !!(r->flags & ROMAW_GRAPHICS_ACTIVE);
    WINDOW_GO_AWAY_FLAG_X(s->window) = !!(r->flags & ROMAW_GRAPHICS_CLOSE);
    /* Hidden Window Manager objects start with empty content regions. Let the
       WDEF calculate its actual geometry for both rendering and hit testing. */
    C_wdef0(documentProc | ((r->flags & ROMAW_GRAPHICS_ZOOM) ? 8 : 0), s->window, wCalcRgns, 0);
    C_wdef0(documentProc | ((r->flags & ROMAW_GRAPHICS_ZOOM) ? 8 : 0), s->window, wDraw, 0);
    if (r->flags & ROMAW_GRAPHICS_CLOSE_PRESSED)
        C_wdef0(documentProc, s->window, wDraw, wInGoAway);
    if (r->flags & ROMAW_GRAPHICS_ZOOM_PRESSED)
        C_wdef0(documentProc | 8, s->window, wDraw, wInZoomOut);
    int error = noErr;
    if (r->flags & ROMAW_GRAPHICS_SCROLLBARS) {
        error = host_scroll(&s->vertical, s->window, m->vertical, &r->vertical);
        if (!error)
            error = host_scroll(&s->horizontal, s->window, m->horizontal, &r->horizontal);
    }
    /* StandardWDEF's grow drawing also supplies the shared top/left rules.
       Populate the hidden window's local vis region just for this draw. */
    if (r->flags & ROMAW_GRAPHICS_SCROLLBARS) {
        C_RectRgn(PORT_VIS_REGION((GrafPtr)s->window), &PORT_RECT((GrafPtr)s->window));
        C_wdef0(documentProc, s->window, wDrawGIcon, 0);
    }
    WINDOW_VISIBLE_X(s->window) = FALSE;
    C_SetEmptyRgn(PORT_VIS_REGION((GrafPtr)s->window));
    return error;
}

/* Draw into an isolated copy of MenuList using the real StandardMBDF. This
   preserves title widths, Chicago baseline, disabled colours and Apple art. */
static int host_menu(const ROMAWGraphicsRequest* r, ROMAWGraphicsMetrics* m)
{
    Handle saved = C_GetMenuBar();
    if (!saved)
        return memFullErr;
    INTEGER saved_hilite = TheMenu;
    MenuHandle menus[ROMAW_GRAPHICS_MAX_MENUS] = {0};
    C_ClearMenuBar();
    int error = noErr;
    for (int i = 0; i < r->menu_count; ++i) {
        menus[i] = C_NewMenu(1024 + i, (StringPtr)r->menus[i].text);
        if (!menus[i]) {
            error = memFullErr;
            break;
        }
        if (!r->menus[i].enabled)
            C_DisableItem(menus[i], 0);
        C_InsertMenu(menus[i], 0);
    }
    if (!error) {
        C_DrawMenuBar();
        if (r->selected_menu >= 0)
            C_HiliteMenu(1024 + r->selected_menu);
        menulist* list = STARH(MENULIST);
        m->menu_height = CW(MBarHeight);
        for (int i = 0; i < r->menu_count; ++i)
            m->menu_titles[i] = (ROMAWGraphicsRect){
                CW(list->mulist[i].muleft), 0,
                i + 1 < r->menu_count ? CW(list->mulist[i + 1].muleft) : CW(list->muright), m->menu_height};
    }
    C_SetMenuBar(saved);
    DisposHandle(saved);
    TheMenu = saved_hilite;
    for (int i = 0; i < r->menu_count; ++i)
        if (menus[i])
            C_DisposeMenu(menus[i]);
    return error;
}

/* Build a checked, one-item MenuHandle inside the packed-record boundary.
   Legacy NewMenu/SetItem assume allocation succeeds; host painting must be
   able to return memFullErr instead. Labels never pass through markup parsing. */
static int host_menu_item(const ROMAWGraphicsRequest* r, ROMAWGraphicsMetrics* m, int draw)
{
    StringPtr title = (StringPtr)r->title;
    MenuHandle menu = (MenuHandle)NewHandleClear(SIZEOFMINFO + title[0] + SIZEOFMEXT + 1);
    if (!menu)
        return memFullErr;
    HxX(menu, menuID) = CWC(32760);
    HxX(menu, enableFlags) = CL(r->flags & ROMAW_GRAPHICS_ITEM_ENABLED ? -1 : 1);
    StringPtr label = (StringPtr)((char*)STARH(menu) + SIZEOFMINFO);
    memcpy(label, title, title[0] + 1);
    mextp options = (mextp)(label + title[0] + 1);
    options->mmarker = r->flags & ROMAW_GRAPHICS_ITEM_CHECKED ? checkMark : 0;
    options->mkeyeq = r->flags & ROMAW_GRAPHICS_ITEM_SUBMENU ? 0x1b : 0;
    Rect bounds;
    C_SetRect(&bounds, 0, 0, r->width, r->height);
    int width, height;
    ROMlib_draw_host_menu_item(menu, &bounds, (StringPtr)r->shortcut, r->icon_width,
                               !!(r->flags & ROMAW_GRAPHICS_ITEM_ENABLED) &&
                                   !(r->flags & ROMAW_GRAPHICS_ITEM_SEPARATOR) &&
                                   !!(r->flags & ROMAW_GRAPHICS_ITEM_SELECTED),
                               !!(r->flags & ROMAW_GRAPHICS_ITEM_SEPARATOR), draw, &width, &height);
    m->menu_item_width = width;
    m->menu_item_height = height;
    DisposHandle((Handle)menu);
    return noErr;
}

/* A temporary standard ControlRecord borrows the isolated GWorld as its port.
   It is never installed in a guest window/control list. CDEF 0 supplies the
   exact 12-pixel box, X, pressed border and inactive pattern used by Mac apps. */
static int host_control(const ROMAWGraphicsRequest* r, GWorldPtr port)
{
    ControlHandle control = (ControlHandle)NewHandleClear(sizeof(ControlRecord));
    if (!control)
        return memFullErr;
    FIELD_ASSIGN_RAW(CTL_OWNER_X(control), RM((WindowPtr)port));
    C_SetRect(&CTL_RECT(control), 0, 0, r->width, r->height);
    HxX(control, contrlVis) = TRUE;
    if (r->title)
        memcpy(CTL_TITLE(control), r->title, r->title[0] + 1);
    CTL_VALUE_X(control) = CW(!!(r->flags & ROMAW_GRAPHICS_ITEM_CHECKED));
    CTL_HILITE_X(control) = !(r->flags & ROMAW_GRAPHICS_ITEM_ENABLED) ? 255
                            : r->flags & ROMAW_GRAPHICS_ITEM_SELECTED
                                ? (r->kind == ROMAW_GRAPHICS_BUTTON ? inButton : inCheckBox)
                                : 0;
    int proc = r->kind == ROMAW_GRAPHICS_BUTTON  ? pushButProc
               : r->kind == ROMAW_GRAPHICS_RADIO ? radioButProc
                                                 : checkBoxProc;
    if (r->flags & ROMAW_GRAPHICS_ITEM_DEFAULT)
        C_InsetRect(&CTL_RECT(control), 4, 4);
    C_cdef0(proc, control, drawCntl, ENTIRECONTROL);
    if (r->flags & ROMAW_GRAPHICS_ITEM_DEFAULT) {
        Rect button_frame;
        C_SetRect(&button_frame, 0, 0, r->width, r->height);
        ROMlib_frame_default_button(&button_frame);
    }
    DisposHandle((Handle)control);
    return noErr;
}

static RGBColor host_rgb(uint32_t color)
{
    RGBColor result = {CW(((color >> 16) & 255) * 257), CW(((color >> 8) & 255) * 257), CW((color & 255) * 257)};
    return result;
}

struct host_text_line {
    uint32_t start, length;
    int width;
};

/* Layout and glyphs use the same Font Manager as emulated applications. All
   slices are bounded; native edits may supply their existing wrapped lines. */
static int host_text(const ROMAWGraphicsRequest* r, ROMAWGraphicsMetrics* metrics, int draw)
{
    C_TextFont(r->font);
    C_TextSize(r->font_size);
    C_TextFace(r->font_style);
    FontInfo info;
    C_GetFontInfo(&info);
    int ascent = CW(info.ascent),
        line_height = r->line_height ? r->line_height : CW(info.ascent) + CW(info.descent) + CW(info.leading);
    line_height = MAX(1, line_height);
    struct host_text_line* lines = calloc(r->text_length + 1, sizeof *lines);
    if (!lines)
        return memFullErr;
    uint32_t at = 0, count = 0;
    do {
        uint32_t end = at;
        while (end < r->text_length && r->text[end] != '\r' && r->text[end] != '\n')
            ++end;
        uint32_t length = end - at;
        if (r->text_flags & ROMAW_TEXT_WRAP) {
            uint32_t low = 0, high = length;
            while (low < high) {
                uint32_t mid = low + (high - low + 1) / 2;
                if (C_TextWidth((Ptr)r->text, at, mid) <= r->width)
                    low = mid;
                else
                    high = mid - 1;
            }
            if (low < length) {
                length = MAX(1, low);
                for (uint32_t split = length; split > 0; --split)
                    if (r->text[at + split - 1] == ' ') {
                        length = split;
                        break;
                    }
            }
        }
        lines[count] = (struct host_text_line){at, length, C_TextWidth((Ptr)r->text, at, length)};
        metrics->text_width = MAX(metrics->text_width, lines[count].width);
        ++count;
        at += length;
        if (at < end && (r->text_flags & ROMAW_TEXT_WRAP)) {
            while (at < end && r->text[at] == ' ')
                ++at;
        }
        else if (at < r->text_length) {
            if (r->text[at] == '\r')
                ++at;
            if (at < r->text_length && r->text[at] == '\n')
                ++at;
        }
    } while (at < r->text_length);
    metrics->text_height = count * line_height;
    metrics->text_ascent = ascent;
    int top = r->text_flags & ROMAW_TEXT_VCENTER  ? (r->height - metrics->text_height) / 2
              : r->text_flags & ROMAW_TEXT_BOTTOM ? r->height - metrics->text_height
                                                  : 0;
    RGBColor foreground = host_rgb(r->foreground), background = host_rgb(r->background);
    C_RGBForeColor(&foreground);
    C_RGBBackColor(&background);
    if (draw)
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t length = lines[i].length;
            int width = lines[i].width;
            int ellipsis = (r->text_flags & ROMAW_TEXT_ELLIPSIS) && width > r->width;
            if (ellipsis) {
                int dots = C_TextWidth((Ptr) "...", 0, 3);
                while (length && C_TextWidth((Ptr)r->text, lines[i].start, length) + dots > r->width)
                    --length;
                width = C_TextWidth((Ptr)r->text, lines[i].start, length) + dots;
            }
            int x = r->text_flags & ROMAW_TEXT_CENTER  ? (r->width - width) / 2
                    : r->text_flags & ROMAW_TEXT_RIGHT ? r->width - width
                                                       : 0;
            C_MoveTo(x, top + i * line_height + ascent);
            C_DrawText((Ptr)r->text, lines[i].start, length);
            if (ellipsis)
                C_DrawText((Ptr) "...", 0, 3);
        }
    free(lines);
    return noErr;
}

static void host_field(const ROMAWGraphicsRequest* r)
{
    Rect bounds;
    C_SetRect(&bounds, 0, 0, r->width, r->height);
    C_FrameRect(&bounds);
    if (r->kind == ROMAW_GRAPHICS_PROGRESS) {
        C_InsetRect(&bounds, 2, 2);
        int64_t range = (int64_t)r->horizontal.maximum - r->horizontal.minimum;
        int64_t value = MAX(0, MIN((int64_t)r->horizontal.value - r->horizontal.minimum, range));
        bounds.right = CW(CW(bounds.left) + (range > 0 ? (CW(bounds.right) - CW(bounds.left)) * value / range : 0));
        RGBColor color = host_rgb(0x737373);
        C_RGBForeColor(&color);
        C_PaintRect(&bounds);
    }
}

static int host_drawing_valid(const ROMAWGraphicsRequest* r)
{
    const ROMAWGraphicsDrawing* d = r->drawing;
    if (!d || d->operation > ROMAW_DRAW_IMAGE || d->pen_width > 128 || d->filled > 1 || d->bounds.left < -32768 ||
        d->bounds.top < -32768 || d->bounds.right > 32767 || d->bounds.bottom > 32767 ||
        d->bounds.right < d->bounds.left || d->bounds.bottom < d->bounds.top || d->end_x < -32768 || d->end_x > 32767 ||
        d->end_y < -32768 || d->end_y > 32767 || d->oval_width < 0 || d->oval_width > 32767 || d->oval_height < 0 ||
        d->oval_height > 32767)
        return 0;
    if (d->operation == ROMAW_DRAW_POLYGON) {
        if (!d->points || d->point_count < 3 || d->point_count > 4096)
            return 0;
        for (uint32_t i = 0; i < d->point_count; ++i)
            if (d->points[i].x < -32768 || d->points[i].x > 32767 || d->points[i].y < -32768 || d->points[i].y > 32767)
                return 0;
    }
    if (d->operation == ROMAW_DRAW_IMAGE &&
        (!d->image || d->image_stride < (size_t)r->width * 4 || (d->image_stride & 3) ||
         d->image_stride > SIZE_MAX / r->height || d->image_bytes < d->image_stride * r->height))
        return 0;
    return 1;
}

/* Import host RGB into the offscreen PixMap. This is an endian boundary, not
   another renderer. It also normalizes premultiplied host artwork: classic
   QuickDraw has binary masks, so fractional alpha is resolved against the
   supplied background before CopyBits performs the image transfer. */
static int host_import(GWorldPtr world, const ROMAWGraphicsRequest* r, const uint32_t* pixels, size_t stride, int left,
                       int width, int image)
{
    PixMapHandle map = C_GetGWorldPixMap(world);
    if (!C_LockPixels(map))
        return memFullErr;
    unsigned char* base = (unsigned char*)C_GetPixBaseAddr(map);
    size_t target_stride = PIXMAP_ROWBYTES(map);
    for (int y = 0; y < r->height; ++y) {
        const uint32_t* row = (const uint32_t*)((const unsigned char*)pixels + y * stride);
        const uint32_t* art =
            image ? (const uint32_t*)((const unsigned char*)r->drawing->image + y * r->drawing->image_stride) : NULL;
        unsigned char* target = base + y * target_stride;
        for (int x = 0; x < width; ++x) {
            uint32_t color = row[left + x];
            unsigned inverse = art ? 255 - (art[left + x] >> 24) : 0;
            target[x * 4] = 0;
            for (int channel = 0; channel < 3; ++channel) {
                unsigned shift = 16 - channel * 8;
                unsigned value = (color >> shift) & 255;
                if (art)
                    value =
                        MIN(255, MIN(255 - inverse, (art[left + x] >> shift) & 255) + (value * inverse + 127) / 255);
                target[x * 4 + 1 + channel] = value;
            }
        }
    }
    C_UnlockPixels(map);
    return noErr;
}

static int host_drawing(struct host_surface* surface, const ROMAWGraphicsRequest* r, const uint32_t* pixels,
                        size_t stride, int left, int width)
{
    const ROMAWGraphicsDrawing* d = r->drawing;
    int error = host_import(surface->world, r, pixels, stride, left, width, 0);
    if (error)
        return error;
    RGBColor color = host_rgb(r->foreground);
    C_RGBForeColor(&color);
    C_PenSize(MAX(1, d->pen_width), MAX(1, d->pen_width));
    Rect bounds;
    C_SetRect(&bounds, d->bounds.left, d->bounds.top, d->bounds.right, d->bounds.bottom);
    switch (d->operation) {
    case ROMAW_DRAW_FILL:
        C_PaintRect(&bounds);
        break;
    case ROMAW_DRAW_FRAME:
        C_FrameRect(&bounds);
        break;
    case ROMAW_DRAW_LINE:
        C_MoveTo(d->bounds.left, d->bounds.top);
        C_LineTo(d->end_x, d->end_y);
        break;
    case ROMAW_DRAW_ROUND_RECT:
        if (d->filled)
            C_PaintRoundRect(&bounds, d->oval_width, d->oval_height);
        else
            C_FrameRoundRect(&bounds, d->oval_width, d->oval_height);
        break;
    case ROMAW_DRAW_POLYGON: {
        PolyHandle polygon = C_OpenPoly();
        if (!polygon)
            return memFullErr;
        C_MoveTo(d->points[0].x, d->points[0].y);
        for (uint32_t i = 1; i <= d->point_count; ++i)
            C_LineTo(d->points[i % d->point_count].x, d->points[i % d->point_count].y);
        C_ClosePoly();
        if (d->filled)
            C_PaintPoly(polygon);
        else
            C_FramePoly(polygon);
        C_KillPoly(polygon);
        break;
    }
    case ROMAW_DRAW_IMAGE: {
        if (!surface->image_world) {
            Rect size;
            C_SetRect(&size, 0, 0, r->width > 4095 ? 2048 : r->width, r->height);
            HIDDEN_GWorldPtr image;
            HIDDEN_RAW_SET(image, 0);
            error = C_NewGWorld(&image, 32, &size, NULL, NULL, 0);
            if (error)
                return error;
            surface->image_world = HIDDEN_NATIVE(image);
        }
        error = host_import(surface->image_world, r, pixels, stride, left, width, 1);
        if (error)
            return error;
        Rect source, destination;
        C_SetRect(&source, 0, 0, width, r->height);
        C_SetRect(&destination, left, 0, left + width, r->height);
        C_RGBForeColor(&ROMlib_black_rgb_color);
        C_RGBBackColor(&ROMlib_white_rgb_color);
        C_CopyBits((BitMap*)STARH(C_GetGWorldPixMap(surface->image_world)),
                   (BitMap*)STARH(C_GetGWorldPixMap(surface->world)), &source, &destination, srcCopy, NULL);
        break;
    }
    }
    return noErr;
}

static void host_about(const Rect* bounds)
{
    /* The classic dBoxProc surrounds its content with two black rules. */
    Rect rule_bounds = *bounds;
    C_FrameRect(&rule_bounds);
    C_InsetRect(&rule_bounds, 1, 1);
    RGBColor frame_gray = host_rgb(0xaaaaaa);
    C_RGBForeColor(&frame_gray);
    C_PenSize(2, 2);
    C_FrameRect(&rule_bounds);
    C_InsetRect(&rule_bounds, 2, 2);
    C_PenNormal();
    C_RGBForeColor(&ROMlib_black_rgb_color);
    C_FrameRect(&rule_bounds);
    ROMlib_draw_about_contents(ROMAW_ABOUT_FRAME, ROMAW_ABOUT_FRAME);
}

/* Entry points take host-order values and produce native RGB pixels. Packed
   records and big-endian 32-bit QuickDraw pixels never reach the Win32 layer. */
static int host_render(const ROMAWGraphicsRequest* r, uint32_t* pixels, size_t stride, ROMAWGraphicsMetrics* output)
{
    if (!ROMlib_desktop_is_ui_thread() || !r || r->size != sizeof *r || !output || r->width <= 0 || r->height <= 0 ||
        r->width > 16384 || r->height > 16384 || (uint64_t)r->width * r->height > 64 * 1024 * 1024 ||
        (pixels && (stride < (size_t)r->width * 4 || (stride & 3) || stride > SIZE_MAX / r->height)) ||
        (r->kind != ROMAW_GRAPHICS_DOCUMENT && r->kind != ROMAW_GRAPHICS_MENU_BAR &&
         r->kind != ROMAW_GRAPHICS_DESKTOP && r->kind != ROMAW_GRAPHICS_MENU_ITEM &&
         r->kind != ROMAW_GRAPHICS_CHECKBOX && r->kind != ROMAW_GRAPHICS_BUTTON && r->kind != ROMAW_GRAPHICS_RADIO &&
         r->kind != ROMAW_GRAPHICS_TEXT && r->kind != ROMAW_GRAPHICS_FIELD && r->kind != ROMAW_GRAPHICS_PROGRESS &&
         r->kind != ROMAW_GRAPHICS_DRAWING && r->kind != ROMAW_GRAPHICS_ABOUT) ||
        (r->kind == ROMAW_GRAPHICS_ABOUT && (r->width != ROMAW_ABOUT_WIDTH + 2 * ROMAW_ABOUT_FRAME ||
                                             r->height != ROMAW_ABOUT_HEIGHT + 2 * ROMAW_ABOUT_FRAME)) ||
        (r->kind == ROMAW_GRAPHICS_DRAWING && (!pixels || !host_drawing_valid(r))) ||
        (r->kind == ROMAW_GRAPHICS_MENU_ITEM && (!r->title || r->icon_width < 0 || r->icon_width > 256)) ||
        (r->kind == ROMAW_GRAPHICS_TEXT &&
         ((!r->text && r->text_length) || r->text_length > 32767 || r->font < 0 || r->font > 32767 ||
          r->font_size < 1 || r->font_size > 128 || r->line_height < 0 || r->line_height > 256)) ||
        (r->kind == ROMAW_GRAPHICS_DOCUMENT &&
         (r->width < 48 || r->status_height < 0 || r->status_height > r->height - 68)) ||
        (r->kind == ROMAW_GRAPHICS_MENU_BAR &&
         (r->menu_count < 0 || r->menu_count > ROMAW_GRAPHICS_MAX_MENUS || (r->menu_count && !r->menus) ||
          r->selected_menu < -1 || r->selected_menu >= r->menu_count)))
        return paramErr;
    for (int i = 0; r->kind == ROMAW_GRAPHICS_MENU_BAR && i < r->menu_count; ++i)
        if (!r->menus[i].text)
            return paramErr;
    if (!pixels && r->kind == ROMAW_GRAPHICS_DOCUMENT) {
        ROMAWGraphicsMetrics metrics = {0};
        host_document_metrics(r, &metrics);
        *output = metrics;
        return noErr;
    }
    /* QuickDraw can poll virtual interrupts while constructing a GWorld.
       The SDL interrupt pumps Win32 messages, so it must not dispatch another
       paint while this render has temporary guest ports and surfaces installed. */
    virtual_int_state_t interrupts = block_virtual_ints();
    HIDDEN_CGrafPtr saved_port;
    HIDDEN_GDHandle saved_device;
    C_GetGWorld(&saved_port, &saved_device);
    HIDDEN_CGrafPtr saved_manager = WMgrCPort_H;
    HIDDEN_GrafPtr saved_mono_manager = WMgrPort_H;
    ROMAWGraphicsMetrics metrics = {0};
    struct host_surface* surface = host_surface_get(r);
    if (!surface) {
        C_SetGWorld(HIDDEN_NATIVE(saved_port), HIDDEN_NATIVE(saved_device));
        restore_virtual_ints(interrupts);
        return memFullErr;
    }
    HIDDEN_RgnHandle saved_vis = SaveVisRgn_H;
    HIDDEN_RAW_SET(SaveVisRgn_H, 0);
    C_SetGWorld(surface->world, NULL);
    HIDDEN_RAW_SET(WMgrCPort_H, RM(surface->world));
    HIDDEN_RAW_SET(WMgrPort_H, RM((GrafPtr)surface->world));
    Rect bounds;
    C_SetRect(&bounds, 0, 0, r->width, r->height);
    int error = noErr;
    /* Power-of-two bands preserve the phase of classic pattern tiles while
       leaving room for the PixMap's rowBytes flag bits at 32-bit depth. */
    int tile_width = r->width > 4095 ? 2048 : r->width, origin = 0;
    for (int left = 0; left < r->width && !error; left += tile_width) {
        /* SetOrigin moves the bitmap bounds as well as the visible region.
       Managers still see the entire logical window through portRect. */
        C_SetRect(&PORT_RECT((GrafPtr)surface->world), origin, 0, origin + tile_width, r->height);
        C_SetOrigin(left, 0);
        origin = left;
        PORT_RECT((GrafPtr)surface->world) = bounds;
        C_RectRgn(PORT_VIS_REGION((GrafPtr)surface->world), &bounds);
        C_ClipRect(&bounds);
        RGBColor background = host_rgb(
            (r->kind == ROMAW_GRAPHICS_TEXT || r->kind == ROMAW_GRAPHICS_BUTTON || r->kind == ROMAW_GRAPHICS_FIELD)
                ? r->background
                : 0xffffff);
        C_RGBBackColor(&background);
        C_RGBForeColor(&ROMlib_black_rgb_color);
        if (r->kind != ROMAW_GRAPHICS_DRAWING)
            C_EraseRect(&bounds);
        C_PenNormal();
        C_TextFont(0);
        C_TextSize(12);
        C_TextFace(0);
        C_TextMode(srcOr);
        if (r->kind == ROMAW_GRAPHICS_DRAWING)
            error = host_drawing(surface, r, pixels, stride, left, MIN(tile_width, r->width - left));
        else if (r->kind == ROMAW_GRAPHICS_DOCUMENT)
            error = host_document(surface, r, &metrics);
        else if (r->kind == ROMAW_GRAPHICS_MENU_BAR)
            error = host_menu(r, &metrics);
        else if (r->kind == ROMAW_GRAPHICS_MENU_ITEM)
            error = host_menu_item(r, &metrics, pixels != NULL);
        else if (r->kind == ROMAW_GRAPHICS_CHECKBOX || r->kind == ROMAW_GRAPHICS_BUTTON ||
                 r->kind == ROMAW_GRAPHICS_RADIO)
            error = host_control(r, surface->world);
        else if (r->kind == ROMAW_GRAPHICS_TEXT)
            error = host_text(r, &metrics, pixels != NULL);
        else if (r->kind == ROMAW_GRAPHICS_ABOUT)
            host_about(&bounds);
        else if (r->kind == ROMAW_GRAPHICS_FIELD || r->kind == ROMAW_GRAPHICS_PROGRESS)
            host_field(r);
        else {
            PixPatHandle pattern = C_GetPixPat(17);
            if (pattern) {
                C_FillCRect(&bounds, pattern);
                C_DisposPixPat(pattern);
            }
            else
                error = resNotFound;
            metrics.client = (ROMAWGraphicsRect){0, 0, r->width, r->height};
        }
        if (!error && pixels) {
            PixMapHandle map = C_GetGWorldPixMap(surface->world);
            if (!C_LockPixels(map))
                error = memFullErr;
            else {
                const unsigned char* base = (unsigned char*)C_GetPixBaseAddr(map);
                size_t source_stride = PIXMAP_ROWBYTES(map);
                for (int y = 0; y < r->height; ++y) {
                    uint32_t* row = (uint32_t*)((unsigned char*)pixels + y * stride);
                    const unsigned char* source = base + y * source_stride;
                    for (int x = 0; x < MIN(tile_width, r->width - left); ++x)
                        row[left + x] = ((uint32_t)source[x * 4 + 1] << 16) | ((uint32_t)source[x * 4 + 2] << 8) |
                                        source[x * 4 + 3];
                }
                C_UnlockPixels(map);
            }
        }
    }
    C_SetGWorld(surface->world, NULL);
    C_SetRect(&PORT_RECT((GrafPtr)surface->world), origin, 0, origin + tile_width, r->height);
    C_SetOrigin(0, 0);
    SaveVisRgn_H = saved_vis;
    WMgrCPort_H = saved_manager;
    WMgrPort_H = saved_mono_manager;
    C_SetGWorld(HIDDEN_NATIVE(saved_port), HIDDEN_NATIVE(saved_device));
    restore_virtual_ints(interrupts);
    if (!error)
        *output = metrics;
    return error;
}

/* Hit testing comes from the same WDEF which produced the cached pixels. */
static int host_document_hit(uint64_t key, int32_t x, int32_t y)
{
    if (!ROMlib_desktop_is_ui_thread() || x < -32768 || x > 32767 || y < -32768 || y > 32767)
        return ROMAW_GRAPHICS_NO_HIT;
    for (struct host_surface* surface = host_surfaces; surface; surface = surface->next) {
        if (surface->key == key && surface->kind == ROMAW_GRAPHICS_DOCUMENT && surface->window) {
            int variant = surface->flags & ROMAW_GRAPHICS_ZOOM ? documentProc | 8 : noGrowDocProc;
            return C_wdef0(variant, surface->window, wHit, (int32_t)(((uint32_t)y << 16) | ((uint32_t)x & 65535)));
        }
    }
    return ROMAW_GRAPHICS_NO_HIT;
}

const ROMAWClassicGraphics* romaw_classic_graphics(void)
{
    static const ROMAWClassicGraphics graphics = {sizeof graphics, ROMAW_GRAPHICS_ABI, host_render, host_release,
                                                  host_document_hit};
    return ROMlib_desktop_is_ui_thread() ? &graphics : NULL;
}
