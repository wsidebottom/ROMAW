/*+===================================================================
  File:      classic.h

  Summary:   Shared classic Finder painting, controls, redraw transactions and movable dialogs.

  Classes:   Declared: IconViewImage, WaitingCursor; Used: ROMAWClassicGraphics, IconViewImage, WaitingCursor, std::wstring

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef FINDER_CLASSIC_H
#define FINDER_CLASSIC_H
#include <windows.h>
#include <commctrl.h>
#include "classic-ui/host_graphics.h"
#include <cstddef>
#include <string>

#include <vector>

namespace finder {
struct FindOptions;
struct SearchRoot;
}

namespace finder::classic {
// Scope synchronous UI work without leaving a busy cursor after exceptions.
// Background workers never own this UI-thread-only state.
class WaitingCursor {
  public:
    WaitingCursor();
    ~WaitingCursor();
    WaitingCursor(const WaitingCursor&) = delete;
    WaitingCursor& operator=(const WaitingCursor&) = delete;

  private:
    HCURSOR previous;
};

// Update a native window hierarchy as one presentation transaction. Nested
// scopes share the top-level owner. Paint suppression never changes visibility
// or input routing, including for an initially hidden window.
class RedrawBatch {
  public:
    explicit RedrawBatch(HWND window);
    ~RedrawBatch();
    RedrawBatch(const RedrawBatch&) = delete;
    RedrawBatch& operator=(const RedrawBatch&) = delete;

  private:
    HWND root = nullptr;
};

// Direct GDI/QuickDraw adapters must honor the same redraw suspension as Win32.
bool redraw_suspended(HWND window);

// Reapply the scoped cursor when styled controls receive WM_SETCURSOR.
bool waiting_cursor();
// Install embedded fonts and shared GDI state on the UI thread before styling
// controls. Font buffers are borrowed during initialization.
void initialize(const void* chicago_data, size_t chicago_size, const void* geneva_data, size_t geneva_size);
// Destroy decorated windows before releasing shared fonts/menu bookkeeping.
void finish();
// Borrow the initialized Toolbox renderer; it stays on this UI thread.
void use_graphics(const ROMAWClassicGraphics* graphics);
// Shared QuickDraw text/control presentation. Windows keeps the control model
// and preserves Unicode glyphs unavailable in the classic MacRoman fonts.
const ROMAWClassicGraphics* shared_graphics();
int draw_text(HDC dc, const wchar_t* text, int length, RECT* bounds, UINT flags);
// Host DCs supply placement/clipping and present completed RGB surfaces only.
// Shapes and artwork are rasterized by the initialized QuickDraw adapter.
int fill_rect(HDC dc, const RECT* bounds, HBRUSH brush);
int frame_rect(HDC dc, const RECT* bounds, HBRUSH brush);
void fill_color(HDC dc, RECT bounds, COLORREF color);
void draw_line(HDC dc, int x, int y, int end_x, int end_y, COLORREF color);
BOOL draw_round_rect(HDC dc, int left, int top, int right, int bottom, int oval_width, int oval_height);
BOOL draw_polygon(HDC dc, const POINT* points, int count);
BOOL draw_icon(HDC dc, int x, int y, HICON icon, int width, int height, UINT frame = 0, HBRUSH background = nullptr,
               UINT flags = DI_NORMAL);
// Save the same icon shown in Finder as a Windows ICO without replacing a file.
bool save_icon_file(HICON icon, const wchar_t* path);
BOOL draw_image(HIMAGELIST images, int index, HDC dc, int x, int y, int width, int height);
BOOL draw_pixels(HDC dc, RECT bounds, const uint32_t* pixels, size_t bytes);
BOOL draw_bitmap(HDC dc, RECT bounds, HBITMAP bitmap);
// Caller-owned premultiplied drag artwork, painted with the same QuickDraw
// icon/text path as the view. Origin is returned in list-client coordinates.
HBITMAP drag_artwork(HWND list, HIMAGELIST images, int image, int item, const wchar_t* name, POINT& origin);
BOOL paint_pixel(HDC dc, int x, int y, COLORREF color);
// Center the complete single-line QuickDraw label on the native icon label.
RECT icon_label_bounds(HDC dc, const wchar_t* text, RECT bounds, const RECT* viewport = nullptr);

struct IconViewImage {
    int normal, selected;
};

// Repaint background and every visible image as one pass, honoring the DC's
// update clip. Native dirty-item notifications cannot describe this full layer.
void paint_icon_view(HWND list, HDC dc, HIMAGELIST images, const IconViewImage* items, size_t count, bool desktop);
// One Finder view update: erase and draw into a private canvas, then present
// only the caller's update region. Child controls retain their own pixels.
void paint_view(HWND list, HDC dc, void (*paint)(HDC, void*), void* context);
void style_text_control(HWND window, bool edit_frame = false);
void finish_text();
void draw_progress(HDC dc, const RECT& bounds, uint64_t value, uint64_t maximum);
// Borrowed font handles: callers must not delete them.
HFONT text_font();
HFONT system_font();
HFONT small_font();
// Paint a folder's item count and optional borrowed 16 px volume-lock icon.
// The shared 18 px status strip retains its two bottom separator rows.
void draw_folder_status(HDC dc, const RECT& bounds, const wchar_t* text, HICON lock = nullptr);
// Retain the Win32 checkbox model/input while the shared CDEF paints its glyph.
void style_checkbox(HWND window);
// Keep the host application's Windows frame; use classic chrome for children.
void decorate(HWND window, bool native_frame = false);
// Track/raise the front child and repaint classic title-bar activation.
void activate(HWND window);
HWND active_window();
// Owner-draw an entire menu tree while retaining its command IDs/submenus.
void style_menu(HMENU menu);
// Change the caption of an already styled command without rebuilding its menu.
void set_menu_caption(HMENU menu, unsigned command, const wchar_t* caption);
// Track a transient Finder popup with the shared menu renderer.
unsigned track_context_menu(HWND owner, HMENU menu, POINT anchor);
// Keep HMENU as the command/keyboard model; QuickDraw owns bar pixels
// and title rectangles rather than Windows' menu-cell measurements.
void attach_menu(HWND window, HMENU menu);
HMENU window_menu(HWND window);
void redraw_menu_bar(HWND window);
bool translate_menu_key(const MSG& message);
// Create an owned classic selected variant without changing the source mask.
HICON selected_icon(HICON source);
// Update borrowed menu/control/title references before the owner frees an icon.
void replace_icon(HICON previous, HICON replacement);
// These subclasses change presentation, not the controls' native data models.
void style_progress(HWND window);
void style_dialog(HWND window);
void style_list(HWND window);
// Anchor the resource pattern to desktop coordinates through partial repaints.
// Installed subclasses own their brushes until their windows are destroyed.
void style_desktop(HWND window);
// The caller owns this resource-derived DIB.
HBITMAP desktop_pattern();
COLORREF desktop_color();
// Artwork handles are borrowed; their owner must keep them alive for painting.
void artwork(HICON stop, HICON warning, HICON app, HICON apple, const HICON* label_icons);
void chrome_art(HICON close, HICON zoom, HICON grow, HICON clicked = nullptr);
void menu_icon(unsigned command, HICON icon);
// Movable notices disable/re-enable their owner, restore focus and forward quit.
int message_box(HWND owner, const wchar_t* text, UINT flags);
void about_box(HWND owner);
// IDYES replaces, IDNO skips, IDCANCEL stops this transfer only.
int conflict_box(HWND owner, const wchar_t* text);
int yes_no_cancel_box(HWND owner, const wchar_t* text);
bool confirm(HWND owner, const wchar_t* text, const wchar_t* affirmative);
// Return label 0..7, or -1 if dismissed without a selection.
int choose_label(HWND owner, int checked);
#ifdef FINDER_TESTING
// Borrow the sibling window that owns the fixed scrollbar gutters.
HWND scrollbar_window(HWND list);
// Paint window-coordinate chrome into a borrowed DC without touching client pixels.
void paint_list_scrollbars(HWND window, HDC dc);
// Paint the classic frame into a borrowed DC, preserving every client pixel.
void paint_window_chrome(HWND window, HDC dc);
// Caller-owned DIB; the returned pixel pointer is valid until DeleteObject.
HBITMAP label_menu_bitmap(void** pixels);
#endif
// Input values change only when the user accepts the dialog.
bool find_box(HWND owner, FindOptions& value, bool has_selection, const std::vector<SearchRoot>& disks);
bool input_box(HWND owner, const wchar_t* text, std::wstring& value);
}
#endif
