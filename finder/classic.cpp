/*+===================================================================
  File:      classic.cpp

  Summary:   Classic presentation for native Win32 windows and controls. These painters run on the UI thread, borrow
             supplied icons and own shared font/brush resources. Windows still supplies the control models, focus and
             message delivery.

  Classes:   Declared: FolderZoom, FrameTracking, Icons, LabelPopup, MenuText, NativeClassicCanvas, Notice,
             RenderedDocument, ScrollTracking, ScrollbarLayout, ToolboxMenuBar, Update, WindowDrag, ZoomedChildLayout; Used:
             ROMAWClassicCanvas, ROMAWClassicGraphics, ROMAWClassicRect, ROMAWGraphicsMetrics,
             ROMAWGraphicsRequest, ROMAWGraphicsTitle, FolderZoom, FrameTracking, Icons, IconViewImage, LabelPopup, MenuText,
             NativeClassicCanvas, Notice, RenderedDocument, ScrollTracking, ScrollbarLayout, ToolboxMenuBar, Update,
             ZoomedChildLayout, std::array, std::map, std::pair, std::runtime_error, std::vector, std::wstring

  Functions: finder::classic::WaitingCursor::WaitingCursor, finder::classic::WaitingCursor::~WaitingCursor,
             finder::classic::activate, finder::classic::active_window, finder::classic::artwork,
             finder::classic::attach_menu, finder::classic::choose_label, finder::classic::chrome_art,
             finder::classic::confirm, finder::classic::decorate, finder::classic::desktop_color,
             finder::classic::desktop_pattern, finder::classic::draw_folder_status, finder::classic::find_box,
             finder::classic::finish, finder::classic::shared_graphics, finder::classic::draw_progress,
             finder::classic::initialize, finder::classic::input_box, finder::classic::label_menu_bitmap,
             finder::classic::menu_icon, finder::classic::message_box, finder::classic::notice,
             finder::classic::paint_icon_view, finder::classic::paint_list_scrollbars, finder::classic::paint_window_chrome,
             finder::classic::redraw_menu_bar, finder::classic::replace_icon, finder::classic::scrollbar_window,
             finder::classic::small_font, finder::classic::style_checkbox, finder::classic::style_desktop, finder::classic::style_dialog,
             finder::classic::set_menu_caption, finder::classic::style_list, finder::classic::style_listbox, finder::classic::style_menu,
             finder::classic::track_context_menu,
             finder::classic::style_progress,
             finder::classic::system_font, finder::classic::text_font, finder::classic::translate_menu_key,
             finder::classic::use_graphics, finder::classic::waiting_cursor, finder::classic::window_menu

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Classic presentation for native Win32 windows and controls. These painters
// run on the UI thread, borrow supplied icons and own shared font/brush
// resources. Windows still supplies the control models, focus and message
// delivery.

#include "finder/classic.h"
#include "finder/actions.h"
#include "rsys/icon_color.h"
#include "classic-ui/classic_ui.h"
#include <algorithm>
#include <array>
#include <vector>
#include <commctrl.h>
#include <deque>
#include <map>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <uxtheme.h>
#include <string>
#include <stdexcept>
#include <windowsx.h>
#include "alert_layout.inc"
#include "find_layout.inc"
#include "label_menu.inc"
#include "desktop_pattern.inc"
#include "scrollbar_art.inc"

namespace finder::classic {
// GetIconInfo returns owned bitmaps. Work on their pixels, keeping both the
// original icon and its transparent mask unchanged; no Windows blend pattern
// participates in classic icon selection.
HICON selected_icon(HICON source)
{
    ICONINFO original{};
    if (!source || !GetIconInfo(source, &original))
        return nullptr;

    struct Bitmaps {
        ICONINFO& original;
        HBITMAP transformed = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);

        ~Bitmaps()
        {
            if (original.hbmColor)
                DeleteObject(original.hbmColor);
            if (original.hbmMask)
                DeleteObject(original.hbmMask);
            if (transformed)
                DeleteObject(transformed);
            if (dc)
                DeleteDC(dc);
        }
    } owned{original};

    BITMAP bitmap{};
    if (!owned.dc || !original.hbmColor || !GetObjectW(original.hbmColor, sizeof bitmap, &bitmap) ||
        bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0 || bitmap.bmWidth > 512 || bitmap.bmHeight > 512)
        return CopyIcon(source);
    int width = bitmap.bmWidth, height = bitmap.bmHeight;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    if (GetDIBits(owned.dc, original.hbmColor, 0, height, pixels.data(), &info, DIB_RGB_COLORS) != height)
        return CopyIcon(source);
    bool alpha = false;
    for (size_t i = 3; i < pixels.size(); i += 4)
        alpha |= pixels[i] != 0;
    if (!alpha) {
        struct MaskInfo {
            BITMAPINFOHEADER header;
            RGBQUAD colors[2];
        } mask{};

        mask.header = info.bmiHeader;
        mask.header.biBitCount = 1;
        mask.header.biSizeImage = 0;
        unsigned stride = (width + 31) / 32 * 4;
        std::vector<unsigned char> bits(size_t(stride) * height);
        if (!original.hbmMask || GetDIBits(owned.dc, original.hbmMask, 0, height, bits.data(),
                                           reinterpret_cast<BITMAPINFO*>(&mask), DIB_RGB_COLORS) != height)
            return CopyIcon(source);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                pixels[(size_t(y) * width + x) * 4 + 3] =
                    bits[size_t(y) * stride + x / 8] & (0x80 >> (x & 7)) ? 0 : 255;
    }
    for (size_t i = 0; i < pixels.size(); i += 4) {
        unsigned a = pixels[i + 3];
        if (!a) {
            pixels[i] = pixels[i + 1] = pixels[i + 2] = 0;
            continue;
        }
        uint16_t color[3];
        for (unsigned channel = 0; channel < 3; ++channel)
            color[channel] = uint16_t(std::min(255u, (unsigned(pixels[i + 2 - channel]) * 255 + a / 2) / a) * 257);
        ROMlib_icon_selected_rgb(color);
        for (unsigned channel = 0; channel < 3; ++channel)
            pixels[i + 2 - channel] = ((unsigned(color[channel]) >> 8) * a + 127) / 255;
    }
    void* output = nullptr;
    owned.transformed = CreateDIBSection(owned.dc, &info, DIB_RGB_COLORS, &output, nullptr, 0);
    if (!owned.transformed)
        return CopyIcon(source);
    memcpy(output, pixels.data(), pixels.size());
    ICONINFO selected = original;
    selected.hbmColor = owned.transformed;
    return CreateIconIndirect(&selected);
}

namespace {
thread_local unsigned waiting_depth = 0;

struct RedrawState {
    unsigned depth = 0;
    bool visible = false;
};

// DefWindowProc handles WM_SETREDRAW(FALSE) by removing WS_VISIBLE. That
// changes hit testing and lets clicks reach unrelated applications beneath
// Finder. Suppress paint messages instead, leaving visibility and input alone.
LRESULT CALLBACK batch_paint_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
{
    switch (message) {
    case WM_SETREDRAW: {
        if (!(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD))
            return 0;
        // Native list controls also batch layout internally. Preserve that
        // behavior, but undo DefWindowProc's visibility side effect before
        // returning to the message loop. The top-level window is never hidden.
        auto visible = GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE;
        auto result = DefSubclassProc(window, message, wp, lp);
        auto style = GetWindowLongPtrW(window, GWL_STYLE);
        if ((style & WS_VISIBLE) != visible)
            SetWindowLongPtrW(window, GWL_STYLE, (style & ~LONG_PTR(WS_VISIBLE)) | visible);
        return result;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCPAINT:
    case WM_PRINT:
    case WM_PRINTCLIENT:
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, batch_paint_proc, id);
        break;
    }
    return DefSubclassProc(window, message, wp, lp);
}

thread_local std::map<HWND, RedrawState> redraw_batches;
// A folder resize lays out the list and its sibling gutters separately.
thread_local HWND laying_out_child = nullptr;
}

RedrawBatch::RedrawBatch(HWND window) : root(GetAncestor(window, GA_ROOT))
{
    if (!root)
        return;
    auto& state = redraw_batches[root];
    if (state.depth++ == 0) {
        state.visible = (GetWindowLongPtrW(root, GWL_STYLE) & WS_VISIBLE) != 0;
        SetWindowSubclass(root, batch_paint_proc, 1, 0);
        EnumChildWindows(
            root,
            [](HWND child, LPARAM) -> BOOL {
                SetWindowSubclass(child, batch_paint_proc, 1, 0);
                return TRUE;
            },
            0);
    }
}

RedrawBatch::~RedrawBatch()
{
    auto found = redraw_batches.find(root);
    if (found == redraw_batches.end() || --found->second.depth)
        return;
    bool visible = found->second.visible;
    EnumChildWindows(
        root,
        [](HWND child, LPARAM) -> BOOL {
            RemoveWindowSubclass(child, batch_paint_proc, 1);
            return TRUE;
        },
        0);
    RemoveWindowSubclass(root, batch_paint_proc, 1);
    redraw_batches.erase(found);
    if (visible && IsWindow(root)) {
        RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

bool redraw_suspended(HWND window)
{
    return (laying_out_child && (window == laying_out_child || IsChild(laying_out_child, window))) ||
           redraw_batches.count(GetAncestor(window, GA_ROOT)) != 0;
}

WaitingCursor::WaitingCursor() : previous(SetCursor(LoadCursorW(nullptr, IDC_WAIT)))
{
    ++waiting_depth;
}

WaitingCursor::~WaitingCursor()
{
    --waiting_depth;
    SetCursor(previous);
}

bool waiting_cursor()
{
    if (!waiting_depth)
        return false;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    return true;
}

namespace {
constexpr int edge = 2, caption = ROMAW_CLASSIC_TITLE_HEIGHT - 1, scrollbar_width = ROMAW_CLASSIC_SCROLLBAR_WIDTH;
constexpr COLORREF gray = RGB(221, 221, 221);

constexpr COLORREF native_rgb(unsigned value)
{
    return RGB((value >> 16) & 255, (value >> 8) & 255, value & 255);
}

constexpr COLORREF scroll_light = native_rgb(ROMAW_CLASSIC_SCROLL_TRACK_LIGHT_RGB);
constexpr COLORREF scroll_dark = native_rgb(ROMAW_CLASSIC_SCROLL_TRACK_DARK_RGB);
// Fonts and the brush are owned here; all supplied artwork handles are borrowed.
HFONT geneva, chicago, small;
HANDLE fonts[2]{};
HBRUSH gray_brush;
const ROMAWClassicGraphics* quickdraw_graphics;

struct RenderedDocument {
    std::array<unsigned char, 256> title{};
    ROMAWGraphicsRequest previous{};
    ROMAWGraphicsMetrics metrics{};
    std::vector<uint32_t> pixels;
    bool valid = false;
};

std::map<HWND, RenderedDocument> rendered_documents, rendered_desktops;
bool paint_quickdraw_document(HWND window, HDC dc, int x, int y);
bool paint_quickdraw_desktop(HWND window, HDC dc);

struct ToolboxMenuBar {
    HMENU model = nullptr, popup = nullptr;
    int height = 19, hot = -1, pending = -1;
    bool tracking = false;
    ROMAWGraphicsMetrics metrics{};
    std::vector<std::array<unsigned char, 256>> names;
    std::vector<ROMAWGraphicsTitle> titles;
    std::vector<uint32_t> pixels;
};

std::map<HWND, ToolboxMenuBar> toolbox_menu_bars;
HWND tracking_menu_owner;
void track_toolbox_menu(HWND window, int index);
RECT toolbox_bar_bounds(HWND window);
bool menu_title_bounds(HWND window, int index, RECT* bounds);
HWND front;
std::map<HWND, HWND> decorated_parents;

// Keep the previous frame bounds for live resizing and Zoom transitions.
struct ChildGeometry {
    RECT previous{};
    HWND reset_scroll = nullptr;
};

std::map<HWND, ChildGeometry> child_geometry;
constexpr UINT restore_activation_message = WM_APP + 0x4d0;
bool labels_open = false;
HWND labels_window = nullptr;
HICON stop_icon, warning_icon, application_icon, apple_icon, labels[7]{};
HICON close_art, zoom_art, grow_art, clicked_art;

struct FrameTracking {
    UINT part;
    bool inside;
};

std::map<HWND, FrameTracking> frame_tracking;

struct WindowDrag {
    HWND window = nullptr;
    HWND outline = nullptr;
    POINT press{};
    RECT original{};
    RECT outline_clip{};
    bool growing = false;
    RECT backdrop_bounds{};
    HBITMAP backdrop = nullptr;
    uint32_t* backdrop_pixels = nullptr;
};

WindowDrag window_drag;
constexpr LONG minimum_child_width = 240, minimum_child_height = 120;

// Classic WStateData keeps user and standard rectangles separately. A native
// folder's standard rectangle fits its contents, without setting WS_MAXIMIZE.
struct FolderZoom {
    RECT user, standard;
};

std::map<HWND, FolderZoom> folder_zoom;
constexpr wchar_t zoom_list_property[] = L"FinderZoomList";
// Owned scrollbar images share the Control Manager's source artwork/rotations.
HICON scrollbar_images[15]{};
HBRUSH scrollbar_track;

// Load once on the UI thread; finish releases every image and the track brush.
void load_scrollbar_art()
{
    for (unsigned i = 0; i < 15; ++i)
        if (!scrollbar_images[i])
            scrollbar_images[i] = CreateIconFromResourceEx(const_cast<BYTE*>(scrollbar_resources[i].data),
                                                           scrollbar_resources[i].size, TRUE, 0x00030000, 16, 16, 0);
    if (!scrollbar_track) {
        // Set bits occupy one quarter of the classic ltGray bitmap.
        auto bitmap = CreateBitmap(8, 8, 1, 1, scrollbar_track_pattern);
        if (bitmap) {
            scrollbar_track = CreatePatternBrush(bitmap);
            DeleteObject(bitmap);
        }
    }
}

std::map<unsigned, HICON> menu_art;

struct MenuText {
    std::wstring text;
    bool bar;
    unsigned id;
    bool submenu;
    bool separator;
    unsigned width;
    std::array<unsigned char, 256> name{}, shortcut{};
    std::vector<uint32_t> pixels{};
    uint32_t painted_flags = 0;
    int painted_width = 0, painted_height = 0;
};

// Owner-drawn menu callbacks retain pointers to these records. A deque keeps
// those addresses stable when later menus append their own records.
std::deque<MenuText> menu_text;

// Windows uses one ampersand for a mnemonic and two for a literal ampersand.
// Decode that convention before handing the literal caption to QuickDraw.
std::wstring menu_caption(const std::wstring& text)
{
    std::wstring caption;
    caption.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'&')
            caption.push_back(text[i]);
        else if (i + 1 < text.size() && text[i + 1] == L'&') {
            caption.push_back(L'&');
            ++i;
        }
    }
    return caption;
}

// Convert literal menu captions at the host boundary. AppendMenu's markup is
// never applied to filenames or command captions containing punctuation.
void menu_pascal(const std::wstring& text, std::array<unsigned char, 256>& result)
{
    result.fill(0);
    int count = WideCharToMultiByte(10000, 0, text.data(), std::min(size_t(255), text.size()),
                                    reinterpret_cast<char*>(result.data() + 1), 255, "?", nullptr);
    result[0] = std::max(0, count);
}

ROMAWGraphicsRequest menu_item_request(MenuText& item, int width, int height, uint32_t flags)
{
    auto tab = item.text.find(L'\t');
    auto name = menu_caption(item.text.substr(0, tab));
    menu_pascal(name, item.name);
    menu_pascal(tab == std::wstring::npos ? std::wstring() : item.text.substr(tab + 1), item.shortcut);
    ROMAWGraphicsRequest request{};
    request.size = sizeof request;
    request.kind = ROMAW_GRAPHICS_MENU_ITEM;
    request.key = reinterpret_cast<uintptr_t>(&item);
    request.width = width;
    request.height = height;
    request.title = item.name.data();
    request.shortcut = item.shortcut.data();
    request.icon_width = menu_art.count(item.id) || (item.id > 200 && item.id < 208) ? 20 : 0;
    request.flags =
        flags | (item.submenu ? ROMAW_GRAPHICS_ITEM_SUBMENU : 0) | (item.separator ? ROMAW_GRAPHICS_ITEM_SEPARATOR : 0);
    return request;
}

bool measure_quickdraw_menu_item(MenuText& item, ROMAWGraphicsMetrics& metrics)
{
    if (!quickdraw_graphics || item.bar)
        return false;
    auto request = menu_item_request(item, 128, 32, ROMAW_GRAPHICS_ITEM_ENABLED);
    return !quickdraw_graphics->render(&request, nullptr, 0, &metrics);
}

// StandardMDEF owns row text, colours, separators, checkmarks and submenu
// arrows. Borrowed native application icons retain their established ownership.
bool paint_quickdraw_menu_item(MenuText& item, const DRAWITEMSTRUCT& draw)
{
    if (!quickdraw_graphics || item.bar)
        return false;
    int width = draw.rcItem.right - draw.rcItem.left, height = draw.rcItem.bottom - draw.rcItem.top;
    if (width < 1 || height < 1 || width > 16384 || height > 1024)
        return false;
    uint32_t flags = draw.itemState & (ODS_DISABLED | ODS_GRAYED) ? 0 : ROMAW_GRAPHICS_ITEM_ENABLED;
    if (draw.itemState & ODS_SELECTED)
        flags |= ROMAW_GRAPHICS_ITEM_SELECTED;
    if (draw.itemState & ODS_CHECKED)
        flags |= ROMAW_GRAPHICS_ITEM_CHECKED;
    try {
        if (item.pixels.empty() || item.painted_width != width || item.painted_height != height ||
            item.painted_flags != flags) {
            auto request = menu_item_request(item, width, height, flags);
            item.pixels.resize(size_t(width) * height);
            ROMAWGraphicsMetrics metrics{};
            if (quickdraw_graphics->render(&request, item.pixels.data(), size_t(width) * 4, &metrics)) {
                item.pixels.clear();
                return false;
            }
            item.painted_width = width;
            item.painted_height = height;
            item.painted_flags = flags;
        }
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = width;
        bitmap.bmiHeader.biHeight = -height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        if (!SetDIBitsToDevice(draw.hDC, draw.rcItem.left, draw.rcItem.top, width, height, 0, 0, 0, height,
                               item.pixels.data(), &bitmap, DIB_RGB_COLORS))
            return false;
        HICON icon = nullptr;
        if (auto art = menu_art.find(item.id); art != menu_art.end())
            icon = art->second;
        else if (item.id > 200 && item.id < 208)
            icon = labels[item.id - 201];
        if (icon)
            classic::draw_icon(draw.hDC, draw.rcItem.left + 12, draw.rcItem.top + (height - 16) / 2, icon, 16, 16, 0,
                               nullptr, DI_NORMAL);
        return true;
    }
    catch (...) {
        item.pixels.clear();
        return false;
    }
}

// Fill a rectangle with a temporary brush, releasing it before returning.
void fill(HDC dc, RECT r, COLORREF color)
{
    auto b = CreateSolidBrush(color);
    classic::fill_rect(dc, &r, b);
    DeleteObject(b);
}

// Draw a one-pixel rule while restoring the caller's selected pen.
void line(HDC dc, int x, int y, int end_x, int end_y, COLORREF color)
{
    // Preserve the native helper's exclusive endpoint. Shared classic canvas
    // callbacks add the final pixel explicitly, matching QuickDraw's LineTo.
    if (x == end_x && y == end_y)
        return;
    draw_line(dc, x, y, end_x - (end_x > x) + (end_x < x), end_y - (end_y > y) + (end_y < y), color);
}

// Draw a rectangle outline without retaining a brush or changing DC ownership.
void frame(HDC dc, RECT r, COLORREF color)
{
    auto b = CreateSolidBrush(color);
    classic::frame_rect(dc, &r, b);
    DeleteObject(b);
}

// Express a window's outer bounds in window-local coordinates for nonclient
// painting.
RECT outer(HWND w)
{
    RECT r;
    GetWindowRect(w, &r);
    OffsetRect(&r, -r.left, -r.top);
    return r;
}

// The native adapter resolves abstract colour roles and inclusive line ends;
// shared rendering never receives an HWND, HDC or packed Toolbox structure.
struct NativeClassicCanvas {
    HDC dc;
    bool active;
};

COLORREF classic_color(const NativeClassicCanvas& canvas, ROMAWClassicColor role)
{
    switch (role) {
    case ROMAW_CLASSIC_TITLE_BACKGROUND:
        return canvas.active ? RGB(238, 238, 238) : RGB(255, 255, 255);
    case ROMAW_CLASSIC_FRAME_OUTLINE:
        return canvas.active ? RGB(0, 0, 0) : RGB(85, 85, 85);
    case ROMAW_CLASSIC_TITLE_LIGHT:
        return RGB(204, 204, 255);
    case ROMAW_CLASSIC_TITLE_SHADOW:
        return RGB(153, 153, 204);
    case ROMAW_CLASSIC_TITLE_RULE:
        return RGB(170, 170, 170);
    case ROMAW_CLASSIC_MENU_BACKGROUND:
        return RGB(255, 255, 255);
    default:
        return RGB(0, 0, 0);
    }
}

void classic_fill(void* context, const ROMAWClassicRect* rect, ROMAWClassicColor role)
{
    auto& canvas = *static_cast<NativeClassicCanvas*>(context);
    fill(canvas.dc, {rect->left, rect->top, rect->right, rect->bottom}, classic_color(canvas, role));
}

void classic_line(void* context, int x, int y, int end_x, int end_y, ROMAWClassicColor role)
{
    auto& canvas = *static_cast<NativeClassicCanvas*>(context);
    auto color = classic_color(canvas, role);
    line(canvas.dc, x, y, end_x, end_y, color);
    // GDI excludes the final line pixel; QuickDraw and the shared API include it.
    classic::paint_pixel(canvas.dc, end_x, end_y, color);
}

void classic_frame(void* context, const ROMAWClassicRect* rect, ROMAWClassicColor role)
{
    auto& canvas = *static_cast<NativeClassicCanvas*>(context);
    frame(canvas.dc, {rect->left, rect->top, rect->right, rect->bottom}, classic_color(canvas, role));
}

ROMAWClassicCanvas classic_canvas(NativeClassicCanvas& context)
{
    return {&context, classic_fill, classic_line, classic_frame};
}

// MBDF owns title layout and painting when the Toolbox is attached. The
// detached Windows model continues to supply command state and popup tracking.
void paint_native_menu_bar(HWND window)
{
    if (redraw_suspended(window))
        return;
    auto native = toolbox_menu_bars.find(window);
    if (quickdraw_graphics && native != toolbox_menu_bars.end()) {
        auto& bar = native->second;
        RECT bounds = toolbox_bar_bounds(window);
        int width = bounds.right - bounds.left;
        if (width <= 0)
            return;
        int count = std::min(GetMenuItemCount(bar.model), int(ROMAW_GRAPHICS_MAX_MENUS));
        bar.names.resize(count);
        bar.titles.resize(count);
        for (int i = 0; i < count; ++i) {
            MENUITEMINFOW item{};
            item.cbSize = sizeof item;
            item.fMask = MIIM_DATA | MIIM_STATE;
            GetMenuItemInfoW(bar.model, i, TRUE, &item);
            auto* stored = reinterpret_cast<MenuText*>(item.dwItemData);
            auto text = stored ? menu_caption(stored->text) : std::wstring();
            auto& title = bar.names[i];
            title.fill(0);
            if (text == L"Apple") {
                title[0] = 1;
                title[1] = 20;
            }
            else {
                int bytes = WideCharToMultiByte(10000, 0, text.c_str(), std::min(size_t(255), text.size()),
                                                reinterpret_cast<char*>(title.data() + 1), 255, "?", nullptr);
                title[0] = std::max(0, bytes);
            }
            bar.titles[i] = {title.data(), !(item.fState & (MFS_DISABLED | MFS_GRAYED))};
        }
        ROMAWGraphicsRequest request{};
        request.size = sizeof request;
        request.kind = ROMAW_GRAPHICS_MENU_BAR;
        request.key = reinterpret_cast<uintptr_t>(window);
        request.width = width;
        request.height = bar.height;
        request.menus = bar.titles.data();
        request.menu_count = count;
        request.selected_menu = labels_open ? 4 : bar.hot;
        try {
            bar.pixels.resize(size_t(width) * bar.height);
        }
        catch (...) {
            return;
        }
        if (quickdraw_graphics->render(&request, bar.pixels.data(), size_t(width) * 4, &bar.metrics))
            return;
        HDC dc = GetWindowDC(window);
        if (!dc)
            return;
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = width;
        bitmap.bmiHeader.biHeight = -bar.height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(dc, bounds.left, bounds.top, width, bar.height, 0, 0, 0, bar.height, bar.pixels.data(),
                          &bitmap, DIB_RGB_COLORS);
        ReleaseDC(window, dc);
        return;
    }
    HMENU menu = GetMenu(window);
    MENUBARINFO info{};
    info.cbSize = sizeof info;
    if (!menu || !GetMenuBarInfo(window, OBJID_MENU, 0, &info))
        return;
    HDC dc = GetWindowDC(window);
    if (!dc)
        return;
    int saved = SaveDC(dc);
    if (saved) {
        RECT bounds;
        GetWindowRect(window, &bounds);
        for (int index = 0; index < GetMenuItemCount(menu); ++index) {
            RECT cell;
            if (GetMenuItemRect(window, menu, index, &cell))
                ExcludeClipRect(dc, cell.left - bounds.left, cell.top - bounds.top, cell.right - bounds.left,
                                cell.bottom - bounds.top);
        }
        NativeClassicCanvas context{dc, true};
        auto canvas = classic_canvas(context);
        ROMAWClassicRect bar{info.rcBar.left - bounds.left, info.rcBar.top - bounds.top, info.rcBar.right - bounds.left,
                             info.rcBar.bottom - bounds.top};
        romaw_classic_menu_bar(&canvas, &bar);
        RestoreDC(dc, saved);
    }
    ReleaseDC(window, dc);
}

// Only the visible 13px resource tile is a button; transparent padding retains
// the title stripes and title dragging behavior.
RECT frame_button(HWND window, UINT part)
{
    RECT bounds = outer(window);
    int left = part == HTCLOSE ? 10 : bounds.right - 26;
    return {left, 3, left + 13, 16};
}

bool frame_button_pressed(HWND window, UINT part)
{
    auto tracked = frame_tracking.find(window);
    return tracked != frame_tracking.end() && tracked->second.part == part && tracked->second.inside;
}

bool active_frame(HWND window)
{
    return front == window || (!front && GetActiveWindow() == window);
}

// Paint classic child-window frames, title rules and supplied button artwork;
// the application frame itself remains native Windows chrome.
void draw_chrome(HWND w, HDC dc)
{
    if (quickdraw_graphics) {
        int clip = SaveDC(dc);
        if (clip) {
            RECT client;
            GetClientRect(w, &client);
            MapWindowPoints(w, nullptr, reinterpret_cast<POINT*>(&client), 2);
            RECT bounds;
            GetWindowRect(w, &bounds);
            OffsetRect(&client, -bounds.left, -bounds.top);
            ExcludeClipRect(dc, client.left, client.top, client.right - 1, client.bottom - 1);
            bool painted = paint_quickdraw_document(w, dc, 0, 0);
            RestoreDC(dc, clip);
            if (painted)
                return;
        }
    }
    int saved = SaveDC(dc);
    if (!saved)
        return;
    RECT window, client;
    GetWindowRect(w, &window);
    GetClientRect(w, &client);
    MapWindowPoints(w, nullptr, reinterpret_cast<POINT*>(&client), 2);
    OffsetRect(&client, -window.left, -window.top);
    // A frame repaint must not overwrite child controls or their scrollbars.
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    auto r = outer(w);
    bool active = active_frame(w);
    NativeClassicCanvas context{dc, active};
    auto canvas = classic_canvas(context);
    fill(dc, r, RGB(255, 255, 255));
    ROMAWClassicRect content{1, caption + 1, r.right - 2, r.bottom - 2};
    romaw_classic_document_frame(&canvas, &content, ROMAW_CLASSIC_TITLE_HEIGHT);
    COLORREF background = classic_color(context, ROMAW_CLASSIC_TITLE_BACKGROUND);
    if (active)
        romaw_classic_title_rules(&canvas, content.left, content.top, content.right, ROMAW_CLASSIC_TITLE_HEIGHT);
    wchar_t name[1024];
    GetWindowTextW(w, name, 1024);
    auto old_font = SelectObject(dc, chicago);
    SIZE size;
    GetTextExtentPoint32W(dc, name, wcslen(name), &size);
    int width = std::min(int(r.right) - 64, int(size.cx) + 12);
    RECT label{(r.right - width) / 2, 2, (r.right + width) / 2, caption - 2};
    fill(dc, label, background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? RGB(0, 0, 0) : RGB(136, 136, 136));
    draw_text(dc, name, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (active) {
        // Resource margins belong inside the title, never over its separator.
        int buttons = SaveDC(dc);
        IntersectClipRect(dc, 1, 1, r.right - 2, caption);
        // The 16px resources contain 13px artwork and transparent padding.
        // Keep title rules visible through that padding, without a 3px gap.
        RECT close = frame_button(w, HTCLOSE);
        fill(dc, close, background);
        HICON close_image = frame_button_pressed(w, HTCLOSE) && clicked_art ? clicked_art : close_art;
        if (close_image)
            classic::draw_icon(dc, close.left, close.top, close_image, 16, 16, 0, nullptr, DI_NORMAL);
        if (GetWindowLongPtrW(w, GWL_STYLE) & WS_THICKFRAME) {
            RECT zoom = frame_button(w, HTMAXBUTTON);
            fill(dc, zoom, background);
            HICON zoom_image = frame_button_pressed(w, HTMAXBUTTON) && clicked_art ? clicked_art : zoom_art;
            if (zoom_image)
                classic::draw_icon(dc, zoom.left, zoom.top, zoom_image, 16, 16, 0, nullptr, DI_NORMAL);
        }
        if (buttons)
            RestoreDC(dc, buttons);
    }
    // A native icon can include a background margin; finish the bevel after
    // buttons/text while using the same WDEF operation sequence and palette.
    if (active)
        romaw_classic_title_bevel(&canvas, content.left, content.top, content.right, ROMAW_CLASSIC_TITLE_HEIGHT);
    SelectObject(dc, old_font);
    RestoreDC(dc, saved);
}

// Acquire a window-coordinate DC while keeping all classic artwork nonclient.
void paint_chrome(HWND w)
{
    if (redraw_suspended(w) || !IsWindowVisible(w))
        return;
    HDC dc = GetWindowDC(w);
    if (dc) {
        draw_chrome(w, dc);
        ReleaseDC(w, dc);
    }
}

// Erase tracking before releasing capture: WM_CAPTURECHANGED is synchronous.
void cancel_frame_tracking(HWND window)
{
    frame_tracking.erase(window);
    if (GetCapture() == window)
        ReleaseCapture();
    paint_chrome(window);
}

// Handle shared menu/button measurement and painting, returning whether the
// subclass should consume the message.
bool paint_manager_control(HDC dc, const RECT& bounds, unsigned kind, unsigned flags, const wchar_t* text,
                           int minimum = 0, int maximum = 100, int value = 0, COLORREF background = RGB(255, 255, 255))
{
    try {
        int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        if (!quickdraw_graphics || width < 1 || height < 1 || uint64_t(width) * height > 4 * 1024 * 1024)
            return false;
        std::array<unsigned char, 256> title{};
        menu_pascal(text ? text : L"", title);
        ROMAWGraphicsRequest request{};
        request.size = sizeof request;
        request.kind = kind;
        request.key = 0x46494e444354524cULL;
        request.width = width;
        request.height = height;
        request.flags = flags;
        request.title = title.data();
        request.horizontal = {minimum, maximum, value, 1, -1};
        request.background =
            (uint32_t(GetRValue(background)) << 16) | (uint32_t(GetGValue(background)) << 8) | GetBValue(background);
        ROMAWGraphicsMetrics metrics{};
        std::vector<uint32_t> pixels(size_t(width) * height);
        int error = quickdraw_graphics->render(&request, pixels.data(), size_t(width) * 4, &metrics);
        quickdraw_graphics->release(request.key);
        if (error)
            return false;
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = width;
        bitmap.bmiHeader.biHeight = -height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(dc, bounds.left, bounds.top, width, height, 0, 0, 0, height, pixels.data(), &bitmap,
                          DIB_RGB_COLORS);
        return true;
    }
    catch (...) {
        return false;
    }
}

// Windows retains checkbox state, capture and keyboard semantics. Suppress its
// themed glyph so a 12-pixel classic resource row cannot clip a Windows checkmark.
void paint_checkbox(HWND window, HDC dc)
{
    RECT bounds;
    GetClientRect(window, &bounds);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    fill(dc, bounds, RGB(255, 255, 255));
    auto state = SendMessageW(window, BM_GETSTATE, 0, 0);
    bool enabled = IsWindowEnabled(window);
    uint32_t flags = (enabled ? ROMAW_GRAPHICS_ITEM_ENABLED : 0) |
                     (state & BST_CHECKED ? ROMAW_GRAPHICS_ITEM_CHECKED : 0) |
                     (state & BST_PUSHED ? ROMAW_GRAPHICS_ITEM_SELECTED : 0);
    std::array<uint32_t, 16 * 12> pixels{};
    ROMAWGraphicsRequest request{};
    request.size = sizeof request;
    auto type = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
    request.kind =
        type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON ? ROMAW_GRAPHICS_RADIO : ROMAW_GRAPHICS_CHECKBOX;
    request.key = reinterpret_cast<uintptr_t>(window);
    request.width = 16;
    request.height = 12;
    request.flags = flags;
    ROMAWGraphicsMetrics metrics{};
    int top = std::max(0L, (bounds.bottom - 12) / 2);
    if (quickdraw_graphics && !quickdraw_graphics->render(&request, pixels.data(), 16 * 4, &metrics)) {
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = 16;
        bitmap.bmiHeader.biHeight = -12;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(dc, 0, top, 16, 12, 0, 0, 0, 12, pixels.data(), &bitmap, DIB_RGB_COLORS);
    }
    else {
        // Isolated native tools can use classic controls before Toolbox startup.
        // Keep CDEF 0's exact 12px frame and diagonal X as the bounded fallback.
        COLORREF color = enabled ? RGB(0, 0, 0) : RGB(128, 128, 128);
        RECT box{2, top, 14, top + 12};
        auto brush = CreateSolidBrush(color);
        classic::frame_rect(dc, &box, brush);
        if (state & BST_PUSHED) {
            InflateRect(&box, -1, -1);
            classic::frame_rect(dc, &box, brush);
        }
        DeleteObject(brush);
        if (state & BST_CHECKED) {
            line(dc, 3, top + 1, 12, top + 10, color);
            line(dc, 12, top + 1, 3, top + 10, color);
        }
    }
    wchar_t text[256]{};
    GetWindowTextW(window, text, std::size(text));
    auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    SelectObject(dc, font ? font : geneva);
    SetTextColor(dc, enabled ? RGB(0, 0, 0) : RGB(128, 128, 128));
    SetBkMode(dc, TRANSPARENT);
    bounds.left += 16;
    draw_text(dc, text, -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved);
}

LRESULT CALLBACK checkbox_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        if (redraw_suspended(window))
            return 0;
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wp);
        paint_checkbox(window, dc);
        if (message == WM_PAINT)
            EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return TRUE;
    if (message == WM_NCDESTROY) {
        if (quickdraw_graphics)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(window));
        RemoveWindowSubclass(window, checkbox_proc, id);
    }
    auto result = DefSubclassProc(window, message, wp, lp);
    if (message == BM_SETCHECK || message == BM_SETSTATE || message == WM_ENABLE || message == WM_SETFOCUS ||
        message == WM_KILLFOCUS || message == WM_LBUTTONUP || message == WM_KEYUP)
        InvalidateRect(window, nullptr, FALSE);
    return result;
}

bool owner_draw(UINT msg, LPARAM lp, LRESULT& result)
{
    if (msg == WM_MEASUREITEM) {
        auto* m = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (m->CtlType != ODT_MENU || !m->itemData)
            return false;
        auto& item = *reinterpret_cast<MenuText*>(m->itemData);
        ROMAWGraphicsMetrics metrics{};
        if (measure_quickdraw_menu_item(item, metrics)) {
            m->itemWidth = item.width;
            m->itemHeight = metrics.menu_item_height;
            result = TRUE;
            return true;
        }
        HDC dc = GetDC(nullptr);
        auto old = SelectObject(dc, chicago);
        SIZE size;
        auto measured = item.text.substr(0, item.text.find(L'\t'));
        measured.erase(std::remove(measured.begin(), measured.end(), L'&'), measured.end());
        GetTextExtentPoint32W(dc, measured.c_str(), measured.size(), &size);
        m->itemWidth = item.width;
        if (item.bar) {
            // Reference title cells; Windows adds 14 pixels to each measurement.
            // Explicit advances retain the bitmap Finder's spacing with the TTF.
            static const std::pair<const wchar_t*, unsigned> widths[] = {
                {L"Apple", 26}, {L"File", 22},    {L"Edit", 24}, {L"View", 31},
                {L"Label", 34}, {L"Special", 45}, {L"Help", 28}};
            m->itemWidth = std::max(1L, size.cx - 2);
            for (const auto& title : widths)
                if (measured == title.first)
                    m->itemWidth = title.second;
        }
        m->itemHeight = item.separator ? 8 : 20;
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
        result = TRUE;
        return true;
    }
    if (msg != WM_DRAWITEM)
        return false;
    auto* d = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
    auto r = d->rcItem;
    auto dc = d->hDC;
    if (d->CtlType == ODT_BUTTON) {
        wchar_t caption[256]{};
        GetWindowTextW(d->hwndItem, caption, std::size(caption));
        unsigned flags = d->itemState & (ODS_DISABLED | ODS_GRAYED) ? 0 : ROMAW_GRAPHICS_ITEM_ENABLED;
        if (d->itemState & ODS_SELECTED)
            flags |= ROMAW_GRAPHICS_ITEM_SELECTED;
        if (GetPropW(d->hwndItem, L"FinderDefault"))
            flags |= ROMAW_GRAPHICS_ITEM_DEFAULT;
        if (paint_manager_control(dc, r, ROMAW_GRAPHICS_BUTTON, flags, caption, 0, 100, 0,
                                  GetPropW(GetParent(d->hwndItem), L"FinderWhite") ? RGB(255, 255, 255) : gray)) {
            result = TRUE;
            return true;
        }
        fill(dc, r, GetPropW(GetParent(d->hwndItem), L"FinderWhite") ? RGB(255, 255, 255) : gray);
        auto button_bounds = r;
        if (GetPropW(d->hwndItem, L"FinderDefault"))
            InflateRect(&button_bounds, -4, -4);
        auto brush = CreateSolidBrush(d->itemState & ODS_SELECTED ? RGB(0, 0, 0) : RGB(255, 255, 255));
        auto old_brush = SelectObject(dc, brush);
        auto old_pen = SelectObject(dc, GetStockObject(BLACK_PEN));
        classic::draw_round_rect(dc, button_bounds.left + 1, button_bounds.top + 1, button_bounds.right - 1,
                                 button_bounds.bottom - 1, 8, 8);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(brush);
        wchar_t text[128];
        GetWindowTextW(d->hwndItem, text, 128);
        auto font = GetPropW(d->hwndItem, L"FinderCommonButton")
                        ? reinterpret_cast<HFONT>(SendMessageW(d->hwndItem, WM_GETFONT, 0, 0))
                        : chicago;
        auto old = SelectObject(dc, font ? font : chicago);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, d->itemState & (ODS_DISABLED | ODS_GRAYED) ? RGB(128, 128, 128)
                         : d->itemState & ODS_SELECTED              ? RGB(255, 255, 255)
                                                                    : RGB(0, 0, 0));
        draw_text(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);
        SelectObject(dc, old);
        if (GetPropW(d->hwndItem, L"FinderDefault")) {
            auto pen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
            auto saved_pen = SelectObject(dc, pen);
            auto saved_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            classic::draw_round_rect(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, 10, 10);
            SelectObject(dc, saved_brush);
            SelectObject(dc, saved_pen);
            DeleteObject(pen);
        }
        result = TRUE;
        return true;
    }
    if (d->CtlType != ODT_MENU || !d->itemData)
        return false;
    auto& item = *reinterpret_cast<MenuText*>(d->itemData);
    if (paint_quickdraw_menu_item(item, *d)) {
        result = TRUE;
        return true;
    }
    bool selected = (d->itemState & ODS_SELECTED) || (item.bar && item.text == L"&Label" && labels_open);
    if (item.separator) {
        fill(dc, r, RGB(255, 255, 255));
        line(dc, r.left, (r.top + r.bottom) / 2, r.right, (r.top + r.bottom) / 2, RGB(170, 170, 170));
        result = TRUE;
        return true;
    }
    if (item.bar) {
        NativeClassicCanvas context{dc, true};
        auto canvas = classic_canvas(context);
        ROMAWClassicRect cell{r.left, r.top, r.right, r.bottom};
        romaw_classic_menu_title(&canvas, &cell, selected);
    }
    else
        fill(dc, r, selected ? RGB(0, 0, 0) : RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, d->itemState & (ODS_DISABLED | ODS_GRAYED) ? RGB(128, 128, 128)
                     : selected                                 ? RGB(255, 255, 255)
                                                                : RGB(0, 0, 0));
    auto old = SelectObject(dc, chicago);
    r.left += item.bar ? 4 : (menu_art.count(item.id) || (item.id > 200 && item.id < 208)) ? 32 : 12;
    if (!item.bar)
        r.right -= 8;
    auto tab = item.text.find(L'\t');
    auto text = item.text.substr(0, tab);
    if (item.bar && item.text == L"Apple" && apple_icon) {
        classic::draw_icon(dc, d->rcItem.left + 16, d->rcItem.top + 1, apple_icon, 16, 16, 0, nullptr, DI_NORMAL);
        SelectObject(dc, old);
        result = TRUE;
        return true;
    }
    draw_text(dc, text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);
    if (tab != std::wstring::npos)
        draw_text(dc, item.text.c_str() + tab + 1, -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (auto art = menu_art.find(item.id); art != menu_art.end())
        classic::draw_icon(dc, d->rcItem.left + 12, d->rcItem.top + 1, art->second, 16, 16, 0, nullptr, DI_NORMAL);
    if (item.id > 200 && item.id < 208 && labels[item.id - 201])
        classic::draw_icon(dc, d->rcItem.left + 14, d->rcItem.top + 2, labels[item.id - 201], 16, 16, 0, nullptr,
                           DI_NORMAL);
    if (!item.bar && item.submenu) {
        int x = d->rcItem.right - 7, y = (d->rcItem.top + d->rcItem.bottom) / 2;
        POINT triangle[] = {{x - 3, y - 4}, {x - 3, y + 4}, {x + 2, y}};
        auto brush = CreateSolidBrush(selected ? RGB(255, 255, 255) : RGB(0, 0, 0));
        auto saved = SelectObject(dc, brush);
        auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
        classic::draw_polygon(dc, triangle, 3);
        SelectObject(dc, pen);
        SelectObject(dc, saved);
        DeleteObject(brush);
    }
    if (!item.bar && (d->itemState & ODS_CHECKED)) {
        int x = d->rcItem.left + 3, y = (d->rcItem.top + d->rcItem.bottom) / 2;
        auto color = selected ? RGB(255, 255, 255) : RGB(0, 0, 0);
        line(dc, x, y, x + 2, y + 3, color);
        line(dc, x + 2, y + 3, x + 7, y - 4, color);
    }
    SelectObject(dc, old);
    result = TRUE;
    return true;
}

LRESULT CALLBACK chrome_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

RECT child_bounds(HWND window)
{
    RECT bounds;
    GetWindowRect(window, &bounds);
    MapWindowPoints(nullptr, GetParent(window), reinterpret_cast<POINT*>(&bounds), 2);
    return bounds;
}

// Update the icon origin before painting the fitted viewport.
void reset_icon_scroll(HWND list)
{
    POINT origin{};
    if (IsWindow(list) && ListView_GetOrigin(list, &origin) && (origin.x || origin.y))
        ListView_Scroll(list, -origin.x, -origin.y);
}

bool set_child_bounds(HWND window, const RECT& bounds, HWND reset_scroll = nullptr)
{
    // Use the same old/new damage region as the interactive grow loop.
    child_geometry[window].reset_scroll = reset_scroll;
    bool moved = SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                              bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    if (auto geometry = child_geometry.find(window); geometry != child_geometry.end()) {
        // Identical bounds can skip the geometry update.
        if (moved && geometry->second.reset_scroll)
            reset_icon_scroll(geometry->second.reset_scroll);
        child_geometry.erase(geometry);
    }
    return moved;
}

RECT drag_destination(const WindowDrag& drag, POINT pointer)
{
    if (pointer.x == drag.press.x && pointer.y == drag.press.y)
        return drag.original;

    RECT workspace{};
    GetClientRect(GetParent(drag.window), &workspace);
    RECT bounds = drag.original;
    if (drag.growing) {
        // Keep the upper-left corner and the pointer's grow-box offset fixed.
        if (pointer.x != drag.press.x) {
            LONG min_right = bounds.left + minimum_child_width;
            bounds.right =
                std::clamp(bounds.right + pointer.x - drag.press.x, min_right, std::max(min_right, workspace.right));
        }
        if (pointer.y != drag.press.y) {
            LONG min_bottom = bounds.top + minimum_child_height;
            bounds.bottom = std::clamp(bounds.bottom + pointer.y - drag.press.y, min_bottom,
                                       std::max(min_bottom, workspace.bottom));
        }
        return bounds;
    }
    OffsetRect(&bounds, pointer.x - drag.press.x, pointer.y - drag.press.y);
    // Keep enough title bar reachable when a window exceeds the desktop.
    LONG width = bounds.right - bounds.left;
    LONG left = std::clamp(bounds.left, 32L - width, std::max(32L - width, workspace.right - 32));
    LONG top = std::clamp(bounds.top, 0L, std::max(0L, workspace.bottom - caption));
    OffsetRect(&bounds, left - bounds.left, top - bounds.top);
    return bounds;
}

HBITMAP outline_bitmap(HDC screen, SIZE size, uint32_t** pixels)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = size.cx;
    info.bmiHeader.biHeight = -size.cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    return CreateDIBSection(screen, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(pixels), nullptr, 0);
}

// Take one snapshot before the first outline. A later screen read could copy
// the preview's own pixels and invert them a second time.
bool capture_grow_backdrop(HWND workspace)
{
    RECT bounds{};
    if (!GetClientRect(workspace, &bounds))
        return false;
    MapWindowPoints(workspace, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
    LONG left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    RECT display{left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN), top + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    if (!IntersectRect(&bounds, &bounds, &display))
        return false;
    HDC screen = GetDC(nullptr);
    if (!screen)
        return false;
    HDC surface = CreateCompatibleDC(screen);
    SIZE size{bounds.right - bounds.left, bounds.bottom - bounds.top};
    uint32_t* pixels = nullptr;
    HBITMAP bitmap = surface ? outline_bitmap(screen, size, &pixels) : nullptr;
    HGDIOBJ previous = bitmap ? SelectObject(surface, bitmap) : nullptr;
    bool success = false;
    if (previous && previous != HGDI_ERROR) {
        success = BitBlt(surface, 0, 0, size.cx, size.cy, screen, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);
        GdiFlush();
        SelectObject(surface, previous);
    }
    if (success) {
        window_drag.backdrop = bitmap;
        window_drag.backdrop_bounds = bounds;
        window_drag.backdrop_pixels = pixels;
    }
    else if (bitmap)
        DeleteObject(bitmap);
    if (surface)
        DeleteDC(surface);
    ReleaseDC(nullptr, screen);
    return success;
}

// Composite the WDEF's gray/notPatXor pattern on a separate surface so the
// alternating pixels invert the desktop without writing into its windows.
bool present_grow_outline(HWND outline, const RECT& bounds, const RECT& clip, POINT desktop_origin)
{
    const LONG width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    SIZE visible{clip.right - clip.left, clip.bottom - clip.top};
    HDC surface = CreateCompatibleDC(nullptr);
    uint32_t* pixels = nullptr;
    HBITMAP bitmap = surface ? outline_bitmap(surface, visible, &pixels) : nullptr;
    HGDIOBJ previous = bitmap ? SelectObject(surface, bitmap) : nullptr;
    bool success = false;
    if (previous && previous != HGDI_ERROR) {
        POINT destination{bounds.left + clip.left, bounds.top + clip.top};
        const RECT& backdrop = window_drag.backdrop_bounds;
        LONG stride = backdrop.right - backdrop.left;
        LONG phase = bounds.left - desktop_origin.x + bounds.top - desktop_origin.y;
        for (LONG y = clip.top; y < clip.bottom; ++y) {
            const auto* source = window_drag.backdrop_pixels + size_t(bounds.top + y - backdrop.top) * stride +
                                 destination.x - backdrop.left;
            bool horizontal = y == 0 || y == height - 1 || y == caption || y == height - scrollbar_width;
            for (LONG x = clip.left; x < clip.right; ++x, ++pixels) {
                bool frame = horizontal || x == 0 || x == width - 1;
                bool side = x == width - scrollbar_width && y > caption && y < height - 1;
                // The crossing of the guides is drawn twice by the WDEF.
                bool invert = frame != side && !((x + y + phase) & 1);
                *pixels = invert ? ((source[x - clip.left] ^ 0x00ffffffu) | 0xff000000u) : 0;
            }
        }
        POINT origin{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        success = UpdateLayeredWindow(outline, nullptr, &destination, &visible, surface, &origin, 0, &blend, ULW_ALPHA);
        SelectObject(surface, previous);
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (surface)
        DeleteDC(surface);
    return success;
}

// Give the outline its own composited surface. Moving a child overlay would
// require Finder to repaint the pixels it vacates on every mouse event.
void show_drag_outline(RECT bounds)
{
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    HWND workspace = GetParent(window_drag.window);
    RECT client{};
    GetClientRect(workspace, &client);
    OffsetRect(&client, -bounds.left, -bounds.top);
    RECT limit{0, 0, width, height}, clip{};
    IntersectRect(&clip, &client, &limit);
    MapWindowPoints(workspace, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
    if (window_drag.growing) {
        if (!window_drag.backdrop && !capture_grow_backdrop(workspace))
            return;
        RECT backdrop = window_drag.backdrop_bounds;
        OffsetRect(&backdrop, -bounds.left, -bounds.top);
        if (!IntersectRect(&clip, &clip, &backdrop))
            return;
    }

    HWND outline = window_drag.outline;
    const bool created = !outline;
    if (created) {
        outline = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC",
                                  L"Finder window outline", WS_POPUP | SS_BLACKRECT, bounds.left, bounds.top, width,
                                  height, GetAncestor(workspace, GA_ROOT), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!outline || (!window_drag.growing && !SetLayeredWindowAttributes(outline, 0, 255, LWA_ALPHA))) {
            if (outline)
                DestroyWindow(outline);
            return;
        }
    }
    if (window_drag.growing) {
        POINT desktop_origin{};
        ClientToScreen(workspace, &desktop_origin);
        if (present_grow_outline(outline, bounds, clip, desktop_origin)) {
            window_drag.outline = outline;
            if (!IsWindowVisible(outline))
                ShowWindow(outline, SW_SHOWNOACTIVATE);
        }
        else if (created)
            DestroyWindow(outline);
        else
            ShowWindow(outline, SW_HIDE);
        return;
    }
    if (created || !EqualRect(&clip, &window_drag.outline_clip)) {
        HRGN border = CreateRectRgnIndirect(&clip);
        HRGN interior = CreateRectRgn(2, 2, width - 2, height - 2);
        bool installed = border && interior && CombineRgn(border, border, interior, RGN_DIFF) != ERROR &&
                         SetWindowRgn(outline, border, FALSE);
        if (interior)
            DeleteObject(interior);
        if (!installed) {
            if (border)
                DeleteObject(border);
            if (created)
                DestroyWindow(outline);
            return;
        }
        window_drag.outline = outline;
        window_drag.outline_clip = clip;
    }
    SetWindowPos(outline, HWND_TOP, bounds.left, bounds.top, width, height,
                 SWP_NOACTIVATE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void clear_window_drag()
{
    auto drag = window_drag;
    window_drag = {};
    if (drag.outline)
        DestroyWindow(drag.outline);
    if (drag.backdrop)
        DeleteObject(drag.backdrop);
    if (drag.window && GetCapture() == drag.window)
        ReleaseCapture();
}

// Calculate the standard rectangle from document coordinates, not the visible
// scrolled slice. Include labels, the status strip, gutters and classic frame.
bool fit_folder_contents(HWND window)
{
    HWND list = reinterpret_cast<HWND>(GetPropW(window, zoom_list_property));
    if (!list || !IsWindow(list) || !GetParent(window))
        return false;
    RECT workspace{}, viewport{}, outer = child_bounds(window);
    GetClientRect(GetParent(window), &workspace);
    GetClientRect(list, &viewport);
    int extra_width = outer.right - outer.left - viewport.right;
    int extra_height = outer.bottom - outer.top - viewport.bottom;
    int count = ListView_GetItemCount(list);
    int64_t width = 0, height = 0;
    auto view = ListView_GetView(list);
    if (view == LV_VIEW_DETAILS) {
        // The header defines the full extent even if there are no rows or
        // the first columns are already scrolled out of view.
        HWND header = ListView_GetHeader(list);
        if (header) {
            int columns = Header_GetItemCount(header);
            for (int column = 0; column < columns; ++column)
                width += ListView_GetColumnWidth(list, column);
            RECT bounds{};
            if (GetWindowRect(header, &bounds))
                height = bounds.bottom - bounds.top;
        }
        RECT row{};
        if (count && ListView_GetItemRect(list, 0, &row, LVIR_BOUNDS))
            height += int64_t(row.bottom - row.top) * count;
    }
    else {
        POINT origin{};
        ListView_GetOrigin(list, &origin);
        for (int index = 0; index < count; ++index) {
            RECT item{};
            if (!ListView_GetItemRect(list, index, &item, LVIR_BOUNDS))
                continue;
            if (view == LV_VIEW_LIST) {
                width = std::max<int64_t>(width, item.right - item.left);
                height = std::max<int64_t>(height, item.bottom - item.top);
            }
            else {
                width = std::max(width, int64_t(item.right) + origin.x);
                height = std::max(height, int64_t(item.bottom) + origin.y);
            }
        }
        if (view == LV_VIEW_LIST && count && height) {
            auto rows = std::max<int64_t>(1, (workspace.bottom - extra_height - 8) / height);
            width *= (count + rows - 1) / rows;
            height *= std::min<int64_t>(rows, count);
        }
    }
    int fitted_width = std::min<int64_t>(workspace.right, std::max<int64_t>(240, width + 8 + extra_width));
    int fitted_height = std::min<int64_t>(workspace.bottom, std::max<int64_t>(120, height + 8 + extra_height));
    if (fitted_width <= 0 || fitted_height <= 0)
        return false;
    RECT standard{0, 0, fitted_width, fitted_height};
    HWND icon_list = (view == LV_VIEW_ICON || view == LV_VIEW_SMALLICON) ? list : nullptr;
    if (!set_child_bounds(window, standard, icon_list) || !IsWindow(window))
        return false;
    folder_zoom[window].standard = standard;
    return true;
}

// Toggle the classic user/standard states; resizing or moving the fitted
// window manually makes that new rectangle the next user state.
void zoom_folder(HWND window)
{
    auto found = folder_zoom.find(window);
    if (found != folder_zoom.end()) {
        RECT restore = found->second.user;
        if (set_child_bounds(window, restore))
            folder_zoom.erase(window);
    }
    else {
        RECT user = child_bounds(window);
        if (fit_folder_contents(window))
            folder_zoom[window].user = user;
    }
}

// Refit standard-state folders after a host resize. Legacy maximized children
// still fill the workspace; controls retain their own layout rules.
struct ZoomedChildLayout {
    HWND parent;
    RECT workspace;
};

BOOL CALLBACK fit_zoomed_child(HWND child, LPARAM data)
{
    auto& layout = *reinterpret_cast<ZoomedChildLayout*>(data);
    DWORD_PTR native_frame = 0;
    if (GetParent(child) != layout.parent || !GetWindowSubclass(child, chrome_proc, 1, &native_frame) || native_frame)
        return TRUE;
    if (folder_zoom.count(child)) {
        fit_folder_contents(child);
        return TRUE;
    }
    if (!IsZoomed(child))
        return TRUE;
    RECT current;
    GetWindowRect(child, &current);
    MapWindowPoints(nullptr, layout.parent, reinterpret_cast<POINT*>(&current), 2);
    if (!EqualRect(&current, &layout.workspace))
        SetWindowPos(child, nullptr, 0, 0, layout.workspace.right, layout.workspace.bottom,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
    return TRUE;
}

// Replace child nonclient geometry/hit testing and shared control colors while
// delegating the application's native frame to Windows.
LRESULT CALLBACK chrome_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR native_frame)
{
    if (msg == WM_CREATE)
        decorated_parents[w] = GetParent(w);
    if (msg == restore_activation_message) {
        if (IsWindowVisible(w)) {
            activate(w);
            SetFocus(w);
        }
        return 0;
    }
    if (msg == WM_SETCURSOR && waiting_cursor())
        return TRUE;
    if (((msg == WM_ACTIVATEAPP && !wp) || msg == WM_CANCELMODE) && labels_window && IsWindow(labels_window))
        SendMessageW(labels_window, WM_CANCELMODE, 0, 0);
    LRESULT result;
    if (owner_draw(msg, lp, result))
        return result;
    if (native_frame && msg == WM_NCCALCSIZE && toolbox_menu_bars.count(w)) {
        auto result = DefSubclassProc(w, msg, wp, lp);
        auto* client = wp ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0] : reinterpret_cast<RECT*>(lp);
        client->top = std::min(client->bottom, client->top + toolbox_menu_bars[w].height);
        return result;
    }
    if (native_frame && msg == WM_NCHITTEST && toolbox_menu_bars.count(w)) {
        RECT bar = toolbox_bar_bounds(w), bounds;
        GetWindowRect(w, &bounds);
        OffsetRect(&bar, bounds.left, bounds.top);
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&bar, point))
            return HTMENU;
    }
    if (native_frame && (msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK) && wp == HTMENU &&
        toolbox_menu_bars.count(w)) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        auto& bar = toolbox_menu_bars[w];
        for (int i = 0; i < GetMenuItemCount(bar.model); ++i) {
            RECT cell;
            if (menu_title_bounds(w, i, &cell) && PtInRect(&cell, point)) {
                track_toolbox_menu(w, i);
                break;
            }
        }
        return 0;
    }
    // With no Windows menu attached, DefWindowProc cannot resolve Alt-letter
    // mnemonics. Resolve WM_SYSCHAR against the same MBDF title model as F10.
    if (native_frame && toolbox_menu_bars.count(w) &&
        ((msg == WM_SYSCOMMAND && (wp & 0xfff0) == SC_KEYMENU && lp != ' ') || (msg == WM_SYSCHAR && wp != ' '))) {
        auto& bar = toolbox_menu_bars[w];
        int index = 0;
        int character = msg == WM_SYSCHAR ? int(wp) : int(lp);
        if (character) {
            index = -1;
            for (int i = 0; i < int(bar.names.size()); ++i)
                if (bar.names[i][0] && tolower(bar.names[i][1]) == tolower(character)) {
                    index = i;
                    break;
                }
        }
        if (index >= 0)
            track_toolbox_menu(w, index);
        return 0;
    }
    if (native_frame && msg == WM_MENUSELECT && toolbox_menu_bars.count(w))
        toolbox_menu_bars[w].popup = reinterpret_cast<HMENU>(lp);
    if (native_frame && (msg == WM_NCPAINT || msg == WM_NCACTIVATE)) {
        auto result = DefSubclassProc(w, msg, wp, lp);
        paint_native_menu_bar(w);
        return result;
    }
    if (native_frame && (msg == WM_NCCALCSIZE || msg == WM_NCHITTEST || msg == WM_NCPAINT || msg == WM_NCACTIVATE ||
                         msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK))
        return DefSubclassProc(w, msg, wp, lp);
    if (native_frame && msg == WM_SIZE && wp != SIZE_MINIMIZED) {
        // Lay out the desktop first, then refit standard-state folders and any
        // legacy maximized children without losing their restore rectangles.
        auto result = DefSubclassProc(w, msg, wp, lp);
        ZoomedChildLayout layout{w, {}};
        GetClientRect(w, &layout.workspace);
        EnumChildWindows(w, fit_zoomed_child, reinterpret_cast<LPARAM>(&layout));
        paint_native_menu_bar(w);
        return result;
    }
    if (native_frame && ((msg == WM_ACTIVATEAPP && !wp) || msg == WM_CANCELMODE)) {
        HWND captured = GetCapture();
        if ((frame_tracking.count(captured) || window_drag.window == captured) && IsChild(w, captured))
            SendMessageW(captured, WM_CANCELMODE, 0, 0);
    }
    if (window_drag.window == w) {
        if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP) {
            POINT pointer{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(w, &pointer);
            RECT bounds = drag_destination(window_drag, pointer);
            if (msg == WM_LBUTTONUP) {
                bool moved = !EqualRect(&bounds, &window_drag.original);
                clear_window_drag();
                if (moved && set_child_bounds(w, bounds))
                    SendMessageW(w, WM_EXITSIZEMOVE, 0, 0);
            }
            else if (!EqualRect(&bounds, &window_drag.original) || window_drag.outline)
                show_drag_outline(bounds);
            return 0;
        }
        if (msg == WM_CANCELMODE || msg == WM_CAPTURECHANGED || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) {
            clear_window_drag();
            return 0;
        }
    }
    auto tracked = frame_tracking.find(w);
    if (tracked != frame_tracking.end()) {
        if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(w, &point);
            RECT window;
            GetWindowRect(w, &window);
            point.x -= window.left;
            point.y -= window.top;
            UINT part = tracked->second.part;
            RECT button = frame_button(w, part);
            bool inside = PtInRect(&button, point);
            if (quickdraw_graphics) {
                int hit = quickdraw_graphics->document_hit(reinterpret_cast<uintptr_t>(w), point.x, point.y);
                inside = part == HTCLOSE ? hit == ROMAW_GRAPHICS_CLOSE_HIT
                                         : hit == ROMAW_GRAPHICS_ZOOM_IN || hit == ROMAW_GRAPHICS_ZOOM_OUT;
            }
            if (msg == WM_LBUTTONUP) {
                cancel_frame_tracking(w);
                if (inside) {
                    if (part == HTCLOSE)
                        SendMessageW(w, WM_CLOSE, 0, 0);
                    else
                        zoom_folder(w);
                }
            }
            else if (inside != tracked->second.inside) {
                tracked->second.inside = inside;
                paint_chrome(w);
            }
            return 0;
        }
        if (msg == WM_CANCELMODE || msg == WM_CAPTURECHANGED || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) {
            cancel_frame_tracking(w);
            return 0;
        }
    }
    switch (msg) {
    case WM_ENTERSIZEMOVE:
        if (!native_frame && (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD))
            child_geometry.try_emplace(w);
        break;
    case WM_WINDOWPOSCHANGING:
        if (auto move = child_geometry.find(w); move != child_geometry.end()) {
            auto* position = reinterpret_cast<WINDOWPOS*>(lp);
            constexpr UINT unchanged = SWP_NOMOVE | SWP_NOSIZE;
            if ((position->flags & unchanged) != unchanged && !redraw_suspended(w)) {
                move->second.previous = child_bounds(w);
                // A move can reuse the visible frame and client pixels.
                // Resizing or fitting changes the layout and needs a joint repaint.
                if (!(position->flags & SWP_NOSIZE) || (position->flags & SWP_NOCOPYBITS) || move->second.reset_scroll)
                    position->flags |= SWP_NOCOPYBITS | SWP_NOREDRAW;
            }
        }
        break;
    case WM_WINDOWPOSCHANGED:
        if (auto move = child_geometry.find(w); move != child_geometry.end() && !IsRectEmpty(&move->second.previous)) {
            const auto geometry = move->second;
            move->second = {};
            const auto* position = reinterpret_cast<const WINDOWPOS*>(lp);
            const bool layout_changed = !(position->flags & SWP_NOSIZE) || geometry.reset_scroll;
            const bool repaint_frame = layout_changed || (position->flags & SWP_NOCOPYBITS);
            auto prior_layout = laying_out_child;
            if (layout_changed)
                laying_out_child = w;
            auto result = DefSubclassProc(w, msg, wp, lp);
            if (geometry.reset_scroll)
                reset_icon_scroll(geometry.reset_scroll);
            laying_out_child = prior_layout;
            auto parent = GetParent(w);
            if (parent && IsWindowVisible(w) && !redraw_suspended(w)) {
                const RECT current = child_bounds(w);
                HRGN damage = CreateRectRgnIndirect(&geometry.previous);
                HRGN destination = CreateRectRgnIndirect(&current);
                const UINT paint =
                    RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | (repaint_frame ? RDW_UPDATENOW : 0);
                // Repaint the area uncovered by pure moves. Leave painting
                // queued so it can coalesce while mouse input is arriving.
                int region = damage && destination
                                 ? CombineRgn(damage, damage, destination, repaint_frame ? RGN_OR : RGN_DIFF)
                                 : ERROR;
                if (region != ERROR) {
                    if (region != NULLREGION)
                        RedrawWindow(parent, nullptr, damage, paint);
                }
                else {
                    RedrawWindow(parent, &geometry.previous, nullptr, paint);
                    if (repaint_frame)
                        RedrawWindow(w, nullptr, nullptr, paint);
                }
                if (damage)
                    DeleteObject(damage);
                if (destination)
                    DeleteObject(destination);
            }
            return result;
        }
        break;
    case WM_GETMINMAXINFO:
        if (!native_frame) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
            limits->ptMinTrackSize = {minimum_child_width, minimum_child_height};
            if (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD) {
                RECT workspace{};
                if (GetClientRect(GetParent(w), &workspace)) {
                    limits->ptMaxPosition = {0, 0};
                    limits->ptMaxSize = {workspace.right, workspace.bottom};
                }
            }
            return 0;
        }
        break;
    case WM_NCCALCSIZE: {
        auto* r = wp ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0] : reinterpret_cast<RECT*>(lp);
        // QuickDraw control tiles include the document's far outline pixel.
        // Keep that shared rule in the client extent, with one outside shadow.
        int border = quickdraw_graphics ? 1 : edge;
        r->left += border;
        r->right -= border;
        r->bottom -= border;
        r->top += caption + 1;
        return 0;
    }
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT wr;
        GetWindowRect(w, &wr);
        p.x -= wr.left;
        p.y -= wr.top;
        if (quickdraw_graphics && rendered_documents.count(w)) {
            int hit = quickdraw_graphics->document_hit(reinterpret_cast<uintptr_t>(w), p.x, p.y);
            switch (hit) {
            case ROMAW_GRAPHICS_CONTENT:
                return HTCLIENT;
            case ROMAW_GRAPHICS_DRAG:
                return HTCAPTION;
            case ROMAW_GRAPHICS_GROW:
                return HTBOTTOMRIGHT;
            case ROMAW_GRAPHICS_CLOSE_HIT:
                return HTCLOSE;
            case ROMAW_GRAPHICS_ZOOM_IN:
            case ROMAW_GRAPHICS_ZOOM_OUT:
                return HTMAXBUTTON;
            default:
                return HTBORDER;
            }
        }
        int width = wr.right - wr.left, height = wr.bottom - wr.top;
        if ((GetWindowLongPtrW(w, GWL_STYLE) & WS_THICKFRAME) && !IsZoomed(w)) {
            if (p.x >= width - edge - scrollbar_width && p.y >= height - edge - scrollbar_width)
                return HTBOTTOMRIGHT;
            if (p.x < edge)
                return HTLEFT;
            if (p.x >= width - edge)
                return HTRIGHT;
            if (p.y < edge)
                return HTTOP;
            if (p.y >= height - edge)
                return HTBOTTOM;
        }
        if (p.y < caption) {
            RECT close = frame_button(w, HTCLOSE), zoom = frame_button(w, HTMAXBUTTON);
            if (active_frame(w) && PtInRect(&close, p))
                return HTCLOSE;
            if (active_frame(w) && PtInRect(&zoom, p) && (GetWindowLongPtrW(w, GWL_STYLE) & WS_THICKFRAME))
                return HTMAXBUTTON;
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_NCPAINT:
        // Native frames use thicker metrics and overwrite the child gutters.
        // The native application frame already delegates above this switch.
        paint_chrome(w);
        return 0;
    case WM_NCACTIVATE:
        paint_chrome(w);
        return TRUE;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        activate(w);
        if ((wp == HTCAPTION || wp == HTBOTTOMRIGHT) && (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD)) {
            HWND focus = GetFocus();
            if (focus != w && !IsChild(w, focus))
                SetFocus(w);
            SetCapture(w);
            if (GetCapture() == w) {
                window_drag = {
                    w, nullptr, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, child_bounds(w), {}, wp == HTBOTTOMRIGHT};
                if (window_drag.growing)
                    show_drag_outline(window_drag.original);
            }
            return 0;
        }
        if (wp == HTCLOSE || wp == HTMAXBUTTON) {
            SetCapture(w);
            if (GetCapture() == w) {
                frame_tracking[w] = {UINT(wp), true};
                paint_chrome(w);
            }
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_MAXIMIZE && !native_frame) {
            zoom_folder(w);
            return 0;
        }
        break;
    case WM_EXITSIZEMOVE: {
        child_geometry.erase(w);
        auto zoom = folder_zoom.find(w);
        if (zoom != folder_zoom.end()) {
            RECT current = child_bounds(w);
            if (!EqualRect(&current, &zoom->second.standard))
                folder_zoom.erase(zoom);
        }
        break;
    }
    case WM_MOUSEACTIVATE:
        if (!native_frame)
            activate(w);
        break;
    case WM_PARENTNOTIFY:
        if (LOWORD(wp) == WM_CREATE)
            style_text_control(reinterpret_cast<HWND>(lp));
        break;
    case WM_CTLCOLORSTATIC:
        if (GetPropW(reinterpret_cast<HWND>(lp), L"FinderWhite") || GetPropW(w, L"FinderWhite")) {
            SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        SetBkColor(reinterpret_cast<HDC>(wp), gray);
        return reinterpret_cast<LRESULT>(gray_brush);
    case WM_ERASEBKGND: {
        if (redraw_suspended(w))
            return 1;
        RECT r;
        GetClientRect(w, &r);
        classic::fill_rect(reinterpret_cast<HDC>(wp), &r,
                           GetPropW(w, L"FinderWhite") ? reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))
                                                       : gray_brush);
        return 1;
    }
    case WM_NCDESTROY:
        if (quickdraw_graphics)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(w));
        rendered_documents.erase(w);
        auto bar = toolbox_menu_bars.find(w);
        if (bar != toolbox_menu_bars.end()) {
            DestroyMenu(bar->second.model);
            toolbox_menu_bars.erase(bar);
        }
        frame_tracking.erase(w);
        if (window_drag.window == w)
            clear_window_drag();
        child_geometry.erase(w);
        folder_zoom.erase(w);
        if (front == w) {
            front = nullptr;
            // SelectWindow keeps the next classic window active after a
            // close. Native children need the same handoff; otherwise every
            // surviving title/control is left painted as inactive.
            // The dying window can already be unlinked from Z order during
            // WM_NCDESTROY. Enumerate its parent's remaining children instead.
            HWND parent = decorated_parents.count(w) ? decorated_parents[w] : GetParent(w);
            for (HWND next = GetWindow(parent, GW_CHILD); next; next = GetWindow(next, GW_HWNDNEXT)) {
                DWORD_PTR native = 0;
                if (next != w && (GetWindowLongPtrW(next, GWL_STYLE) & WS_VISIBLE) &&
                    GetWindowSubclass(next, chrome_proc, 1, &native) && !native) {
                    activate(next);
                    // Windows finishes restoring focus after WM_NCDESTROY.
                    // Deliver the classic activation step after that teardown,
                    // rather than let its old content control reclaim the front.
                    PostMessageW(next, restore_activation_message, 0, 0);
                    break;
                }
            }
        }
        decorated_parents.erase(w);
        RemoveWindowSubclass(w, chrome_proc, 1);
        break;
    }
    return DefSubclassProc(w, msg, wp, lp);
}

// Paint the classic progress frame and bounded fill from the native progress
// control's range and position.
LRESULT CALLBACK progress_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        int minimum = SendMessageW(w, PBM_GETRANGE, TRUE, 0), maximum = SendMessageW(w, PBM_GETRANGE, FALSE, 0);
        int value = SendMessageW(w, PBM_GETPOS, 0, 0);
        if (paint_manager_control(dc, r, ROMAW_GRAPHICS_PROGRESS, ROMAW_GRAPHICS_ITEM_ENABLED, nullptr, minimum,
                                  maximum, value)) {
            EndPaint(w, &ps);
            return 0;
        }
        fill(dc, r, RGB(255, 255, 255));
        frame(dc, r, RGB(0, 0, 0));
        InflateRect(&r, -2, -2);
        int pos = SendMessageW(w, PBM_GETPOS, 0, 0);
        r.right = r.left + int((r.right - r.left) * std::clamp(double(pos) / std::max(1, maximum), 0.0, 1.0));
        int height = std::max(1L, r.bottom - r.top);
        for (int y = r.top; y < r.bottom; ++y) {
            unsigned shade = 80 + (y - r.top) * 60 / height;
            line(dc, r.left, y, r.right, y, RGB(shade, shade, shade));
        }
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(w, progress_proc, 2);
    return DefSubclassProc(w, msg, wp, lp);
}

// Geometry is in window coordinates; the list view still owns scroll ranges.
// Keep the content control's actual window equal to its visible viewport.
// Its native range calculations assume standard nonclient metrics, so the
// fixed classic gutters live in a separate, non-overlapping sibling window.
std::map<HWND, HWND> scrollbar_hosts;

HWND scrollbar_surface(HWND list)
{
    auto found = scrollbar_hosts.find(list);
    return found == scrollbar_hosts.end() || !IsWindow(found->second) ? list : found->second;
}

void paint_scrollbars(HWND list);

LRESULT CALLBACK scrollbar_host_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    HWND list = reinterpret_cast<HWND>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        list = static_cast<HWND>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(list));
    }
    if (message == WM_NCCALCSIZE) {
        auto* client = wp ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0] : reinterpret_cast<RECT*>(lp);
        client->right = std::max(client->left, client->right - scrollbar_width);
        client->bottom = std::max(client->top, client->bottom - scrollbar_width);
        return 0;
    }
    if (message == WM_SIZE) {
        RECT bounds;
        GetWindowRect(window, &bounds);
        int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        HRGN outer = CreateRectRgn(0, 0, width, height);
        HRGN content = CreateRectRgn(0, 0, std::max(0, width - scrollbar_width), std::max(0, height - scrollbar_width));
        if (outer && content && CombineRgn(outer, outer, content, RGN_DIFF) != ERROR) {
            if (SetWindowRgn(window, outer, !redraw_suspended(window)))
                outer = nullptr; // Windows owns the installed region.
        }
        if (outer)
            DeleteObject(outer);
        if (content)
            DeleteObject(content);
    }
    if (list && (message == WM_NCHITTEST || message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK ||
                 message == WM_NCMOUSEMOVE || message == WM_SETCURSOR))
        return SendMessageW(list, message, wp, lp);
    if (message == WM_NCPAINT) {
        if (list)
            paint_scrollbars(list);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

// Classic thumbs have a fixed length and traverse the whole available track.
struct ScrollbarLayout {
    RECT area{}, first{}, last{}, thumb{};
    SCROLLINFO info{};
    bool enabled = false;
};

static bool scroll_listbox(HWND window)
{
    return GetPropW(window, L"FinderClassicListBox") != nullptr;
}

ScrollbarLayout scrollbar_layout(HWND w, bool vertical)
{
    RECT window, client;
    GetWindowRect(scrollbar_surface(w), &window);
    GetClientRect(w, &client);
    MapWindowPoints(w, nullptr, reinterpret_cast<POINT*>(&client), 2);
    OffsetRect(&client, -window.left, -window.top);
    ScrollbarLayout bar;
    bar.area =
        vertical
            ? RECT{client.right, client.top, window.right - window.left, client.bottom + (quickdraw_graphics ? 1 : 0)}
            : RECT{client.left, client.bottom, client.right + (quickdraw_graphics ? 1 : 0), window.bottom - window.top};
    bar.info.cbSize = sizeof bar.info;
    bar.info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    bool present = (GetWindowLongPtrW(w, GWL_STYLE) & (vertical ? WS_VSCROLL : WS_HSCROLL)) != 0;
    if (!GetScrollInfo(w, vertical ? SB_VERT : SB_HORZ, &bar.info))
        present = false;
    if (vertical && scroll_listbox(w)) {
        int count = int(SendMessageW(w, LB_GETCOUNT, 0, 0));
        int row = int(SendMessageW(w, LB_GETITEMHEIGHT, 0, 0));
        bar.info.nMin = 0;
        bar.info.nMax = std::max(0, count - 1);
        bar.info.nPage = row > 0 ? std::max(1L, (client.bottom - client.top) / row) : 1;
        bar.info.nPos = std::max<LRESULT>(0, SendMessageW(w, LB_GETTOPINDEX, 0, 0));
        present = count > 0;
    }
    auto maximum =
        std::max<long long>(bar.info.nMin, static_cast<long long>(bar.info.nMax) - std::max(1u, bar.info.nPage) + 1);
    bar.enabled = present && maximum > bar.info.nMin;
    bar.first = bar.last = bar.thumb = bar.area;
    int size = scrollbar_width;
    int start = vertical ? bar.area.top : bar.area.left;
    int finish = vertical ? bar.area.bottom : bar.area.right;
    size = std::min(size, std::max(0, (finish - start) / 2));
    int length = std::min(16, std::max(0, finish - start - size * 2));
    int travel = std::max(0, finish - start - size * 2 - length);
    int offset = bar.enabled
                     ? int((static_cast<long long>(bar.info.nPos) - bar.info.nMin) * travel / (maximum - bar.info.nMin))
                     : 0;
    if (vertical) {
        bar.first.bottom = start + size;
        bar.last.top = finish - size;
        bar.thumb.top = start + size + offset;
        bar.thumb.bottom = bar.thumb.top + length;
    }
    else {
        bar.first.right = start + size;
        bar.last.left = finish - size;
        bar.thumb.left = start + size + offset;
        bar.thumb.right = bar.thumb.left + length;
    }
    return bar;
}

struct ScrollTracking {
    bool vertical = false, thumb = false;
    int offset = 0;
    UINT action = 0;
    POINT pointer{};
};

std::map<HWND, ScrollTracking> scroll_tracking;
constexpr UINT_PTR scroll_repeat_timer = 0x464e4453;

// Captured mouse events are client-relative; scrollbar artwork is nonclient.
POINT
scroll_pointer(HWND w, POINT client)
{
    RECT window;
    GetWindowRect(scrollbar_surface(w), &window);
    ClientToScreen(w, &client);
    client.x -= window.left;
    client.y -= window.top;
    return client;
}

// Convert the list view's row/column scroll units to LVM_SCROLL pixel units.
void scroll_to(HWND w, bool vertical, int position)
{
    if (scroll_listbox(w)) {
        SendMessageW(w, LB_SETTOPINDEX, position, 0);
        return;
    }
    auto bar = scrollbar_layout(w, vertical);
    long long delta = static_cast<long long>(position) - bar.info.nPos;
    auto view = ListView_GetView(w);
    if ((vertical && view == LV_VIEW_DETAILS) || (!vertical && view == LV_VIEW_LIST)) {
        RECT item;
        if (ListView_GetItemRect(w, 0, &item, LVIR_BOUNDS))
            delta *= vertical ? item.bottom - item.top : item.right - item.left;
    }
    int pixels = int(std::clamp<long long>(delta, INT_MIN, INT_MAX));
    ListView_Scroll(w, vertical ? 0 : pixels, vertical ? pixels : 0);
}

// Stop at the pointer while repeating page clicks, as on a classic scrollbar.
void repeat_scroll(HWND w, const ScrollTracking& tracking)
{
    auto bar = scrollbar_layout(w, tracking.vertical);
    if (!bar.enabled)
        return;
    bool before = tracking.vertical ? tracking.pointer.y < bar.thumb.top : tracking.pointer.x < bar.thumb.left;
    bool after = tracking.vertical ? tracking.pointer.y >= bar.thumb.bottom : tracking.pointer.x >= bar.thumb.right;
    bool act = tracking.action == SB_LINEUP     ? PtInRect(&bar.first, tracking.pointer)
               : tracking.action == SB_LINEDOWN ? PtInRect(&bar.last, tracking.pointer)
               : tracking.action == SB_PAGEUP   ? before
                                                : after;
    if (act && scroll_listbox(w)) {
        int page = bar.info.nPage;
        int delta = tracking.action == SB_LINEUP     ? -1
                    : tracking.action == SB_LINEDOWN ? 1
                    : tracking.action == SB_PAGEUP   ? -page : page;
        scroll_to(w, true, std::clamp(bar.info.nPos + delta, 0, std::max(0, bar.info.nMax - page + 1)));
    }
    else if (act)
        SendMessageW(w, tracking.vertical ? WM_VSCROLL : WM_HSCROLL, tracking.action, 0);
}

// Tear down capture before a control is destroyed or a gesture is cancelled.
void stop_scroll_tracking(HWND w)
{
    KillTimer(w, scroll_repeat_timer);
    scroll_tracking.erase(w);
    if (GetCapture() == w)
        ReleaseCapture();
}

// Present complete WDEF/CDEF output. The native list view is a data/input
// model here; it no longer supplies scrollbar pixels or frame geometry.
bool paint_quickdraw_document(HWND window, HDC dc, int x, int y)
{
    if (!quickdraw_graphics)
        return false;
    try {
        RECT bounds = outer(window);
        auto& cached = rendered_documents[window];
        std::array<unsigned char, 256> title{};
        wchar_t name[1024];
        int length = GetWindowTextW(window, name, std::size(name));
        int bytes = WideCharToMultiByte(10000, 0, name, std::min(length, 255),
                                        reinterpret_cast<char*>(title.data() + 1), 255, "?", nullptr);
        title[0] = std::max(0, bytes);
        bool title_changed = title != cached.title;
        cached.title = title;
        ROMAWGraphicsRequest request{};
        request.size = sizeof request;
        request.kind = ROMAW_GRAPHICS_DOCUMENT;
        request.key = reinterpret_cast<uintptr_t>(window);
        request.width = bounds.right;
        request.height = bounds.bottom;
        request.title = cached.title.data();
        request.flags = (active_frame(window) ? ROMAW_GRAPHICS_ACTIVE : 0) |
                        (GetWindowLongPtrW(window, GWL_STYLE) & WS_SYSMENU ? ROMAW_GRAPHICS_CLOSE : 0) |
                        (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME ? ROMAW_GRAPHICS_ZOOM : 0) |
                        (frame_button_pressed(window, HTCLOSE) ? ROMAW_GRAPHICS_CLOSE_PRESSED : 0) |
                        (frame_button_pressed(window, HTMAXBUTTON) ? ROMAW_GRAPHICS_ZOOM_PRESSED : 0);
        request.vertical.pressed = request.horizontal.pressed = -1;
        HWND list = reinterpret_cast<HWND>(GetPropW(window, zoom_list_property));
        if (list) {
            request.flags |= ROMAW_GRAPHICS_SCROLLBARS;
            request.status_height = 18;
            for (bool vertical : {true, false}) {
                auto bar = scrollbar_layout(list, vertical);
                auto& scroll = vertical ? request.vertical : request.horizontal;
                int extent = vertical ? bar.area.bottom - bar.area.top : bar.area.right - bar.area.left;
                int travel = std::max(0, extent - 3 * scrollbar_width);
                long long maximum = std::max<long long>(bar.info.nMin, static_cast<long long>(bar.info.nMax) -
                                                                           std::max(1u, bar.info.nPage) + 1);
                scroll.minimum = 0;
                scroll.maximum = travel;
                scroll.enabled = active_frame(window) && bar.enabled && travel > 0;
                if (maximum > bar.info.nMin)
                    scroll.value = int((static_cast<long long>(bar.info.nPos) - bar.info.nMin) * travel /
                                       (maximum - bar.info.nMin));
                auto tracking = scroll_tracking.find(list);
                if (tracking != scroll_tracking.end() && !tracking->second.thumb &&
                    tracking->second.vertical == vertical) {
                    if (tracking->second.action == SB_LINEUP && PtInRect(&bar.first, tracking->second.pointer))
                        scroll.pressed = 0;
                    if (tracking->second.action == SB_LINEDOWN && PtInRect(&bar.last, tracking->second.pointer))
                        scroll.pressed = 1;
                }
            }
        }
        if (!cached.valid || title_changed || std::memcmp(&cached.previous, &request, sizeof request)) {
            if (bounds.right < 1 || bounds.bottom < 1 || bounds.right > 16384 || bounds.bottom > 16384 ||
                uint64_t(bounds.right) * bounds.bottom > 64 * 1024 * 1024)
                return false;
            cached.valid = false;
            cached.pixels.resize(size_t(bounds.right) * bounds.bottom);
            if (quickdraw_graphics->render(&request, cached.pixels.data(), size_t(bounds.right) * 4, &cached.metrics))
                return false;
            cached.previous = request;
            cached.valid = true;
        }
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = bounds.right;
        bitmap.bmiHeader.biHeight = -bounds.bottom;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        return SetDIBitsToDevice(dc, x, y, bounds.right, bounds.bottom, 0, 0, 0, bounds.bottom, cached.pixels.data(),
                                 &bitmap, DIB_RGB_COLORS) != 0;
    }
    catch (...) {
        return false;
    }
}

// Paint unscaled classic sprites in nonclient gutters, using list-view ranges
// and fixed-size thumb geometry shared with the pointer tracker.
void draw_scrollbars(HWND w, HDC dc)
{
    int saved = SaveDC(dc);
    if (!saved)
        return;
    RECT window;
    GetWindowRect(scrollbar_surface(w), &window);
    RECT client;
    GetClientRect(w, &client);
    MapWindowPoints(w, nullptr, reinterpret_cast<POINT*>(&client), 2);
    OffsetRect(&client, -window.left, -window.top);
    // The list view owns every client pixel, including its optimized scroll
    // copies. Restrict all chrome drawing to the surrounding nonclient gutters.
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    const bool listbox = scroll_listbox(w);
    if (quickdraw_graphics && !listbox) {
        HWND parent = GetParent(w);
        RECT bounds;
        GetWindowRect(parent, &bounds);
        if (paint_quickdraw_document(parent, dc, bounds.left - window.left, bounds.top - window.top)) {
            RestoreDC(dc, saved);
            return;
        }
    }
    for (bool vertical : {true, false}) {
        if (listbox && !vertical)
            continue;
        auto bar = scrollbar_layout(w, vertical);
        fill(dc, bar.area, scroll_light);
        if (bar.enabled && scrollbar_track) {
            // GDI maps zero bits to the text color (the light field),
            // and set bits to the background color (the dark dots).
            SetTextColor(dc, scroll_light);
            SetBkColor(dc, scroll_dark);
            classic::fill_rect(dc, &bar.area, scrollbar_track);
        }
        auto draw = [&](RECT box, unsigned image) {
            fill(dc, box, gray);
            if (scrollbar_images[image])
                classic::draw_icon(dc, box.left + (box.right - box.left - 16) / 2,
                                   box.top + (box.bottom - box.top - 16) / 2, scrollbar_images[image], 16, 16, 0,
                                   nullptr, DI_NORMAL);
        };
        auto tracking = scroll_tracking.find(w);
        for (unsigned end = 0; end < 2; ++end) {
            RECT button = end ? bar.last : bar.first;
            bool pressed = tracking != scroll_tracking.end() && !tracking->second.thumb &&
                           tracking->second.vertical == vertical &&
                           tracking->second.action == (end ? SB_LINEDOWN : SB_LINEUP) &&
                           PtInRect(&button, tracking->second.pointer);
            auto state = romaw_classic_arrow_state(bar.enabled, pressed);
            unsigned direction = vertical ? end : 2 + end;
            unsigned base = state == ROMAW_CLASSIC_ARROW_INACTIVE ? 4 : state == ROMAW_CLASSIC_ARROW_CLICKED ? 11 : 0;
            draw(button, base + direction);
        }
        if (bar.enabled)
            draw(bar.thumb, vertical ? 9 : 8);
        frame(dc, bar.area, RGB(0, 0, 0));
        // The containing document already supplies its outline and shadow.
        // Leave a light inner bevel beside it, rather than stacking a second
        // black control edge against that frame (including the arrow sprites).
        RECT bevel = bar.area;
        if (vertical)
            bevel.left = bevel.right - 1;
        else
            bevel.top = bevel.bottom - 1;
        fill(dc, bevel, RGB(238, 238, 238));
    }
    if (listbox) {
        frame(dc, RECT{0, 0, window.right - window.left, window.bottom - window.top}, RGB(0, 0, 0));
        RestoreDC(dc, saved);
        return;
    }
    // The grow resource contains a 14px tile and transparent padding. Place
    // that tile inside the top/left rules; the document owns the far edges.
    RECT grow{client.right, client.bottom, window.right - window.left, window.bottom - window.top};
    fill(dc, grow, gray);
    auto grow_image = grow_art ? grow_art : scrollbar_images[10];
    if (grow_image)
        classic::draw_icon(dc, grow.left + 1, grow.top + 1, grow_image, 16, 16, 0, nullptr, DI_NORMAL);
    RECT grow_top{grow.left, grow.top, grow.right, grow.top + 1};
    RECT grow_left{grow.left, grow.top, grow.left + 1, grow.bottom};
    fill(dc, grow_top, RGB(0, 0, 0));
    fill(dc, grow_left, RGB(0, 0, 0));
    RECT grow_right{grow.right - 1, grow.top + 1, grow.right, grow.bottom};
    RECT grow_bottom{grow.left + 1, grow.bottom - 1, grow.right, grow.bottom};
    fill(dc, grow_right, RGB(238, 238, 238));
    fill(dc, grow_bottom, RGB(238, 238, 238));
    RestoreDC(dc, saved);
}

// Acquire a window-coordinate DC; the painter preserves its clipping state.
void paint_scrollbars(HWND w)
{
    if (redraw_suspended(w) || !IsWindowVisible(w))
        return;
    HDC dc = GetWindowDC(scrollbar_surface(w));
    if (dc) {
        draw_scrollbars(w, dc);
        ReleaseDC(scrollbar_surface(w), dc);
    }
}

// The desktop pattern is a System PixPat, painted by QuickDraw as well. A
// whole-surface cache keeps partial erasure anchored through translated DCs.
bool paint_quickdraw_desktop(HWND window, HDC dc)
{
    if (!quickdraw_graphics)
        return false;
    RECT bounds;
    GetClientRect(window, &bounds);
    if (bounds.right <= 0 || bounds.bottom <= 0 || bounds.right > 16384 || bounds.bottom > 16384 ||
        uint64_t(bounds.right) * bounds.bottom > 64 * 1024 * 1024)
        return false;
    try {
        auto& cached = rendered_desktops[window];
        ROMAWGraphicsRequest request{};
        request.size = sizeof request;
        request.kind = ROMAW_GRAPHICS_DESKTOP;
        request.key = reinterpret_cast<uintptr_t>(window);
        request.width = bounds.right;
        request.height = bounds.bottom;
        if (!cached.valid || request.width != cached.previous.width || request.height != cached.previous.height) {
            cached.pixels.resize(size_t(bounds.right) * bounds.bottom);
            if (quickdraw_graphics->render(&request, cached.pixels.data(), size_t(bounds.right) * 4, &cached.metrics))
                return false;
            cached.previous = request;
            cached.valid = true;
        }
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = bounds.right;
        bitmap.bmiHeader.biHeight = -bounds.bottom;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        return SetDIBitsToDevice(dc, 0, 0, bounds.right, bounds.bottom, 0, 0, 0, bounds.bottom, cached.pixels.data(),
                                 &bitmap, DIB_RGB_COLORS) != 0;
    }
    catch (...) {
        return false;
    }
}

// A list-view background image can restart its tile at the exposed update
// rectangle during window moves. Paint the System PixPat in desktop coordinates
// instead, including when a buffered DC has a translated viewport.
LRESULT CALLBACK desktop_style_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    if (msg == WM_SETCURSOR && waiting_cursor())
        return TRUE;
    if (msg == WM_ERASEBKGND) {
        if (redraw_suspended(w))
            return 1;
        HDC dc = reinterpret_cast<HDC>(wp);
        int saved = SaveDC(dc);
        if (!saved)
            return 0;
        if (paint_quickdraw_desktop(w, dc)) {
            RestoreDC(dc, saved);
            return 1;
        }
        POINT origin{};
        LPtoDP(dc, &origin, 1);
        SetBrushOrgEx(dc, origin.x, origin.y, nullptr);
        RECT bounds;
        GetClientRect(w, &bounds);
        classic::fill_rect(dc, &bounds, reinterpret_cast<HBRUSH>(data));
        RestoreDC(dc, saved);
        return 1;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(w, desktop_style_proc, id);
        rendered_desktops.erase(w);
        if (quickdraw_graphics)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(w));
        DeleteObject(reinterpret_cast<HBRUSH>(data));
    }
    return DefSubclassProc(w, msg, wp, lp);
}

// Keep list-view ranges synchronized with classic nonclient painting and
// fixed-thumb capture, repeat and cancellation.
LRESULT CALLBACK list_style_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_SETCURSOR && waiting_cursor())
        return TRUE;
    if (msg == WM_WINDOWPOSCHANGING) {
        auto* position = reinterpret_cast<WINDOWPOS*>(lp);
        HWND host = scrollbar_surface(w);
        if (host != w && (!(position->flags & SWP_NOSIZE) || !(position->flags & SWP_NOMOVE))) {
            if (redraw_suspended(w))
                position->flags |= SWP_NOREDRAW | SWP_NOCOPYBITS;
            SetWindowPos(host, nullptr, position->x, position->y, position->cx, position->cy,
                         (position->flags & (SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOCOPYBITS)) | SWP_NOZORDER |
                             SWP_NOACTIVATE);
            if (!(position->flags & SWP_NOSIZE)) {
                position->cx = std::max(0, position->cx - scrollbar_width);
                position->cy = std::max(0, position->cy - scrollbar_width);
            }
        }
    }
    // The model owns only content; the sibling owns both nonclient gutters.
    if (msg == WM_NCCALCSIZE && !scroll_listbox(w))
        return 0;
    if (msg == WM_NCPAINT) {
        paint_scrollbars(w);
        return 0;
    }
    if ((msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK) && (wp == HTVSCROLL || wp == HTHSCROLL)) {
        if (quickdraw_graphics && !active_frame(GetParent(w))) {
            activate(GetParent(w));
            paint_scrollbars(w);
            return 0;
        }
        RECT window;
        GetWindowRect(scrollbar_surface(w), &window);
        POINT point{GET_X_LPARAM(lp) - window.left, GET_Y_LPARAM(lp) - window.top};
        bool vertical = wp == HTVSCROLL;
        auto bar = scrollbar_layout(w, vertical);
        if (!bar.enabled)
            return 0;
        ScrollTracking tracking;
        tracking.vertical = vertical;
        tracking.pointer = point;
        tracking.thumb = PtInRect(&bar.thumb, point);
        tracking.offset = vertical ? point.y - bar.thumb.top : point.x - bar.thumb.left;
        tracking.action = PtInRect(&bar.first, point)                                       ? SB_LINEUP
                          : PtInRect(&bar.last, point)                                      ? SB_LINEDOWN
                          : (vertical ? point.y < bar.thumb.top : point.x < bar.thumb.left) ? SB_PAGEUP
                                                                                            : SB_PAGEDOWN;
        SetCapture(w);
        scroll_tracking[w] = tracking;
        if (!tracking.thumb) {
            repeat_scroll(w, tracking);
            SetTimer(w, scroll_repeat_timer, 150, nullptr);
        }
        paint_scrollbars(w);
        return 0;
    }
    auto found = scroll_tracking.find(w);
    if (found != scroll_tracking.end()) {
        if (msg == WM_MOUSEMOVE || (msg == WM_LBUTTONUP && found->second.thumb)) {
            auto tracking = found->second;
            tracking.pointer = scroll_pointer(w, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            found->second.pointer = tracking.pointer;
            if (tracking.thumb) {
                auto bar = scrollbar_layout(w, tracking.vertical);
                int start = tracking.vertical ? bar.first.bottom : bar.first.right;
                int finish = tracking.vertical ? bar.last.top : bar.last.left;
                int travel = std::max(0, finish - start - 16);
                int point = tracking.vertical ? tracking.pointer.y : tracking.pointer.x;
                int offset = std::clamp(point - tracking.offset - start, 0, travel);
                auto maximum = std::max<long long>(bar.info.nMin, static_cast<long long>(bar.info.nMax) -
                                                                      std::max(1u, bar.info.nPage) + 1);
                int position = bar.info.nMin;
                if (travel)
                    position += int((maximum - bar.info.nMin) * offset / travel);
                scroll_to(w, tracking.vertical, position);
            }
            if (msg == WM_LBUTTONUP)
                stop_scroll_tracking(w);
            paint_scrollbars(w);
            return 0;
        }
        if (msg == WM_TIMER && wp == scroll_repeat_timer) {
            repeat_scroll(w, found->second);
            paint_scrollbars(w);
            return 0;
        }
        if (msg == WM_LBUTTONUP || msg == WM_CANCELMODE || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) {
            stop_scroll_tracking(w);
            paint_scrollbars(w);
            return 0;
        }
        if (msg == WM_CAPTURECHANGED || msg == WM_NCDESTROY) {
            KillTimer(w, scroll_repeat_timer);
            scroll_tracking.erase(w);
        }
    }
    auto result = DefSubclassProc(w, msg, wp, lp);
    if (msg == WM_SIZE && !redraw_suspended(w) && IsWindowVisible(w))
        // Discard native resize bit copies before repainting the resized contents.
        RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
    if (msg == WM_NCHITTEST) {
        RECT window;
        GetWindowRect(scrollbar_surface(w), &window);
        POINT point{GET_X_LPARAM(lp) - window.left, GET_Y_LPARAM(lp) - window.top};
        for (bool vertical : {true, false}) {
            if (!vertical && scroll_listbox(w))
                break;
            auto bar = scrollbar_layout(w, vertical);
            if (PtInRect(&bar.area, point))
                return vertical ? HTVSCROLL : HTHSCROLL;
        }
        if (scroll_listbox(w))
            return result;
        // The list owns the grow tile's pixels, but only its decorated parent
        // can resize the folder. Native hit testing returns HTNOWHERE here
        // when the list has no scroll ranges, leaving a painted but dead grip.
        auto vertical = scrollbar_layout(w, true), horizontal = scrollbar_layout(w, false);
        RECT grow{vertical.area.left, horizontal.area.top, window.right - window.left, window.bottom - window.top};
        HWND parent = GetParent(w);
        DWORD_PTR native_frame = 0;
        if (PtInRect(&grow, point) && (GetWindowLongPtrW(parent, GWL_STYLE) & WS_THICKFRAME) && !IsZoomed(parent) &&
            GetWindowSubclass(parent, chrome_proc, 1, &native_frame) && !native_frame)
            return HTTRANSPARENT;
    }
    if (msg == WM_NCMOUSEMOVE || msg == WM_PAINT || msg == WM_SIZE || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL ||
        msg == WM_HSCROLL || msg == WM_VSCROLL || msg == LVM_SCROLL || msg == LVM_ENSUREVISIBLE ||
        msg == WM_WINDOWPOSCHANGED || msg == LB_SETTOPINDEX || msg == LB_RESETCONTENT || msg == LB_ADDSTRING || msg == WM_ENABLE || msg == WM_CAPTURECHANGED)
        paint_scrollbars(w);
    if (msg == WM_NCDESTROY) {
        auto host = scrollbar_hosts.find(w);
        if (host != scrollbar_hosts.end()) {
            DestroyWindow(host->second);
            scrollbar_hosts.erase(host);
        }
        HWND parent = GetParent(w);
        if (reinterpret_cast<HWND>(GetPropW(parent, zoom_list_property)) == w)
            RemovePropW(parent, zoom_list_property);
        RemovePropW(w, L"FinderClassicListBox");
        RemoveWindowSubclass(w, list_style_proc, 3);
    }
    return result;
}
}

namespace {
// Positions are returned by MBDF and mapped from window to screen coordinates.
RECT toolbox_bar_bounds(HWND window)
{
    RECT client{}, bounds{};
    GetClientRect(window, &client);
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&client), 2);
    GetWindowRect(window, &bounds);
    OffsetRect(&client, -bounds.left, -bounds.top);
    int height = toolbox_menu_bars[window].height;
    return {client.left, client.top - height, client.right, client.top};
}

bool menu_title_bounds(HWND window, int index, RECT* bounds)
{
    auto found = toolbox_menu_bars.find(window);
    if (found == toolbox_menu_bars.end())
        return GetMenuItemRect(window, GetMenu(window), index, bounds);
    auto& bar = found->second;
    if (index < 0 || index >= int(bar.titles.size()))
        return false;
    auto cell = bar.metrics.menu_titles[index];
    RECT origin = toolbox_bar_bounds(window), window_bounds;
    GetWindowRect(window, &window_bounds);
    *bounds = {cell.left + origin.left + window_bounds.left, origin.top + window_bounds.top,
               cell.right + origin.left + window_bounds.left, origin.bottom + window_bounds.top};
    return true;
}

// Keep Windows' established popup tracking and submenu/keyboard handling, but
// switch top-level menus using MBDF cells rather than native bar hit testing.
LRESULT CALLBACK toolbox_menu_filter(int code, WPARAM wp, LPARAM lp)
{
    if (code == MSGF_MENU && tracking_menu_owner && toolbox_menu_bars.count(tracking_menu_owner)) {
        auto& bar = toolbox_menu_bars.find(tracking_menu_owner)->second;
        auto* message = reinterpret_cast<MSG*>(lp);
        int next = -1;
        if (message->message == WM_MOUSEMOVE) {
            POINT point;
            GetCursorPos(&point);
            for (int i = 0; i < int(bar.titles.size()); ++i) {
                RECT cell;
                if (menu_title_bounds(tracking_menu_owner, i, &cell) && PtInRect(&cell, point)) {
                    next = i;
                    break;
                }
            }
        }
        if (message->message == WM_KEYDOWN && (message->wParam == VK_LEFT || message->wParam == VK_RIGHT) &&
            bar.popup == GetSubMenu(bar.model, bar.hot)) {
            bool child = false;
            for (int i = 0; i < GetMenuItemCount(bar.popup); ++i)
                if ((GetMenuState(bar.popup, i, MF_BYPOSITION) & MF_HILITE) && GetSubMenu(bar.popup, i))
                    child = true;
            if (message->wParam == VK_LEFT || !child)
                next =
                    (bar.hot + (message->wParam == VK_LEFT ? -1 : 1) + int(bar.titles.size())) % int(bar.titles.size());
        }
        if (next >= 0 && next != bar.hot && bar.titles[next].enabled) {
            bar.pending = next;
            EndMenu();
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void track_toolbox_menu(HWND window, int index)
{
    auto& bar = toolbox_menu_bars[window];
    if (bar.tracking || index < 0 || index >= int(bar.titles.size()))
        return;
    // Selection can change while the bar is idle. Refresh command state before
    // deciding whether a previously disabled title (especially Label) opens.
    SendMessageW(window, WM_INITMENU, reinterpret_cast<WPARAM>(bar.model), 0);
    paint_native_menu_bar(window);
    if (!bar.titles[index].enabled)
        return;
    bar.tracking = true;
    HWND previous_owner = tracking_menu_owner;
    tracking_menu_owner = window;
    HHOOK hook = SetWindowsHookExW(WH_MSGFILTER, toolbox_menu_filter, nullptr, GetCurrentThreadId());
    while (index >= 0 && IsWindow(window)) {
        SendMessageW(window, WM_INITMENU, reinterpret_cast<WPARAM>(bar.model), 0);
        bar.hot = index;
        bar.pending = -1;
        paint_native_menu_bar(window);
        RECT anchor;
        if (!menu_title_bounds(window, index, &anchor))
            break;
        HMENU popup = GetSubMenu(bar.model, index);
        if (!popup) {
            MENUITEMINFOW item{};
            item.cbSize = sizeof item;
            item.fMask = MIIM_ID;
            if (GetMenuItemInfoW(bar.model, index, TRUE, &item))
                PostMessageW(window, WM_COMMAND, item.wID, 0);
            break;
        }
        bar.popup = popup;
        unsigned command = TrackPopupMenuEx(popup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, anchor.left,
                                            anchor.bottom - 1, window, nullptr);
        if (command)
            PostMessageW(window, WM_COMMAND, command, 0);
        if (!IsWindow(window) || !toolbox_menu_bars.count(window))
            break;
        index = bar.pending;
    }
    if (hook)
        UnhookWindowsHookEx(hook);
    tracking_menu_owner = previous_owner;
    auto live = toolbox_menu_bars.find(window);
    if (live != toolbox_menu_bars.end()) {
        live->second.tracking = false;
        live->second.hot = -1;
        paint_native_menu_bar(window);
    }
}
}

void attach_menu(HWND window, HMENU menu)
{
    if (!quickdraw_graphics) {
        SetMenu(window, menu);
        return;
    }
    auto& bar = toolbox_menu_bars[window];
    if (bar.model && bar.model != menu)
        DestroyMenu(bar.model);
    bar.model = menu;
    // Query the real MBDF height without attaching an OS-sized menu bar.
    ROMAWGraphicsRequest query{};
    query.size = sizeof query;
    query.kind = ROMAW_GRAPHICS_MENU_BAR;
    query.key = reinterpret_cast<uintptr_t>(window);
    query.width = 64;
    query.height = 64;
    query.selected_menu = -1;
    ROMAWGraphicsMetrics metrics{};
    if (!quickdraw_graphics->render(&query, nullptr, 0, &metrics))
        bar.height = metrics.menu_height;
    SetMenu(window, nullptr);
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    paint_native_menu_bar(window);
}

HMENU window_menu(HWND window)
{
    auto found = toolbox_menu_bars.find(window);
    return found == toolbox_menu_bars.end() ? GetMenu(window) : found->second.model;
}

void redraw_menu_bar(HWND window)
{
    if (toolbox_menu_bars.count(window))
        paint_native_menu_bar(window);
    else
        DrawMenuBar(window);
}

// Child controls consume system characters when no Win32 HMENU is attached.
// Route Alt-letter input to the owning custom bar before ordinary dispatch.
bool translate_menu_key(const MSG& message)
{
    if (window_drag.window && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
        clear_window_drag();
        return true;
    }
    if (message.message != WM_SYSCHAR || message.wParam == ' ')
        return false;
    HWND owner = GetAncestor(message.hwnd, GA_ROOT);
    if (!toolbox_menu_bars.count(owner))
        return false;
    SendMessageW(owner, message.message, message.wParam, message.lParam);
    return true;
}

// Install the embedded fonts and create shared GDI objects before decorating
// any Finder windows.
void initialize(const void* a, size_t na, const void* b, size_t nb)
{
    DWORD count;
    fonts[0] = AddFontMemResourceEx(const_cast<void*>(a), DWORD(na), nullptr, &count);
    fonts[1] = AddFontMemResourceEx(const_cast<void*>(b), DWORD(nb), nullptr, &count);
    geneva = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Geneva");
    chicago = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Chicago");
    small = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Geneva");
    gray_brush = CreateSolidBrush(gray);
}

// Release shared fonts/brushes and menu bookkeeping after the decorated
// windows have been destroyed.
void finish()
{
    finish_text();
    if (quickdraw_graphics)
        for (const auto& item : menu_text)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(&item));
    if (quickdraw_graphics)
        for (const auto& item : rendered_documents)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(item.first));
    if (quickdraw_graphics)
        for (const auto& item : rendered_desktops)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(item.first));
    rendered_documents.clear();
    rendered_desktops.clear();
    decorated_parents.clear();
    child_geometry.clear();
    quickdraw_graphics = nullptr;
    DeleteObject(geneva);
    DeleteObject(small);
    DeleteObject(chicago);
    DeleteObject(gray_brush);
    for (auto& image : scrollbar_images) {
        if (image)
            DestroyIcon(image);
        image = nullptr;
    }
    if (scrollbar_track)
        DeleteObject(scrollbar_track);
    scrollbar_track = nullptr;
    for (auto f : fonts)
        if (f)
            RemoveFontMemResourceEx(f);
    menu_text.clear();
    menu_art.clear();
}

// The initialized runtime owns this interface and every guest-side object.
const ROMAWClassicGraphics* shared_graphics()
{
    return quickdraw_graphics;
}

void use_graphics(const ROMAWClassicGraphics* graphics)
{
    finish_text();
    quickdraw_graphics =
        graphics && graphics->size == sizeof *graphics && graphics->version == ROMAW_GRAPHICS_ABI ? graphics : nullptr;
}

// Borrow the shared small Geneva font used for icon labels and compact dialog
// fields.
HFONT
small_font()
{
    return small;
}

// Borrow the shared Geneva text font; callers must not delete it.
HFONT
text_font()
{
    return geneva;
}

// The source SICN includes transparent padding around its 7x9 padlock.
// Keep the count's ordinary baseline and reserve one 16 px cell when locked.
void draw_folder_status(HDC dc, const RECT& bounds, const wchar_t* text, HICON lock)
{
    int saved = SaveDC(dc);
    if (!saved)
        return;
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    classic::fill_rect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SelectObject(dc, small_font());
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    RECT count = bounds;
    count.top += 3;
    if (lock) {
        classic::draw_icon(dc, bounds.left - 2, bounds.top, lock, 16, 16, 0, nullptr, DI_NORMAL);
        count.left += 16;
    }
    draw_text(dc, text, -1, &count, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved);
}

// Borrow the shared Chicago system font; callers must not delete it.
HFONT
system_font()
{
    return chicago;
}

// Return the tracked live classic window, or null if it has already been
// destroyed.
HWND active_window()
{
    return front;
}

// Raise the selected child and repaint title bars to keep classic activation
// distinct from the host's top-level activation.
void activate(HWND w)
{
    auto old = front;
    front = w;
    if (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD)
        SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (old && IsWindow(old))
        RedrawWindow(old, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
    RedrawWindow(w, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

// About and copy windows use the same clipped QuickDraw progress geometry.
void draw_progress(HDC dc, const RECT& bounds, uint64_t value, uint64_t maximum)
{
    int units = maximum ? int(std::min<long double>(1.0L, static_cast<long double>(value) / maximum) * 10000) : 0;
    if (paint_manager_control(dc, bounds, ROMAW_GRAPHICS_PROGRESS, ROMAW_GRAPHICS_ITEM_ENABLED, nullptr, 0, 10000,
                              units))
        return;
    frame(dc, bounds, RGB(0, 0, 0));
    RECT inside = bounds;
    InflateRect(&inside, -2, -2);
    inside.right = inside.left + (inside.right - inside.left) * units / 10000;
    fill(dc, inside, RGB(115, 115, 115));
}

// Apply presentation without changing BS_AUTOCHECKBOX, BM_GETCHECK or input.
void style_checkbox(HWND window)
{
    SetWindowTheme(window, L"", L"");
    SetWindowSubclass(window, checkbox_proc, 1, 0);
    InvalidateRect(window, nullptr, TRUE);
}

// Install classic child chrome or shared owner drawing on the native
// application frame; leave caption drag/resize tracking to Windows.
void decorate(HWND w, bool native_frame)
{
    decorated_parents[w] = GetParent(w);
    if (native_frame) {
        SetWindowSubclass(w, chrome_proc, 1, 1);
        return;
    }
    SetWindowTheme(w, L"", L"");
    auto library = LoadLibraryW(L"dwmapi.dll");
    if (library) {
        using Setter = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto p = GetProcAddress(library, "DwmSetWindowAttribute");
        Setter set = nullptr;
        static_assert(sizeof p == sizeof set);
        memcpy(&set, &p, sizeof set);
        if (set) {
            int disabled = 1, corner = 1;
            set(w, 2, &disabled, sizeof disabled);
            set(w, 33, &corner, sizeof corner);
        }
        FreeLibrary(library);
    }
    SetWindowSubclass(w, chrome_proc, 1, 0);
}

namespace {
// Each record lives through its menu's last measure/draw callback.
void style_menu_items(HMENU menu, bool bar, std::deque<MenuText>& records)
{
    unsigned popup_width = 1;
    HDC dc = GetDC(nullptr);
    auto old_font = SelectObject(dc, chicago);
    for (int i = 0; !bar && i < GetMenuItemCount(menu); ++i) {
        wchar_t text[512]{};
        GetMenuStringW(menu, i, text, 512, MF_BYPOSITION);
        if (quickdraw_graphics) {
            MenuText item{text,
                          false,
                          GetMenuItemID(menu, i),
                          GetSubMenu(menu, i) != nullptr,
                          bool(GetMenuState(menu, i, MF_BYPOSITION) & MF_SEPARATOR),
                          0};
            ROMAWGraphicsMetrics metrics{};
            bool measured = measure_quickdraw_menu_item(item, metrics);
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(&item));
            if (measured) {
                popup_width = std::max(popup_width, unsigned(metrics.menu_item_width));
                continue;
            }
        }
        std::wstring label = text;
        auto tab = label.find(L'\t');
        SIZE name{}, shortcut{};
        GetTextExtentPoint32W(dc, label.c_str(), tab == std::wstring::npos ? label.size() : tab, &name);
        if (tab != std::wstring::npos)
            GetTextExtentPoint32W(dc, label.c_str() + tab + 1, label.size() - tab - 1, &shortcut);
        // Windows supplies its nonclient menu gutters in addition to this width.
        popup_width = std::max(popup_width, unsigned(std::max(1L, name.cx + shortcut.cx + (shortcut.cx ? 12 : 8))));
    }
    SelectObject(dc, old_font);
    ReleaseDC(nullptr, dc);
    if (!bar) {
        MENUINFO info{};
        info.cbSize = sizeof info;
        info.fMask = MIM_STYLE;
        info.dwStyle = MNS_NOCHECK;
        SetMenuInfo(menu, &info);
    }
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t text[512]{};
        MENUITEMINFOW m{};
        m.cbSize = sizeof m;
        m.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU | MIIM_ID;
        m.dwTypeData = text;
        m.cch = 512;
        if (!GetMenuItemInfoW(menu, i, TRUE, &m))
            continue;
        records.push_back({text, bar, m.wID, m.hSubMenu != nullptr, bool(m.fType & MFT_SEPARATOR), popup_width});
        m.fMask = MIIM_FTYPE | MIIM_DATA;
        m.fType |= MFT_OWNERDRAW;
        m.dwItemData = reinterpret_cast<ULONG_PTR>(&records.back());
        SetMenuItemInfoW(menu, i, TRUE, &m);
        if (m.hSubMenu)
            style_menu_items(m.hSubMenu, false, records);
    }
}
}

void style_menu(HMENU menu)
{
    style_menu_items(menu, true, menu_text);
}

void set_menu_caption(HMENU menu, unsigned command, const wchar_t* caption)
{
    MENUITEMINFOW item{};
    item.cbSize = sizeof item;
    item.fMask = MIIM_DATA;
    if (!GetMenuItemInfoW(menu, command, FALSE, &item) || !item.dwItemData)
        return;
    auto& text = *reinterpret_cast<MenuText*>(item.dwItemData);
    if (text.text != caption) {
        text.text = caption;
        item.fMask = MIIM_STRING;
        item.dwTypeData = text.text.data();
        SetMenuItemInfoW(menu, command, FALSE, &item);
        text.pixels.clear();
        if (quickdraw_graphics)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(&text));
    }
}

unsigned track_context_menu(HWND owner, HMENU menu, POINT anchor)
{
    std::deque<MenuText> records;
    style_menu_items(menu, false, records);
    auto command =
        TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, anchor.x, anchor.y, owner, nullptr);
    if (quickdraw_graphics)
        for (const auto& item : records)
            quickdraw_graphics->release(reinterpret_cast<uintptr_t>(&item));
    return command;
}

namespace {
struct LabelPopup {
    int checked, hot = -1, result = -1;
    HWND list = nullptr;
    POINT mouse{};
    bool done = false, opening_press = false;
};

// Paint the reference-sized Label popup border and shadow from generated
// layout constants.
void label_frame(HDC dc)
{
    fill(dc, {0, 0, label_canvas_width, label_canvas_height}, RGB(255, 255, 255));
    fill(dc, {label_frame_width, 2, label_frame_width + 1, label_canvas_height}, RGB(0, 0, 0));
    fill(dc, {3, label_frame_height, label_canvas_width, label_canvas_height}, RGB(0, 0, 0));
    RECT bounds{0, 0, label_frame_width, label_frame_height};
    fill(dc, bounds, RGB(255, 255, 255));
    frame(dc, bounds, RGB(0, 0, 0));
}

// Clip each label row before drawing its full-size icon swatch, text and
// checkmark.
void label_row(const DRAWITEMSTRUCT& draw, const LabelPopup& popup)
{
    if (draw.itemID > 7)
        return;
    int saved = SaveDC(draw.hDC);
    RECT row = draw.rcItem;
    IntersectClipRect(draw.hDC, row.left, row.top, row.right, row.bottom);
    fill(draw.hDC, row, RGB(255, 255, 255));
    RECT text = row;
    text.bottom = text.top + label_item_height;
    bool hot = popup.hot == int(draw.itemID);
    if (hot)
        fill(draw.hDC, text, RGB(0, 0, 0));
    auto old_font = SelectObject(draw.hDC, chicago);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, hot ? RGB(255, 255, 255) : RGB(0, 0, 0));
    int x = draw.itemID ? label_text_x : label_none_text_x;
    RECT text_bounds{row.left + x, row.top, row.right, row.bottom};
    draw_text(draw.hDC, label_menu_names[draw.itemID], -1, &text_bounds, DT_SINGLELINE | DT_NOPREFIX);
    if (draw.itemID)
        classic::draw_icon(draw.hDC, row.left + label_sprite_x, row.top + label_sprite_y, labels[draw.itemID - 1], 32,
                           32, 0, nullptr, DI_NORMAL);
    else
        line(draw.hDC, row.left, row.top + label_separator_y, row.right, row.top + label_separator_y,
             RGB(label_separator_gray, label_separator_gray, label_separator_gray));
    if (popup.checked == int(draw.itemID)) {
        auto color = hot ? RGB(255, 255, 255) : RGB(0, 0, 0);
        for (unsigned y = 0; y < std::size(label_check_bitmap); ++y)
            for (unsigned x = 0; x < 9; ++x)
                if (label_check_bitmap[y] & (1u << (8 - x)))
                    classic::paint_pixel(draw.hDC, row.left + label_check_x + x, row.top + label_check_y + y, color);
    }
    SelectObject(draw.hDC, old_font);
    RestoreDC(draw.hDC, saved);
}

// Map popup-client coordinates to a selectable row, rejecting border,
// separator and shadow areas.
int label_hit(POINT point)
{
    if (point.x < 1 || point.x >= label_frame_width - 1 || point.y < 1 || point.y >= label_frame_height - 1)
        return -1;
    int y = point.y - 1;
    if (y < label_header_height)
        return y < label_item_height ? 0 : -1;
    int item = 1 + (y - label_header_height) / label_item_height;
    return item <= 7 ? item : -1;
}

// Update hover state and invalidate only the rows whose highlight changes.
void label_hot(LabelPopup& popup, int item)
{
    if (popup.hot == item)
        return;
    popup.hot = item;
    SendMessageW(popup.list, LB_SETCURSEL, item, 0);
    InvalidateRect(popup.list, nullptr, FALSE);
}

// Manage mouse/keyboard selection and cancel when capture leaves the custom
// Label popup.
LRESULT CALLBACK label_popup_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    auto* popup = reinterpret_cast<LabelPopup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        popup = static_cast<LabelPopup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
    }
    if (!popup)
        return DefWindowProcW(window, message, wp, lp);
    auto close = [&] {
        if (!popup->done) {
            popup->done = true;
            DestroyWindow(window);
        }
    };
    auto switch_menu = [&](POINT point) {
        HWND owner = GetWindow(window, GW_OWNER);
        ClientToScreen(window, &point);
        HMENU bar = window_menu(owner);
        for (int i = 0; i < GetMenuItemCount(bar); ++i) {
            RECT bounds;
            if (i != 4 && menu_title_bounds(owner, i, &bounds) && PtInRect(&bounds, point)) {
                close();
                PostMessageW(owner, WM_NCLBUTTONDOWN, HTMENU, MAKELPARAM(point.x, point.y));
                return true;
            }
        }
        return false;
    };
    if (message == WM_CREATE) {
        popup->list = CreateWindowW(L"LISTBOX", L"Label",
                                    WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS |
                                        LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT,
                                    1, 1, label_frame_width - 2, label_frame_height - 2, window,
                                    reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(popup->list, WM_SETFONT, reinterpret_cast<WPARAM>(chicago), FALSE);
        for (const auto* name : label_menu_names)
            SendMessageW(popup->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        return 0;
    }
    if (message == WM_MEASUREITEM) {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        measure->itemHeight = measure->itemID == 0 ? label_header_height : label_item_height;
        measure->itemWidth = label_frame_width - 2;
        return TRUE;
    }
    if (message == WM_DRAWITEM) {
        label_row(*reinterpret_cast<DRAWITEMSTRUCT*>(lp), *popup);
        return TRUE;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        auto dc = BeginPaint(window, &paint);
        label_frame(dc);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (message == WM_MOUSEMOVE) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (point.x == popup->mouse.x && point.y == popup->mouse.y)
            return 0;
        popup->mouse = point;
        if (switch_menu(point))
            return 0;
        label_hot(*popup, label_hit(point));
        return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (message == WM_LBUTTONUP && popup->opening_press) {
            popup->opening_press = false;
            POINT screen = point;
            ClientToScreen(window, &screen);
            RECT title;
            if (menu_title_bounds(GetWindow(window, GW_OWNER), 4, &title) && PtInRect(&title, screen))
                return 0;
        }
        if (message == WM_LBUTTONDOWN && switch_menu(point))
            return 0;
        int item = label_hit(point);
        if (message == WM_LBUTTONUP && item >= 0)
            popup->result = item;
        if (message == WM_LBUTTONUP || item < 0)
            close();
        return 0;
    }
    if (message == WM_CHAR) {
        wchar_t key = wchar_t(wp);
        for (int offset = 1; offset <= 8; ++offset) {
            int item = (std::max(-1, popup->hot) + offset) % 8;
            if (CompareStringOrdinal(label_menu_names[item], 1, &key, 1, TRUE) == CSTR_EQUAL) {
                label_hot(*popup, item);
                break;
            }
        }
        return 0;
    }
    if (message == WM_VKEYTOITEM || message == WM_KEYDOWN) {
        UINT key = message == WM_VKEYTOITEM ? LOWORD(wp) : UINT(wp);
        int item = popup->hot < 0 ? std::max(0, popup->checked) : popup->hot;
        if (key == VK_ESCAPE || key == VK_MENU)
            close();
        else if (key == VK_RETURN || key == VK_SPACE) {
            popup->result = item;
            close();
        }
        else if (key == VK_UP)
            label_hot(*popup, (item + 7) % 8);
        else if (key == VK_DOWN)
            label_hot(*popup, (item + 1) % 8);
        else if (key == VK_HOME)
            label_hot(*popup, 0);
        else if (key == VK_END)
            label_hot(*popup, 7);
        return message == WM_VKEYTOITEM ? -2 : 0;
    }
    if ((message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) || message == WM_CANCELMODE || message == WM_CLOSE) {
        close();
        return 0;
    }
    if (message == WM_CAPTURECHANGED && !popup->done && IsWindow(window)) {
        close();
        return 0;
    }
    if (message == WM_NCDESTROY)
        popup->done = true;
    return DefWindowProcW(window, message, wp, lp);
}
}

// Run the Label popup's message pump while preserving focus and WM_QUIT;
// return -1 on cancellation.
int choose_label(HWND owner, int checked)
{
    RECT anchor;
    if (!menu_title_bounds(owner, 4, &anchor))
        return -1;
    WNDCLASSW type{};
    type.lpszClassName = L"FinderLabelMenu";
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = label_popup_proc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    LabelPopup popup{checked};
    // A custom bar opens on mouse-down. Releasing that opening click on its
    // title should leave the popup open, just like TrackPopupMenu does.
    popup.opening_press = GetKeyState(VK_LBUTTON) < 0;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = std::clamp(int(anchor.left), int(monitor.rcWork.left),
                       std::max(int(monitor.rcWork.left), int(monitor.rcWork.right) - label_canvas_width));
    int y =
        anchor.bottom + label_canvas_height <= monitor.rcWork.bottom ? anchor.bottom : anchor.top - label_canvas_height;
    HWND focus = GetFocus();
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, type.lpszClassName, L"Label",
                                  WS_POPUP | WS_CLIPCHILDREN, x, y, label_canvas_width, label_canvas_height, owner,
                                  nullptr, type.hInstance, &popup);
    if (!window)
        return -1;
    HRGN region = CreateRectRgn(0, 0, label_frame_width, label_frame_height);
    HRGN right = CreateRectRgn(label_frame_width, 2, label_canvas_width, label_canvas_height);
    HRGN bottom = CreateRectRgn(3, label_frame_height, label_canvas_width, label_canvas_height);
    CombineRgn(region, region, right, RGN_OR);
    CombineRgn(region, region, bottom, RGN_OR);
    DeleteObject(right);
    DeleteObject(bottom);
    if (!SetWindowRgn(window, region, TRUE))
        DeleteObject(region);
    labels_open = true;
    labels_window = window;
    redraw_menu_bar(owner);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    GetCursorPos(&popup.mouse);
    ScreenToClient(window, &popup.mouse);
    SetCapture(window);
    MSG message;
    while (!popup.done) {
        int got = GetMessageW(&message, nullptr, 0, 0);
        if (got <= 0) {
            if (!got)
                PostQuitMessage(int(message.wParam));
            break;
        }
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            if (message.wParam == VK_F4 && GetKeyState(VK_MENU) < 0)
                SendMessageW(window, WM_CLOSE, 0, 0);
            else {
                if (message.wParam != VK_RETURN && message.wParam != VK_SPACE && message.wParam != VK_ESCAPE)
                    TranslateMessage(&message);
                SendMessageW(window, WM_KEYDOWN, message.wParam, message.lParam);
            }
            continue;
        }
        if (message.message == WM_CHAR || message.message == WM_SYSCHAR) {
            SendMessageW(window, WM_CHAR, message.wParam, message.lParam);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    labels_window = nullptr;
    if (IsWindow(window))
        DestroyWindow(window);
    if (GetCapture() == window)
        ReleaseCapture();
    labels_open = false;
    redraw_menu_bar(owner);
    if (IsWindow(focus))
        SetFocus(focus);
    return popup.result;
}

void paint_icon_view(HWND list, HDC dc, HIMAGELIST images, const IconViewImage* items, size_t count, bool desktop)
{
    if (!images || (!items && count) || redraw_suspended(list))
        return;
    RECT client{};
    GetClientRect(list, &client);
    int saved = SaveDC(dc);
    if (!saved)
        return;
    IntersectClipRect(dc, client.left, client.top, client.right, client.bottom);
    RECT damaged{};
    const int clip_kind = GetClipBox(dc, &damaged);
    if (clip_kind == ERROR || clip_kind == NULLREGION) {
        RestoreDC(dc, saved);
        return;
    }
    if (desktop) {
        if (!paint_quickdraw_desktop(list, dc) && !quickdraw_graphics)
            SendMessageW(list, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
    }
    else
        classic::fill_rect(dc, &damaged, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    int width = 0, height = 0;
    ImageList_GetIconSize(images, &width, &height);
    count = std::min(count, size_t(std::max(0, ListView_GetItemCount(list))));
    for (size_t index = 0; index < count; ++index) {
        RECT bounds{};
        if (!ListView_GetItemRect(list, int(index), &bounds, LVIR_ICON) || !RectVisible(dc, &bounds))
            continue;
        bool selected = ListView_GetItemState(list, int(index), LVIS_SELECTED | LVIS_DROPHILITED) != 0;
        draw_image(images, selected ? items[index].selected : items[index].normal, dc,
                   bounds.left + (bounds.right - bounds.left - width) / 2, bounds.top, width, height);
    }
    RestoreDC(dc, saved);
}

#ifdef FINDER_TESTING
// Exercise frame clipping without reading or painting a user's desktop.
void paint_window_chrome(HWND window, HDC dc)
{
    if (redraw_suspended(window))
        return;
    draw_chrome(window, dc);
}

// Exercise the same nonclient painter against a caller-owned window-sized DIB.
HWND scrollbar_window(HWND list)
{
    return scrollbar_surface(list);
}

void paint_list_scrollbars(HWND window, HDC dc)
{
    if (redraw_suspended(window))
        return;
    draw_scrollbars(window, dc);
}

// Render the production popup painter into an owned test DIB so pixel tests
// exercise the same row/frame code.
HBITMAP
label_menu_bitmap(void** pixels)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = label_canvas_width + 1;
    info.bmiHeader.biHeight = -label_canvas_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, pixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!bitmap || !dc) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        return nullptr;
    }
    auto previous = SelectObject(dc, bitmap);
    fill(dc, {0, 0, label_canvas_width + 1, label_canvas_height}, RGB(255, 255, 255));
    label_frame(dc);
    SetViewportOrgEx(dc, 1, 1, nullptr);
    LabelPopup popup{0};
    int top = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int height = i ? label_item_height : label_header_height;
        DRAWITEMSTRUCT draw{};
        draw.itemID = i;
        draw.hDC = dc;
        draw.rcItem = {0, top, label_frame_width - 2, top + height};
        label_row(draw, popup);
        top += height;
    }
    GdiFlush();
    SelectObject(dc, previous);
    DeleteDC(dc);
    return bitmap;
}
#endif
// Apply Finder fonts and button owner drawing to controls created by a Windows
// common dialog.
void style_dialog(HWND window)
{
    decorate(window);
    EnumChildWindows(
        window,
        [](HWND child, LPARAM) -> BOOL {
            SetWindowTheme(child, L"", L"");
            wchar_t type[32]{};
            GetClassNameW(child, type, 32);
            HFONT chosen = geneva;
            HFONT original = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
            int padding = 4;
            if (_wcsicmp(type, L"BUTTON") == 0) {
                auto style = GetWindowLongPtrW(child, GWL_STYLE);
                auto kind = style & BS_TYPEMASK;
                padding = kind == BS_GROUPBOX ? 4 : 22;
                if (kind == BS_PUSHBUTTON || kind == BS_DEFPUSHBUTTON) {
                    chosen = chicago;
                    SetPropW(child, L"FinderCommonButton", reinterpret_cast<HANDLE>(1));
                    if (kind == BS_DEFPUSHBUTTON)
                        SetPropW(child, L"FinderDefault", reinterpret_cast<HANDLE>(1));
                    SetWindowLongPtrW(child, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
                }
            }
            // Driver templates have fixed control widths. Keep their original font
            // for text that cannot fit in a classic font, without widening the dialog.
            wchar_t text[256]{};
            GetWindowTextW(child, text, 256);
            if (original && *text) {
                RECT bounds{}, measured{};
                GetClientRect(child, &bounds);
                auto dc = GetDC(child);
                auto old = SelectObject(dc, chosen);
                draw_text(dc, text, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_HIDEPREFIX);
                if (measured.right + padding > bounds.right)
                    chosen = original;
                SelectObject(dc, old);
                ReleaseDC(child, dc);
            }
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(chosen), TRUE);
            style_text_control(child);
            return TRUE;
        },
        0);
}

// Attach the bounded classic progress painter without replacing the native
// control's range model.
void style_progress(HWND w)
{
    SetWindowSubclass(w, progress_proc, 2, 0);
}

// Attach classic scrollbar painting while retaining the list view's native
// scrolling behavior.
void style_list(HWND w)
{
    if (auto header = ListView_GetHeader(w))
        style_text_control(header);
    load_scrollbar_art();
    if (scrollbar_hosts.count(w))
        return;
    RECT bounds;
    GetWindowRect(w, &bounds);
    MapWindowPoints(nullptr, GetParent(w), reinterpret_cast<POINT*>(&bounds), 2);
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"ROMAWFinderScrollGutters";
    type.lpfnWndProc = scrollbar_host_proc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    HWND host = CreateWindowExW(0, type.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, bounds.left,
                                bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, GetParent(w),
                                nullptr, type.hInstance, w);
    if (!host)
        throw std::runtime_error("Cannot create Finder scrollbar gutters");
    scrollbar_hosts[w] = host;
    SetPropW(GetParent(w), zoom_list_property, w);
    if (!SetWindowSubclass(w, list_style_proc, 3, 0)) {
        scrollbar_hosts.erase(w);
        DestroyWindow(host);
        throw std::runtime_error("Cannot attach Finder scrollbar gutters");
    }
    SetWindowPos(w, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (GetWindowLongPtrW(w, GWL_STYLE) & WS_VISIBLE)
        ShowWindow(host, SW_SHOWNOACTIVATE);
}

void style_listbox(HWND window)
{
    load_scrollbar_art();
    if (!SetPropW(window, L"FinderClassicListBox", reinterpret_cast<HANDLE>(1)))
        return;
    if (!SetWindowSubclass(window, list_style_proc, 3, 0))
        RemovePropW(window, L"FinderClassicListBox");
}

// Keep native icon/selection rendering, but give background erasure a stable
// tile origin. The pattern brush copies the bitmap and owns no borrowed pixels.
void style_desktop(HWND w)
{
    // The desktop is a sibling behind every document frame. Its buffered
    // background must exclude those siblings when completion invalidates it.
    SetWindowLongPtrW(w, GWL_STYLE, GetWindowLongPtrW(w, GWL_STYLE) | WS_CLIPSIBLINGS);
    // Transparent list-view buffering asks its parent to erase as well. Both
    // surfaces share the same origin because the desktop list starts at (0,0).
    for (HWND target : {GetParent(w), w}) {
        DWORD_PTR existing;
        if (!target || GetWindowSubclass(target, desktop_style_proc, 4, &existing))
            continue;
        HBITMAP bitmap = desktop_pattern();
        HBRUSH brush = bitmap ? CreatePatternBrush(bitmap) : nullptr;
        if (bitmap)
            DeleteObject(bitmap);
        if (!brush)
            brush = CreateSolidBrush(desktop_color());
        if (!brush)
            continue;
        if (!SetWindowSubclass(target, desktop_style_proc, 4, reinterpret_cast<DWORD_PTR>(brush)))
            DeleteObject(brush);
    }
    // CLR_NONE asks the list view to use its patterned parent background,
    // rather than replace it with a solid fill while drawing its items.
    ListView_SetBkColor(w, CLR_NONE);
    RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
}

// Create an owned top-down DIB from the decoded System PixPat.
HBITMAP
desktop_pattern()
{
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = desktop_pattern_width;
    bi.bmiHeader.biHeight = -desktop_pattern_height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits;
    auto bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
        return nullptr;
    std::memcpy(bits, desktop_pattern_pixels, sizeof desktop_pattern_pixels);
    return bitmap;
}

// Return the generated pattern's dominant RGB color for the desktop list's
// background.
COLORREF
desktop_color()
{
    return RGB((desktop_pattern_background >> 16) & 255, (desktop_pattern_background >> 8) & 255,
               desktop_pattern_background & 255);
}

// Borrow close/zoom/grow icons from the desktop; their owner retains
// responsibility for destruction.
void chrome_art(HICON close, HICON zoom, HICON grow, HICON clicked)
{
    close_art = close;
    zoom_art = zoom;
    grow_art = grow;
    clicked_art = clicked;
}

// Associate a borrowed icon with an owner-drawn command ID.
void menu_icon(unsigned command, HICON icon)
{
    menu_art[command] = icon;
    for (auto& item : menu_text)
        if (item.id == command)
            item.pixels.clear();
}

void replace_icon(HICON previous, HICON replacement)
{
    for (auto& art : menu_art)
        if (art.second == previous)
            menu_icon(art.first, replacement);

    struct Icons {
        HICON previous, replacement;
    } icons{previous, replacement};

    auto update = [](HWND window, LPARAM data) -> BOOL {
        auto& icons = *reinterpret_cast<Icons*>(data);
        for (unsigned kind : {ICON_SMALL, ICON_BIG})
            if (reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, kind, 0)) == icons.previous)
                SendMessageW(window, WM_SETICON, kind, reinterpret_cast<LPARAM>(icons.replacement));
        wchar_t type[16]{};
        GetClassNameW(window, type, 16);
        if (!_wcsicmp(type, L"STATIC") && (GetWindowLongPtrW(window, GWL_STYLE) & SS_TYPEMASK) == SS_ICON &&
            reinterpret_cast<HICON>(SendMessageW(window, STM_GETICON, 0, 0)) == icons.previous)
            SendMessageW(window, STM_SETICON, reinterpret_cast<WPARAM>(icons.replacement), 0);
        return TRUE;
    };

    struct Update {
        decltype(update) visit;
        Icons* icons;
    } context{update, &icons};

    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND root, LPARAM data) -> BOOL {
            auto& context = *reinterpret_cast<Update*>(data);
            context.visit(root, reinterpret_cast<LPARAM>(context.icons));
            EnumChildWindows(root, context.visit, reinterpret_cast<LPARAM>(context.icons));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
}

// Borrow notice, Apple-menu and label artwork for subsequent shared painters.
void artwork(HICON stop, HICON warning, HICON app, HICON apple, const HICON* label_icons)
{
    apple_icon = apple;
    stop_icon = stop;
    warning_icon = warning;
    application_icon = app;
    std::copy(label_icons, label_icons + 7, labels);
}

namespace {
constexpr const wchar_t* app_labels[] = {
    L"Window name:", L"System version:", L"Window size (W x H):", L"Bits per pixel:", L"System zone:", L"Application zone:"
};
enum AppControl {
    app_first = 100, app_default_cpu = 110, app_68k, app_ppc, app_gestalt_list, app_gestalt_value,
    app_gestalt_add, app_gestalt_update, app_gestalt_delete, app_gestalt_type
};
struct AppPreferences : AppPreferenceDraft {
    std::wstring title;
    int (*validate)(const char*);
    const std::function<bool(const AppPreferenceDraft&)>& save;
};

std::wstring preference_wide(const std::string& text)
{
    UINT page = CP_UTF8;
    int count = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count && !text.empty()) {
        page = CP_ACP;
        count = MultiByteToWideChar(page, 0, text.data(), int(text.size()), nullptr, 0);
    }
    std::wstring result(count, L'\0');
    MultiByteToWideChar(page, 0, text.data(), int(text.size()), result.data(), count);
    return result;
}

std::string preference_text(HWND field)
{
    std::wstring text(GetWindowTextLengthW(field) + 1, L'\0');
    text.resize(GetWindowTextW(field, text.data(), int(text.size())));
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

struct Notice {
    int result = IDCANCEL;
    const wchar_t* text;
    UINT flags;
    std::wstring* input = nullptr;
    HWND edit = nullptr;
    const wchar_t* affirmative = L"Stop and Quit";
    FindOptions* find = nullptr;
    const std::vector<SearchRoot>* disks = nullptr;
    bool has_selection = false;
    bool conflict = false;
    std::wstring simple_name{};
    bool changing_find = false;
    bool about = false;
    AppPreferences* application = nullptr;

    bool is_alert() const { return !about && !find && !input; }
};

// Match the dBoxProc frame used by the classic Window Manager.
constexpr int alert_frame = 8;
constexpr int app_width = 464, app_height = 360, app_cpu_height = 25;

void build_app_controls(HWND window, const AppPreferences& app)
{
    auto make = [&](const wchar_t* kind, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h) {
        auto child = CreateWindowW(kind, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                                   reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(geneva), FALSE);
        return child;
    };
    auto label = [&](const wchar_t* text, int x, int y, int width) {
        return make(L"STATIC", text, SS_LEFT, 0, x, y, width, 16);
    };
    SendMessageW(label(app.title.c_str(), 12, 10, app_width - 24), WM_SETFONT,
                 reinterpret_cast<WPARAM>(chicago), FALSE);
    label(L"Blank fields inherit ROMAW defaults. Use k or m for memory sizes.", 12, 30, app_width - 24);
    for (unsigned i = 0; i < app.fields.size(); ++i) {
        int y = 51 + 23 * i;
        label(app_labels[i], 12, y + 3, 125);
        const auto& stored = app.fields[i];
        auto text = i == 0 && stored.size() >= 2 && stored.front() == '"' && stored.back() == '"'
                        ? stored.substr(1, stored.size() - 2)
                        : stored;
        auto field = make(L"EDIT", preference_wide(text).c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, app_first + i,
                          140, y, app_width - 152, 19);
        SendMessageW(field, EM_SETLIMITTEXT, 240, 0);
        style_text_control(field, true);
    }
    int row = 196;
    if (app.fat) {
        label(L"Run FAT app as:", 12, row, 125);
        constexpr const wchar_t* choices[] = {L"Default", L"m68k", L"PowerPC"};
        for (int i = 0; i < 3; ++i) {
            auto radio = make(L"BUTTON", choices[i], BS_AUTORADIOBUTTON | (i ? 0 : WS_GROUP | WS_TABSTOP),
                              app_default_cpu + i, 140 + 90 * i, row - 2, 88, 19);
            style_checkbox(radio);
        }
        CheckRadioButton(window, app_default_cpu, app_ppc,
                         app.prefer_m68k ? (*app.prefer_m68k ? app_68k : app_ppc) : app_default_cpu);
        row += app_cpu_height;
    }
    label(L"Gestalt overrides", 12, row, 420);
    auto list = make(L"LISTBOX", L"", WS_GROUP | WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                     app_gestalt_list, 12, row + 18, app_width - 24, 54);
    for (const auto& statement : app.gestalts)
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preference_wide(statement).c_str()));
    label(L"Type", 12, row + 77, 96);
    label(L"Value", 116, row + 77, 116);
    auto type = make(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, app_gestalt_type, 12, row + 94, 96, 19);
    auto value = make(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, app_gestalt_value, 116, row + 94, 116, 19);
    SendMessageW(type, EM_SETLIMITTEXT, 10, 0);
    SendMessageW(value, EM_SETLIMITTEXT, 240, 0);
    style_text_control(type, true);
    style_text_control(value, true);
    auto button = [&](const wchar_t* text, int id, int x, int y, int width) {
        return make(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id, x, y, width, 24);
    };
    button(L"Add", app_gestalt_add, 240, row + 92, 62);
    EnableWindow(button(L"Update", app_gestalt_update, 310, row + 92, 66), FALSE);
    EnableWindow(button(L"Delete", app_gestalt_delete, 384, row + 92, 68), FALSE);
    label(L"Value: number, 'TEXT', or {error,value}", 12, row + 127, 262);
    button(L"Cancel", IDCANCEL, 282, row + 127, 76);
    SetPropW(button(L"OK", IDOK, 376, row + 127, 76), L"FinderDefault", reinterpret_cast<HANDLE>(1));
}

// Treat quoted and hexadecimal spellings of the same selector as one override.
uint32_t gestalt_selector(const std::string& statement)
{
    if (statement[8] == '0')
        return uint32_t(std::strtoul(statement.c_str() + 10, nullptr, 16));
    uint32_t code = 0;
    for (unsigned i = 9; i < 13; ++i)
        code = code << 8 | static_cast<unsigned char>(statement[i]);
    return code;
}

void update_app_gestalt(HWND window, AppPreferences& app, int command)
{
    auto list = GetDlgItem(window, app_gestalt_list);
    auto type = GetDlgItem(window, app_gestalt_type);
    auto value = GetDlgItem(window, app_gestalt_value);
    auto selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (command == app_gestalt_list) {
        if (selected != LB_ERR) {
            const auto& line = app.gestalts[selected];
            size_t end = line[8] == '0' ? 18 : 14;
            auto key = line[8] == '0' ? line.substr(8, 10) : line.substr(9, 4);
            auto data = line.substr(line.find('=', end) + 1);
            data.erase(data.find_last_not_of("; \t") + 1);
            if (data.compare(0, 3, "{0,") == 0 && data.back() == '}')
                data = data.substr(3, data.size() - 4);
            SetWindowTextW(type, preference_wide(key).c_str());
            SetWindowTextW(value, preference_wide(data).c_str());
        }
        EnableWindow(GetDlgItem(window, app_gestalt_update), selected != LB_ERR);
        EnableWindow(GetDlgItem(window, app_gestalt_delete), selected != LB_ERR);
        return;
    }
    if (command != app_gestalt_add && selected == LB_ERR)
        return;
    if (command == app_gestalt_delete) {
        SendMessageW(list, LB_DELETESTRING, selected, 0);
        app.gestalts.erase(app.gestalts.begin() + selected);
        SendMessageW(list, LB_SETCURSEL, -1, 0);
        SetWindowTextW(type, L"");
        SetWindowTextW(value, L"");
        update_app_gestalt(window, app, app_gestalt_list);
        return;
    }
    auto key = preference_text(type);
    if (key.size() == 4)
        key = "'" + key + "'";
    auto data = preference_text(value);
    if (data.size() == 6 && data.front() == '\'' && data.back() == '\'') {
        // Four-character responses are stored as the corresponding 32-bit value.
        char hex[11];
        snprintf(hex, sizeof hex, "0x%02X%02X%02X%02X", unsigned(uint8_t(data[1])), unsigned(uint8_t(data[2])),
                 unsigned(uint8_t(data[3])), unsigned(uint8_t(data[4])));
        data = hex;
    }
    auto first = data.find_first_not_of(" \t");
    if (first != std::string::npos && data[first] != '{')
        data = "{0," + data + "}";
    auto statement = "Gestalt(" + key + ")=" + data + ";";
    if (!app.validate(statement.c_str())) {
        message_box(window, L"Enter a four-character type or hex selector, and a number, 'TEXT', or {error,value}.",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    uint32_t code = gestalt_selector(statement);
    for (size_t i = 0; i < app.gestalts.size(); ++i)
        if ((command == app_gestalt_add || size_t(selected) != i) && gestalt_selector(app.gestalts[i]) == code) {
            message_box(window, L"That selector already exists. Select it and choose Update.", MB_OK | MB_ICONWARNING);
            return;
        }
    auto text = preference_wide(statement);
    if (command == app_gestalt_add) {
        selected = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (selected < 0)
            return;
        app.gestalts.push_back(std::move(statement));
    }
    else {
        SendMessageW(list, LB_DELETESTRING, selected, 0);
        SendMessageW(list, LB_INSERTSTRING, selected, reinterpret_cast<LPARAM>(text.c_str()));
        app.gestalts[selected] = std::move(statement);
    }
    SendMessageW(list, LB_SETCURSEL, selected, 0);
    update_app_gestalt(window, app, app_gestalt_list);
}

bool save_app_controls(HWND window, const AppPreferences& app)
{
    AppPreferenceDraft draft;
    draft.fat = app.fat;
    for (unsigned i = 0; i < draft.fields.size(); ++i) {
        auto field = GetDlgItem(window, app_first + i);
        auto value = preference_text(field);
        if (value.empty())
            continue;
        if (i == 0) {
            if (value.find('"') != std::string::npos) {
                message_box(window, L"Window names cannot contain double quotes.", MB_OK | MB_ICONWARNING);
                SetFocus(field);
                return false;
            }
            value = '"' + value + '"';
        }
        auto line = std::string(app_preference_keys[i]) + '=' + value;
        if (!app.validate(line.c_str())) {
            auto prompt = L"Check the value for " + std::wstring(app_labels[i]);
            message_box(window, prompt.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(field);
            return false;
        }
        draft.fields[i] = std::move(value);
    }
    if (app.fat && IsDlgButtonChecked(window, app_default_cpu) != BST_CHECKED)
        draft.prefer_m68k = IsDlgButtonChecked(window, app_68k) == BST_CHECKED;
    draft.gestalts = app.gestalts;
    if (app.save(draft))
        return true;
    message_box(window, L"ROMAW could not save the application's preferences.", MB_OK | MB_ICONERROR);
    return false;
}

void paint_alert_frame(HWND window, HDC dc)
{
    RECT bounds{};
    GetWindowRect(window, &bounds);
    OffsetRect(&bounds, -bounds.left, -bounds.top);
    RECT content = bounds;
    InflateRect(&content, -alert_frame, -alert_frame);
    int saved = SaveDC(dc);
    if (!saved)
        return;
    ExcludeClipRect(dc, content.left, content.top, content.right, content.bottom);
    fill(dc, bounds, RGB(255, 255, 255));
    for (auto [inset, shade] : {std::pair{0, 0}, std::pair{1, 221}, std::pair{2, 187},
                                std::pair{3, 221}, std::pair{4, 0}}) {
        RECT rule = bounds;
        InflateRect(&rule, -inset, -inset);
        frame(dc, rule, RGB(shade, shade, shade));
    }
    RestoreDC(dc, saved);
}

enum FindControl { find_toggle = 11057, find_field = 11052, find_relation, find_value, find_batch, find_scope };

std::vector<FindMenuChoice> find_relations(FindField field)
{
    switch (field) {
    case FindField::name:
        return {std::begin(find_menu_11030), std::end(find_menu_11030)};
    case FindField::comments:
        return {std::begin(find_menu_11035), std::end(find_menu_11035)};
    case FindField::size:
        return {std::begin(find_menu_11037), std::end(find_menu_11037)};
    case FindField::created:
    case FindField::modified:
        return {std::begin(find_menu_11032), std::end(find_menu_11032)};
    case FindField::lock:
        return {{int(FindRelation::equal), L"is"}};
    default:
        return {std::begin(find_menu_11031), std::end(find_menu_11031)};
    }
}

std::vector<FindMenuChoice> find_values(FindField field)
{
    switch (field) {
    case FindField::kind:
        return {std::begin(find_menu_11036), std::end(find_menu_11036)};
    case FindField::lock:
        return {std::begin(find_menu_11038), std::end(find_menu_11038)};
    case FindField::label: {
        std::vector<FindMenuChoice> labels;
        for (int i = 0; i < int(std::size(label_menu_names)); ++i)
            labels.push_back({i, label_menu_names[i]});
        return labels;
    }
    default:
        return {};
    }
}

constexpr int first_disk_choice = 100;

std::vector<FindMenuChoice> find_scopes(const Notice& data)
{
    std::vector<FindMenuChoice> scopes{{int(FindScope::current), L"in this folder"}};
    for (auto choice : find_menu_11056)
        if (choice.value != int(FindScope::selection) || data.has_selection)
            scopes.push_back(choice);
    for (size_t i = 0; i < data.disks->size(); ++i)
        scopes.push_back({first_disk_choice + int(i), (*data.disks)[i].title.c_str()});
    return scopes;
}

int find_scope_value(const Notice& data)
{
    if (data.find->scope == FindScope::disk)
        for (size_t i = 0; i < data.disks->size(); ++i)
            if ((*data.disks)[i].path == data.find->disk)
                return first_disk_choice + int(i);
    return int(data.find->scope);
}

template <typename Choices> const wchar_t* find_choice_text(const Choices& choices, int value)
{
    for (auto entry : choices)
        if (entry.value == value)
            return entry.text;
    return L"";
}

template <typename Choices> int choose_find_value(HWND window, int id, const Choices& choices, int value)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return value;
    unsigned index = 0;
    for (auto choice : choices)
        AppendMenuW(menu, MF_STRING | (choice.value == value ? MF_CHECKED : 0), ++index, choice.text);
    RECT anchor{};
    GetWindowRect(GetDlgItem(window, id), &anchor);
    auto choice = track_context_menu(window, menu, {anchor.left, anchor.bottom});
    DestroyMenu(menu);
    return choice && choice <= index ? choices[choice - 1].value : value;
}

void read_find_controls(HWND w, Notice& data)
{
    if (data.edit) {
        std::wstring text(GetWindowTextLengthW(data.edit) + 1, L'\0');
        text.resize(GetWindowTextW(data.edit, text.data(), int(text.size())));
        data.find->query.text = std::move(text);
    }
    if (data.find->query.field == FindField::name)
        data.simple_name = data.find->query.text;
    if (data.find->expanded)
        data.find->all_at_once = SendDlgItemMessageW(w, find_batch, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void build_find_controls(HWND w, Notice& data)
{
    auto& choices = *data.find;
    auto& query = choices.query;
    data.edit = nullptr;
    for (auto child = GetWindow(w, GW_CHILD); child;) {
        auto next = GetWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }
    auto make = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id, RECT bounds) {
        HWND child = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, bounds.left, bounds.top,
                                   bounds.right - bounds.left, bounds.bottom - bounds.top, w,
                                   reinterpret_cast<HMENU>(intptr_t(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(chicago), TRUE);
        SetWindowTheme(child, L"", L"");
        style_text_control(child);
        return child;
    };
    auto button = [&](int id, const wchar_t* title, RECT bounds) {
        return make(L"BUTTON", title, WS_TABSTOP | BS_OWNERDRAW, id, bounds);
    };
    auto popup = [&](int id, const wchar_t* title, RECT bounds) {
        std::wstring caption = std::wstring(title) + L"  \x25be";
        return button(id, caption.c_str(), bounds);
    };
    auto edit = [&](RECT bounds) {
        data.edit = make(L"EDIT", query.text.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, find_value, bounds);
        SendMessageW(data.edit, EM_SETLIMITTEXT, 255, 0);
        SendMessageW(data.edit, EM_SETSEL, 0, -1);
    };
    HWND submit;
    if (choices.expanded) {
        const wchar_t* label = query.field == FindField::size ? L"Find and select items whose size (in bytes)"
                               : query.field == FindField::created || query.field == FindField::modified
                                   ? L"Find and select items whose date (YYYY-MM-DD)"
                                   : find_expanded_label_text;
        make(L"STATIC", label, SS_LEFT, 0, find_expanded_label);
        popup(find_field, find_choice_text(find_menu_11052, int(query.field)), find_expanded_field);
        popup(find_relation, find_choice_text(find_relations(query.field), int(query.relation)),
              find_expanded_relation);
        auto values = find_values(query.field);
        if (values.empty())
            edit(find_expanded_value);
        else
            popup(find_value, find_choice_text(values, int(query.value)), find_expanded_value);
        make(L"STATIC", find_expanded_search_text, SS_LEFT, 0, find_expanded_search);
        popup(find_scope, find_choice_text(find_scopes(data), find_scope_value(data)), find_expanded_scope);
        auto batch =
            make(L"BUTTON", find_expanded_batch_text, WS_TABSTOP | BS_AUTOCHECKBOX, find_batch, find_expanded_batch);
        style_checkbox(batch);
        SendMessageW(batch, BM_SETCHECK, choices.all_at_once ? BST_CHECKED : BST_UNCHECKED, 0);
        button(find_toggle, find_expanded_toggle_text, find_expanded_toggle);
        button(IDCANCEL, find_expanded_cancel_text, find_expanded_cancel);
        submit = button(IDOK, find_expanded_submit_text, find_expanded_submit);
    }
    else {
        make(L"STATIC", find_simple_label_text, SS_LEFT, 0, find_simple_label);
        edit(find_simple_value);
        button(find_toggle, find_simple_toggle_text, find_simple_toggle);
        button(IDCANCEL, find_simple_cancel_text, find_simple_cancel);
        submit = button(IDOK, find_simple_submit_text, find_simple_submit);
    }
    SetPropW(submit, L"FinderDefault", reinterpret_cast<HANDLE>(1));
    SetFocus(data.edit ? data.edit : GetDlgItem(w, find_field));
}

void resize_find_panel(HWND w, bool expanded)
{
    RECT bounds{}, client{};
    GetWindowRect(w, &bounds);
    GetClientRect(w, &client);
    int width = (expanded ? find_expanded_width : find_simple_width) + (bounds.right - bounds.left - client.right);
    int height = (expanded ? find_expanded_height : find_simple_height) + (bounds.bottom - bounds.top - client.bottom);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = std::clamp(int(bounds.left), int(monitor.rcWork.left),
                       std::max(int(monitor.rcWork.left), int(monitor.rcWork.right) - width));
    int y = std::clamp(int(bounds.top), int(monitor.rcWork.top),
                       std::max(int(monitor.rcWork.top), int(monitor.rcWork.bottom) - height));
    SetWindowPos(w, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
}

// Create and dispatch a classic notice/input window using state owned by its
// enclosing modal loop.
LRESULT CALLBACK notice_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* data = reinterpret_cast<Notice*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        data = static_cast<Notice*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        if (!data->about && !data->is_alert())
            decorate(w);
        SetPropW(w, L"FinderWhite", reinterpret_cast<HANDLE>(1));
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
    }
    if (!data)
        return DefWindowProcW(w, msg, wp, lp);
    if (data->is_alert()) {
        LRESULT result{};
        if (owner_draw(msg, lp, result))
            return result;
        switch (msg) {
        case WM_NCCALCSIZE: {
            auto* bounds = wp ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0] : reinterpret_cast<RECT*>(lp);
            InflateRect(bounds, -alert_frame, -alert_frame);
            return 0;
        }
        case WM_NCPAINT: {
            HDC dc = GetWindowDC(w);
            if (dc) {
                paint_alert_frame(w, dc);
                ReleaseDC(w, dc);
            }
            return 0;
        }
        case WM_NCACTIVATE:
            return TRUE;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(w, &point);
            RECT client{};
            GetClientRect(w, &client);
            return PtInRect(&client, point) ? HTCLIENT : HTBORDER;
        }
        case WM_PARENTNOTIFY:
            if (LOWORD(wp) == WM_CREATE)
                style_text_control(reinterpret_cast<HWND>(lp));
            break;
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(w, &client);
            fill(reinterpret_cast<HDC>(wp), client, RGB(255, 255, 255));
            return TRUE;
        }
        }
    }
    if (msg == WM_CREATE) {
        RECT r;
        GetClientRect(w, &r);
        if (data->application) {
            build_app_controls(w, *data->application);
            return 0;
        }
        if (data->about) {
            int right = r.right - ROMAW_ABOUT_FRAME - ROMAW_ABOUT_MARGIN;
            int bottom = r.bottom - ROMAW_ABOUT_FRAME - ROMAW_ABOUT_MARGIN;
            auto close = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                       right - ROMAW_ABOUT_BUTTON_WIDTH, bottom - ROMAW_ABOUT_BUTTON_HEIGHT,
                                       ROMAW_ABOUT_BUTTON_WIDTH, ROMAW_ABOUT_BUTTON_HEIGHT, w,
                                       reinterpret_cast<HMENU>(INT_PTR(IDOK)), GetModuleHandleW(nullptr), nullptr);
            if (!close)
                return -1;
            return 0;
        }
        if (data->find) {
            build_find_controls(w, *data);
            return 0;
        }
        auto icon = (data->flags & MB_ICONMASK) == MB_ICONERROR     ? stop_icon
                    : (data->flags & MB_ICONMASK) == MB_ICONWARNING ? warning_icon
                                                                    : application_icon;
        auto image = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON, alert_icon.left, alert_icon.top, 32,
                                   32, w, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(image, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
        auto text = CreateWindowW(L"STATIC", data->text, WS_CHILD | WS_VISIBLE | SS_LEFT, alert_text.left,
                                  alert_text.top, alert_text.right - alert_text.left, r.bottom - 45, w, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
        SendMessageW(text, WM_SETFONT, reinterpret_cast<WPARAM>(chicago), TRUE);
        if (data->input) {
            data->edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", data->input->c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, alert_text.left, r.bottom - 68,
                                r.right - alert_text.left - 10, 22, w, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(data->edit, WM_SETFONT, reinterpret_cast<WPARAM>(geneva), TRUE);
            SendMessageW(data->edit, EM_SETLIMITTEXT, 32767, 0);
            SendMessageW(data->edit, EM_SETSEL, 0, -1);
        }
        auto button = [&](const wchar_t* label, int id, int x, int width) {
            auto b =
                CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, r.bottom - 34,
                              width, 28, w, reinterpret_cast<HMENU>(intptr_t(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(chicago), TRUE);
            if (id == IDOK || id == IDYES)
                SetPropW(b, L"FinderDefault", reinterpret_cast<HANDLE>(1));
            return b;
        };
        if ((data->flags & MB_TYPEMASK) == MB_YESNOCANCEL) {
            button(data->conflict ? L"Stop" : L"Cancel", IDCANCEL, r.right - 278, 76);
            button(data->conflict ? L"Skip" : L"No", IDNO, r.right - 192, 76);
            SetFocus(button(data->conflict ? L"Replace" : L"Yes", IDYES, r.right - 106, 88));
        }
        else if (data->flags & MB_YESNO) {
            button(L"Cancel", IDNO, r.right - 210, 76);
            SetFocus(button(data->affirmative, IDYES, r.right - 120, 102));
        }
        else {
            if ((data->flags & MB_TYPEMASK) == MB_OKCANCEL)
                button(L"Cancel", IDCANCEL, alert_cancel.left, 59);
            SetFocus(button(L"OK", IDOK, alert_button.left - 4, alert_button.right - alert_button.left + 8));
        }
        if (data->edit)
            SetFocus(data->edit);
        return 0;
    }
    if (data->about) {
        LRESULT result{};
        if (owner_draw(msg, lp, result))
            return result;
        if (msg == WM_ERASEBKGND)
            return TRUE;
        if (msg == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(w, &paint);
            RECT bounds{};
            GetClientRect(w, &bounds);
            if (!paint_manager_control(dc, bounds, ROMAW_GRAPHICS_ABOUT, 0, nullptr)) {
                fill(dc, bounds, RGB(255, 255, 255));
                auto previous = SelectObject(dc, chicago);
                draw_text(dc, L"About ROMAW", -1, &bounds, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                SelectObject(dc, previous);
            }
            EndPaint(w, &paint);
            return 0;
        }
    }
    if (msg == WM_PAINT && data->find && data->find->expanded) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(w, &paint);
        for (const auto& rule : {find_expanded_rule1, find_expanded_rule2})
            draw_line(dc, rule.left, rule.top, rule.right, rule.top, RGB(0, 0, 0));
        EndPaint(w, &paint);
        return 0;
    }
    if (msg == WM_COMMAND && data->application) {
        int id = LOWORD(wp);
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDCANCEL || save_app_controls(w, *data->application)) {
                data->result = id;
                DestroyWindow(w);
            }
        }
        else if ((id == app_gestalt_list && HIWORD(wp) == LBN_SELCHANGE) ||
                 (id >= app_gestalt_add && id <= app_gestalt_delete && HIWORD(wp) == BN_CLICKED))
            update_app_gestalt(w, *data->application, id);
        return 0;
    }
    if (msg == WM_COMMAND && data->find) {
        if (data->changing_find)
            return 0;

        struct ChangingFind {
            bool& active;

            ~ChangingFind()
            {
                active = false;
            }
        } guard{data->changing_find};

        data->changing_find = true;
        auto& options = *data->find;
        auto& query = options.query;
        int id = LOWORD(wp);
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK)
                read_find_controls(w, *data);
            data->result = id;
            DestroyWindow(w);
            return 0;
        }
        if (HIWORD(wp) != BN_CLICKED || (id == find_value && data->edit) || id == find_batch)
            return 0;
        read_find_controls(w, *data);
        switch (id) {
        case find_toggle:
            options.expanded = !options.expanded;
            if (!options.expanded) {
                query = {FindField::name, FindRelation::contains, data->simple_name};
                options.scope = FindScope::current;
                options.all_at_once = false;
            }
            break;
        case find_field: {
            auto field = FindField(choose_find_value(w, id, find_menu_11052, int(query.field)));
            if (field == query.field)
                return 0;
            query = {field, FindRelation(find_relations(field).front().value), L""};
            auto values = find_values(field);
            if (!values.empty())
                query.value = values.front().value;
            if (field == FindField::name)
                query.text = data->simple_name;
            break;
        }
        case find_relation:
            query.relation = FindRelation(choose_find_value(w, id, find_relations(query.field), int(query.relation)));
            break;
        case find_value:
            query.value = choose_find_value(w, id, find_values(query.field), int(query.value));
            break;
        case find_scope: {
            int chosen = choose_find_value(w, id, find_scopes(*data), find_scope_value(*data));
            if (chosen >= first_disk_choice) {
                options.scope = FindScope::disk;
                options.disk = (*data->disks)[chosen - first_disk_choice].path;
            }
            else
                options.scope = FindScope(chosen);
            break;
        }
        default:
            return 0;
        }
        RedrawBatch redraw(w);
        if (id == find_toggle)
            resize_find_panel(w, options.expanded);
        build_find_controls(w, *data);
        InvalidateRect(w, nullptr, TRUE);
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) != IDOK && LOWORD(wp) != IDCANCEL && LOWORD(wp) != IDYES && LOWORD(wp) != IDNO)
            return 0;
        data->result = LOWORD(wp);
        if (data->input && data->result == IDOK) {
            std::wstring value(GetWindowTextLengthW(data->edit) + 1, L'\0');
            GetWindowTextW(data->edit, value.data(), int(value.size()));
            value.resize(wcslen(value.c_str()));
            *data->input = std::move(value);
        }
        DestroyWindow(w);
        return 0;
    }
    if (msg == WM_CLOSE) {
        if (data->changing_find)
            return 0;
        DestroyWindow(w);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
}

// Run a movable classic notice, disabling its owner during the loop, restoring
// prior focus and forwarding quit messages.
static int run_notice(HWND owner, Notice data)
{
    if (!chicago)
        return MessageBoxW(owner, data.text, L"Finder", MB_OK | data.flags);
    auto root = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    WNDCLASSW c{};
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"FinderNotice";
    c.lpfnWndProc = notice_proc;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&c);
    HDC dc = GetDC(nullptr);
    auto old = SelectObject(dc, chicago);
    RECT measured{0, 0, alert_text.right - alert_text.left, 0};
    draw_text(dc, data.text, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    int width = alert_width + (data.is_alert() ? 2 * alert_frame : 4);
    int height = std::clamp(int(measured.bottom) + (data.input ? 88 : 52), alert_height, 600) +
                 (data.is_alert() ? 2 * alert_frame : 21);
    RECT area;
    GetWindowRect(root ? root : GetDesktopWindow(), &area);
    if (data.find) {
        width = (data.find->expanded ? find_expanded_width : find_simple_width) + 4;
        height = (data.find->expanded ? find_expanded_height : find_simple_height) + 21;
    }
    if (data.about) {
        width = ROMAW_ABOUT_WIDTH + 2 * ROMAW_ABOUT_FRAME;
        height = ROMAW_ABOUT_HEIGHT + 2 * ROMAW_ABOUT_FRAME;
    }
    if (data.application) {
        width = app_width + 2 * alert_frame;
        height = app_height + 2 * alert_frame + (data.application->fat ? app_cpu_height : 0);
    }
    if (data.find && data.find->query.field == FindField::name)
        data.simple_name = data.find->query.text;
    HWND focus = GetFocus(), previous = front;
    bool document_frame = !data.about && !data.is_alert();
    DWORD style = WS_POPUP | (document_frame ? WS_CAPTION | WS_SYSMENU : WS_CLIPCHILDREN);
    const wchar_t* title = data.about ? L"About ROMAW" : data.find ? L"Find" : L"Finder";
    HWND dialog = CreateWindowExW(document_frame ? WS_EX_DLGMODALFRAME : 0, L"FinderNotice", title, style,
                                  area.left + (area.right - area.left - width) / 2,
                                  area.top + (area.bottom - area.top - height) / 2, width, height, root, nullptr,
                                  c.hInstance, &data);
    if (!dialog)
        return IDCANCEL;
    if (root)
        EnableWindow(root, FALSE);
    ShowWindow(dialog, SW_SHOW);
    if (data.about)
        SetFocus(GetDlgItem(dialog, IDOK));
    else {
        activate(dialog);
        if (data.application)
            SetFocus(GetDlgItem(dialog, app_first));
    }
    MSG msg;
    while (IsWindow(dialog)) {
        int got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (!got)
                PostQuitMessage(int(msg.wParam));
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(dialog, WM_CLOSE, 0, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            int command = data.conflict || (data.flags & MB_YESNO) ? IDYES : IDOK;
            if (data.application) {
                int focused = GetDlgCtrlID(GetFocus());
                if (focused == IDCANCEL || (focused >= app_gestalt_add && focused <= app_gestalt_delete))
                    command = focused;
            }
            if ((data.flags & MB_TYPEMASK) == MB_YESNOCANCEL) {
                int focused = GetDlgCtrlID(GetFocus());
                if (focused == IDYES || focused == IDNO || focused == IDCANCEL)
                    command = focused;
            }
            SendMessageW(dialog, WM_COMMAND, command, 0);
            continue;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (IsWindow(dialog))
        DestroyWindow(dialog);
    if (root) {
        EnableWindow(root, TRUE);
        SetActiveWindow(root);
    }
    if (IsWindow(previous))
        activate(previous);
    if (IsWindow(focus))
        SetFocus(focus);
    return data.result;
}

int notice(HWND owner, const wchar_t* text, UINT flags, std::wstring* input,
           const wchar_t* affirmative = L"Stop and Quit", bool conflict = false, FindOptions* find = nullptr,
           bool has_selection = false, const std::vector<SearchRoot>* disks = nullptr)
{
    return run_notice(owner,
                      {IDCANCEL, text, flags, input, nullptr, affirmative, find, disks, has_selection, conflict});
}

void about_box(HWND owner)
{
    Notice data{IDCANCEL, L"About ROMAW", 0};
    data.about = true;
    run_notice(owner, data);
}

void configure_app(HWND owner, const std::wstring& name, AppPreferenceDraft draft, int (*validate)(const char*),
                   const std::function<bool(const AppPreferenceDraft&)>& save)
{
    AppPreferences app{std::move(draft), L"Configure App: " + name, validate, save};
    Notice notice{IDCANCEL, L"Configure App", MB_OKCANCEL};
    notice.application = &app;
    run_notice(owner, notice);
}

// Return a standard dialog result from the shared classic notice
// implementation.
int message_box(HWND owner, const wchar_t* text, UINT flags)
{
    return notice(owner, text, flags, nullptr);
}

// Replacement decisions use the same movable, keyboard-accessible notice
// loop as other Finder alerts. Escape/close always mean Stop, never Replace.
int conflict_box(HWND owner, const wchar_t* text)
{
    return notice(owner, text, MB_YESNOCANCEL | MB_ICONWARNING, nullptr, L"Replace", true);
}

int yes_no_cancel_box(HWND owner, const wchar_t* text)
{
    return notice(owner, text, MB_YESNOCANCEL | MB_ICONINFORMATION, nullptr);
}

// Present the requested affirmative action and return true only for an
// explicit Yes.
bool confirm(HWND owner, const wchar_t* text, const wchar_t* affirmative)
{
    return notice(owner, text, MB_YESNO | MB_ICONWARNING, nullptr, affirmative) == IDYES;
}

// Use the resource-derived Find layout and update the query only when the user
// accepts it.
bool find_box(HWND owner, FindOptions& value, bool has_selection, const std::vector<SearchRoot>& disks)
{
    auto draft = value;
    if (!has_selection && draft.scope == FindScope::selection)
        draft.scope = FindScope::current;
    if (draft.scope == FindScope::disk &&
        std::none_of(disks.begin(), disks.end(), [&](const auto& disk) { return disk.path == draft.disk; }))
        draft.scope = FindScope::current;
    if (notice(owner, L"Find:", MB_OKCANCEL, nullptr, L"Find", false, &draft, has_selection, &disks) != IDOK)
        return false;
    value = std::move(draft);
    return true;
}

// Edit a caller-owned string through the shared notice loop, preserving it on
// cancellation.
bool input_box(HWND owner, const wchar_t* text, std::wstring& value)
{
    return notice(owner, text, MB_OKCANCEL, &value) == IDOK;
}
}
