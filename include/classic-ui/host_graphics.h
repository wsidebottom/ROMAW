/*+===================================================================
  File:      host_graphics.h

  Summary:   Host-order rendering and drawing interface to ROMAW's QuickDraw managers on the initialized Toolbox/UI
             thread. No guest records escape.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/classic-ui; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native, host-order interface to ROMAW's existing QuickDraw managers.
   Call only on the initialized Toolbox/UI thread. No guest records escape. */
#ifndef ROMAW_CLASSIC_HOST_GRAPHICS_H
#define ROMAW_CLASSIC_HOST_GRAPHICS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { ROMAW_GRAPHICS_ABI = 4, ROMAW_GRAPHICS_MAX_MENUS = 32 };

enum {
    ROMAW_GRAPHICS_DOCUMENT,
    ROMAW_GRAPHICS_MENU_BAR,
    ROMAW_GRAPHICS_DESKTOP,
    ROMAW_GRAPHICS_MENU_ITEM,
    ROMAW_GRAPHICS_CHECKBOX,
    ROMAW_GRAPHICS_BUTTON,
    ROMAW_GRAPHICS_RADIO,
    ROMAW_GRAPHICS_TEXT,
    ROMAW_GRAPHICS_FIELD,
    ROMAW_GRAPHICS_PROGRESS,
    ROMAW_GRAPHICS_DRAWING,
    ROMAW_GRAPHICS_ABOUT
};

/* Content geometry shared by the classic dialog and the Finder popup. */
enum {
    ROMAW_ABOUT_WIDTH = 354,
    ROMAW_ABOUT_HEIGHT = 164,
    ROMAW_ABOUT_MARGIN = 16,
    ROMAW_ABOUT_FRAME = 4,
    ROMAW_ABOUT_BUTTON_WIDTH = 86,
    ROMAW_ABOUT_BUTTON_HEIGHT = 22
};

enum {
    ROMAW_GRAPHICS_ITEM_ENABLED = 1,
    ROMAW_GRAPHICS_ITEM_SELECTED = 2,
    ROMAW_GRAPHICS_ITEM_CHECKED = 4,
    ROMAW_GRAPHICS_ITEM_SUBMENU = 8,
    ROMAW_GRAPHICS_ITEM_SEPARATOR = 16,
    ROMAW_GRAPHICS_ITEM_DEFAULT = 32
};

/* Shared text formatting uses host-order values and MacRoman bytes. */
enum {
    ROMAW_TEXT_CENTER = 1,
    ROMAW_TEXT_RIGHT = 2,
    ROMAW_TEXT_VCENTER = 4,
    ROMAW_TEXT_BOTTOM = 8,
    ROMAW_TEXT_WRAP = 16,
    ROMAW_TEXT_ELLIPSIS = 32
};

/* Standard WDEF hit results, expressed without packed Point/Window records. */
enum {
    ROMAW_GRAPHICS_NO_HIT,
    ROMAW_GRAPHICS_CONTENT,
    ROMAW_GRAPHICS_DRAG,
    ROMAW_GRAPHICS_GROW,
    ROMAW_GRAPHICS_CLOSE_HIT,
    ROMAW_GRAPHICS_ZOOM_IN,
    ROMAW_GRAPHICS_ZOOM_OUT
};

enum {
    ROMAW_GRAPHICS_ACTIVE = 1,
    ROMAW_GRAPHICS_CLOSE = 2,
    ROMAW_GRAPHICS_ZOOM = 4,
    ROMAW_GRAPHICS_CLOSE_PRESSED = 8,
    ROMAW_GRAPHICS_ZOOM_PRESSED = 16,
    ROMAW_GRAPHICS_SCROLLBARS = 32
};

typedef struct ROMAWGraphicsRect {
    int32_t left, top, right, bottom;
} ROMAWGraphicsRect;

typedef struct ROMAWGraphicsScroll {
    /* CDEF signed 16-bit range. pressed is -1, first arrow 0, last arrow 1. */
    int32_t minimum, maximum, value, enabled, pressed;
} ROMAWGraphicsScroll;

typedef struct ROMAWGraphicsTitle {
    /* Borrowed Pascal MacRoman title, including its length byte. */
    const unsigned char* text;
    int32_t enabled;
} ROMAWGraphicsTitle;

enum {
    ROMAW_DRAW_FILL,
    ROMAW_DRAW_FRAME,
    ROMAW_DRAW_LINE,
    ROMAW_DRAW_ROUND_RECT,
    ROMAW_DRAW_POLYGON,
    ROMAW_DRAW_IMAGE
};

typedef struct ROMAWGraphicsPoint {
    int32_t x, y;
} ROMAWGraphicsPoint;

/* Coordinates are local to the supplied canvas. Drawing reads the destination
   RGB pixels before changing them, preserving transparent shapes and images.
   Image pixels are premultiplied native 0xAARRGGBB, with a checked byte stride.
   Guest records, handles and endian conversions remain inside the adapter. */
typedef struct ROMAWGraphicsDrawing {
    uint32_t operation, pen_width, filled;
    ROMAWGraphicsRect bounds;
    int32_t end_x, end_y, oval_width, oval_height;
    const ROMAWGraphicsPoint* points;
    uint32_t point_count;
    const uint32_t* image;
    size_t image_stride, image_bytes;
} ROMAWGraphicsDrawing;

typedef struct ROMAWGraphicsRequest {
    uint32_t size, kind;
    uint64_t key;
    int32_t width, height, status_height;
    uint32_t flags;
    const unsigned char* title;
    ROMAWGraphicsScroll vertical, horizontal;
    const ROMAWGraphicsTitle* menus;
    int32_t menu_count, selected_menu;
    /* MENU_ITEM uses title, these fields and ITEM flags. The reserved icon
       column lets a native model retain its borrowed application artwork. */
    const unsigned char* shortcut;
    int32_t icon_width;
    const unsigned char* text;
    uint32_t text_length, text_flags, foreground, background;
    int32_t font, font_size, font_style, line_height;
    const ROMAWGraphicsDrawing* drawing;
} ROMAWGraphicsRequest;

typedef struct ROMAWGraphicsMetrics {
    ROMAWGraphicsRect client, vertical, horizontal, grow;
    int32_t menu_height;
    ROMAWGraphicsRect menu_titles[ROMAW_GRAPHICS_MAX_MENUS];
    int32_t menu_item_width, menu_item_height;
    int32_t text_width, text_height, text_ascent;
} ROMAWGraphicsMetrics;

typedef struct ROMAWClassicGraphics {
    uint32_t size, version;
    /* Top-down native 0x00RRGGBB pixels; stride is in bytes. A null destination
       queries layout without copying pixels. The caller owns all buffers.
       Dimensions are bounded to 16384 and 64 Mi pixels in total. release
       discards all surfaces with the supplied key; other keys stay valid. */
    int (*render)(const ROMAWGraphicsRequest*, uint32_t*, size_t, ROMAWGraphicsMetrics*);
    void (*release)(uint64_t key);
    int (*document_hit)(uint64_t key, int32_t x, int32_t y);
} ROMAWClassicGraphics;

#if defined(_WIN32)
__declspec(dllexport)
#endif
const ROMAWClassicGraphics*
romaw_classic_graphics(void);
#ifdef __cplusplus
}
#endif
#endif
