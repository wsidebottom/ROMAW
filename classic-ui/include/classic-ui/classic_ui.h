/*+===================================================================
  File:      classic_ui.h

  Summary:   Host-independent classic window/menu drawing with no guest pointers or records.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/classic-ui; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Host-independent classic window/menu drawing. No guest pointers or records. */
#ifndef ROMAW_CLASSIC_UI_H
#define ROMAW_CLASSIC_UI_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ROMAW_CLASSIC_TITLE_HEIGHT = 19,
    ROMAW_CLASSIC_SCROLLBAR_WIDTH = 16,
    ROMAW_CLASSIC_MENU_LEFT = 10,
    ROMAW_CLASSIC_MENU_GAP = 13,
    ROMAW_CLASSIC_MENU_BASELINE = 14
};

/* Reference classic scrollbar track: light field with one dark pixel in four.
   Packed host-order 0xRRGGBB, independent of COLORREF and guest RGBColor. */
enum { ROMAW_CLASSIC_SCROLL_TRACK_LIGHT_RGB = 0xdedade, ROMAW_CLASSIC_SCROLL_TRACK_DARK_RGB = 0x737173 };

typedef enum ROMAWClassicArrowState {
    ROMAW_CLASSIC_ARROW_INACTIVE,
    ROMAW_CLASSIC_ARROW_ACTIVE,
    ROMAW_CLASSIC_ARROW_CLICKED
} ROMAWClassicArrowState;

ROMAWClassicArrowState romaw_classic_arrow_state(int enabled, int pressed);

typedef enum ROMAWClassicColor {
    ROMAW_CLASSIC_TITLE_BACKGROUND,
    ROMAW_CLASSIC_FRAME_OUTLINE,
    ROMAW_CLASSIC_FRAME_SHADOW,
    ROMAW_CLASSIC_TITLE_LIGHT,
    ROMAW_CLASSIC_TITLE_SHADOW,
    ROMAW_CLASSIC_TITLE_RULE,
    ROMAW_CLASSIC_MENU_BACKGROUND,
    ROMAW_CLASSIC_MENU_HIGHLIGHT,
    ROMAW_CLASSIC_MENU_OUTLINE,
    ROMAW_CLASSIC_COLOR_COUNT
} ROMAWClassicColor;

/* Rectangles are half-open; line endpoints are inclusive, as in QuickDraw. */
typedef struct ROMAWClassicRect {
    int left, top, right, bottom;
} ROMAWClassicRect;

/* Borrowed callbacks/context are valid for the duration of a drawing call.
   Adapters own clipping, colour conversion, fonts, icons and graphics state. */
typedef struct ROMAWClassicCanvas {
    void* context;
    void (*fill_rect)(void*, const ROMAWClassicRect*, ROMAWClassicColor);
    void (*stroke_line)(void*, int, int, int, int, ROMAWClassicColor);
    void (*frame_rect)(void*, const ROMAWClassicRect*, ROMAWClassicColor);
} ROMAWClassicCanvas;

void romaw_classic_document_frame(const ROMAWClassicCanvas* canvas, const ROMAWClassicRect* content, int title_height);
void romaw_classic_title_bevel(const ROMAWClassicCanvas* canvas, int left, int top, int right, int height);
void romaw_classic_title_rules(const ROMAWClassicCanvas* canvas, int left, int top, int right, int height);
void romaw_classic_menu_bar(const ROMAWClassicCanvas* canvas, const ROMAWClassicRect* bounds);
void romaw_classic_menu_title(const ROMAWClassicCanvas* canvas, const ROMAWClassicRect* bounds, int highlighted);
int romaw_classic_menu_advance(int text_width);

#ifdef __cplusplus
}
#endif
#endif
