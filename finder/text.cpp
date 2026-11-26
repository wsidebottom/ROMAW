/*+===================================================================
  File:      text.cpp

  Summary:   Present Finder views, shapes, images, text and editable controls through the shared QuickDraw renderer.

  Classes:   Declared: DrawingSurface, OwnedIconBits, TextRaster. Uses bounded raster caches and Win32 control models.

  Functions: finder::classic::drag_artwork, finder::classic::draw_bitmap, finder::classic::draw_icon, finder::classic::draw_image,
             finder::classic::draw_line, finder::classic::draw_pixels, finder::classic::draw_polygon,
             finder::classic::draw_round_rect, finder::classic::draw_text, finder::classic::fill_color,
             finder::classic::fill_rect, finder::classic::frame_rect, finder::classic::icon_label_bounds,
             finder::classic::paint_pixel, finder::classic::paint_view, finder::classic::style_text_control,
             finder::classic::finish_text.

  Origin:    Executor native Finder presentation adapter; Windows retains input, accessibility and Unicode glyph sources.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "finder/classic.h"
#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cstring>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace finder::classic {
namespace {
// A DC is only the host presentation endpoint. Capture RGB for transparent
// operations, let QuickDraw draw into its own GWorld, then publish the result.
// No guest memory, port or packed record is stored in a Windows object.
struct DrawingSurface {
    DrawingSurface(const DrawingSurface&) = delete;
    DrawingSurface& operator=(const DrawingSurface&) = delete;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    uint32_t* pixels = nullptr;
    BITMAPINFO info{};

    DrawingSurface(HDC target, RECT bounds, bool preserve_pixels = true)
    {
        int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        if (!dc || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
            uint64_t(width) * height > 64 * 1024 * 1024)
            return;
        info.bmiHeader.biSize = sizeof info.bmiHeader;
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!bitmap)
            return;
        previous = SelectObject(dc, bitmap);
        std::fill_n(pixels, size_t(width) * height, 0xffffff);
        if (preserve_pixels) {
            BitBlt(dc, 0, 0, width, height, target, bounds.left, bounds.top, SRCCOPY);
            GdiFlush();
        }
    }

    ~DrawingSurface()
    {
        if (previous && previous != HGDI_ERROR)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
    }
};

bool draw_surface(HDC dc, RECT bounds, ROMAWGraphicsDrawing drawing, COLORREF color)
{
    auto* graphics = shared_graphics();
    if (!graphics || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return false;
    RECT clip{}, visible{};
    if (GetClipBox(dc, &clip) == NULLREGION || !IntersectRect(&visible, &clip, &bounds))
        return true;
    try {
        DrawingSurface surface(dc, bounds);
        if (!surface.pixels)
            return false;
        ROMAWGraphicsRequest request{};
        request.size = sizeof request;
        request.kind = ROMAW_GRAPHICS_DRAWING;
        request.key = 0x46494e4444524157ULL;
        request.width = bounds.right - bounds.left;
        request.height = bounds.bottom - bounds.top;
        request.foreground = (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
        request.drawing = &drawing;
        ROMAWGraphicsMetrics metrics{};
        if (graphics->render(&request, surface.pixels, size_t(request.width) * 4, &metrics))
            return false;
        return SetDIBitsToDevice(dc, bounds.left, bounds.top, request.width, request.height, 0, 0, 0, request.height,
                                 surface.pixels, &surface.info, DIB_RGB_COLORS) != 0;
    }
    catch (...) {
        return false;
    }
}

COLORREF brush_color(HBRUSH brush)
{
    LOGBRUSH value{};
    GetObjectW(brush, sizeof value, &value);
    return value.lbColor;
}

struct TextRaster {
    std::vector<uint32_t> pixels;
    ROMAWGraphicsMetrics metrics{};
};

using TextKey = std::tuple<std::wstring, int, int, int, int, int, UINT, COLORREF>;
std::map<TextKey, std::unique_ptr<TextRaster>> text_cache;
size_t cache_bytes = 0;

// Unicode names that the classic MacRoman fonts cannot represent retain their
// original spelling through the host fallback. No filename is ever substituted.
bool encode_text(const std::wstring& text, std::string& encoded)
{
    if (text.size() > 32767)
        return false;
    BOOL lost = FALSE;
    int count = WideCharToMultiByte(10000, 0, text.data(), int(text.size()), nullptr, 0, nullptr, &lost);
    if ((!count && !text.empty()) || lost)
        return false;
    encoded.resize(count);
    return !count ||
           (WideCharToMultiByte(10000, 0, text.data(), int(text.size()), encoded.data(), count, nullptr, &lost) &&
            !lost);
}

// The host edit keeps its original client geometry. Paint only its nonclient
// strips with a QuickDraw frame, avoiding the Windows sunken-edge renderer.
bool paint_edit_frame(HWND window)
{
    auto style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!(style & WS_BORDER) && !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_CLIENTEDGE))
        return false;
    auto* graphics = shared_graphics();
    if (!graphics)
        return false;
    RECT outer;
    GetWindowRect(window, &outer);
    int width = outer.right - outer.left, height = outer.bottom - outer.top;
    if (width < 1 || height < 1 || uint64_t(width) * height > 4 * 1024 * 1024)
        return false;
    ROMAWGraphicsRequest request{};
    request.size = sizeof request;
    request.kind = ROMAW_GRAPHICS_FIELD;
    request.key = 0x46494e4445444954ULL;
    request.width = width;
    request.height = height;
    request.background = 0xffffff;
    ROMAWGraphicsMetrics metrics{};
    std::vector<uint32_t> pixels(size_t(width) * height);
    int error = graphics->render(&request, pixels.data(), size_t(width) * 4, &metrics);
    graphics->release(request.key);
    if (error)
        return false;
    HDC dc = GetWindowDC(window);
    int saved = SaveDC(dc);
    ExcludeClipRect(dc, 1, 1, width - 1, height - 1);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    SetDIBitsToDevice(dc, 0, 0, width, height, 0, 0, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    RestoreDC(dc, saved);
    ReleaseDC(window, dc);
    return true;
}

// The header control still supplies column sizing, hit testing and sorting;
// QuickDraw supplies its frame and the same Font Manager labels as the rows.
LRESULT CALLBACK header_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        if (redraw_suspended(window))
            return 0;
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wp);
        int saved = SaveDC(dc);
        RECT client;
        GetClientRect(window, &client);
        HBRUSH gray = CreateSolidBrush(RGB(221, 221, 221));
        fill_rect(dc, &client, gray);
        DeleteObject(gray);
        SelectObject(dc, small_font());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        for (int i = 0; i < Header_GetItemCount(window); ++i) {
            RECT bounds;
            Header_GetItemRect(window, i, &bounds);
            wchar_t text[1024]{};
            HDITEMW item{};
            item.mask = HDI_TEXT | HDI_FORMAT;
            item.pszText = text;
            item.cchTextMax = std::size(text);
            Header_GetItem(window, i, &item);
            auto* graphics = shared_graphics();
            int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
            if (graphics && width > 0 && height > 0 && uint64_t(width) * height < 1024 * 1024) {
                ROMAWGraphicsRequest request{};
                request.size = sizeof request;
                request.kind = ROMAW_GRAPHICS_FIELD;
                request.key = 0x46494e4448454144ULL;
                request.width = width;
                request.height = height;
                request.background = 0xdddddd;
                ROMAWGraphicsMetrics metrics{};
                std::vector<uint32_t> pixels(size_t(width) * height);
                if (!graphics->render(&request, pixels.data(), size_t(width) * 4, &metrics)) {
                    BITMAPINFO info{};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = width;
                    info.bmiHeader.biHeight = -height;
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    SetDIBitsToDevice(dc, bounds.left, bounds.top, width, height, 0, 0, 0, height, pixels.data(), &info,
                                      DIB_RGB_COLORS);
                }
                graphics->release(request.key);
            }
            bounds.left += 4;
            bounds.right -= 4;
            draw_text(dc, text, -1, &bounds,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                          (item.fmt & HDF_RIGHT    ? DT_RIGHT
                           : item.fmt & HDF_CENTER ? DT_CENTER
                                                   : DT_LEFT));
        }
        RestoreDC(dc, saved);
        if (message == WM_PAINT)
            EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return TRUE;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, header_proc, id);
    return DefSubclassProc(window, message, wp, lp);
}

LRESULT CALLBACK text_control_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR mode)
{
    bool edit_frame = mode == 5;
    bool edit = mode == 1 || edit_frame, icon = mode == 2, group = mode == 4;
    if (edit && message == WM_NCPAINT) {
        if (paint_edit_frame(window) || shared_graphics())
            return 0;
        return DefSubclassProc(window, message, wp, lp);
    }
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        if (redraw_suspended(window))
            return 0;
        PAINTSTRUCT paint{};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wp);
        int saved = SaveDC(dc);
        RECT bounds;
        GetClientRect(window, &bounds);
        IntersectClipRect(dc, 0, 0, bounds.right, bounds.bottom);
        bool white = edit || GetPropW(window, L"FinderWhite") || GetPropW(GetParent(window), L"FinderWhite");
        HBRUSH background = CreateSolidBrush(white ? RGB(255, 255, 255) : RGB(221, 221, 221));
        fill_rect(dc, &bounds, background);
        DeleteObject(background);
        auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        SelectObject(dc, font ? font : small_font());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, IsWindowEnabled(window) ? RGB(0, 0, 0) : RGB(128, 128, 128));
        std::wstring text(GetWindowTextLengthW(window) + 1, 0);
        GetWindowTextW(window, text.data(), int(text.size()));
        text.resize(wcslen(text.c_str()));
        auto style = GetWindowLongPtrW(window, GWL_STYLE);
        if (icon) {
            auto image = reinterpret_cast<HICON>(SendMessageW(window, STM_GETICON, 0, 0));
            if (image)
                draw_icon(dc, 0, 0, image, bounds.right, bounds.bottom);
        }
        else if (group) {
            RECT outline = bounds;
            outline.top += 5;
            frame_rect(dc, &outline, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            RECT caption{};
            draw_text(dc, text.c_str(), -1, &caption, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            OffsetRect(&caption, 8, 0);
            fill_color(dc, caption, white ? RGB(255, 255, 255) : RGB(221, 221, 221));
            draw_text(dc, text.c_str(), -1, &caption, DT_SINGLELINE | DT_NOPREFIX);
        }
        else if (!edit) {
            UINT format = (style & SS_NOPREFIX ? DT_NOPREFIX : 0);
            auto kind = style & SS_TYPEMASK;
            if (kind == SS_CENTER)
                format |= DT_CENTER;
            if (kind == SS_RIGHT)
                format |= DT_RIGHT;
            format |= style & SS_CENTERIMAGE                           ? DT_SINGLELINE | DT_VCENTER
                      : kind == SS_SIMPLE || kind == SS_LEFTNOWORDWRAP ? DT_SINGLELINE
                                                                       : DT_WORDBREAK;
            if (style & (SS_ENDELLIPSIS | SS_PATHELLIPSIS | SS_WORDELLIPSIS))
                format |= DT_END_ELLIPSIS;
            draw_text(dc, text.c_str(), -1, &bounds, format);
        }
        else {
            RECT content;
            SendMessageW(window, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&content));
            if (edit_frame) {
                // Paint the outline with the text so it survives sibling repaints.
                frame_rect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                IntersectClipRect(dc, 1, 1, bounds.right - 1, bounds.bottom - 1);
            }
            IntersectClipRect(dc, content.left, content.top, content.right, content.bottom);
            DWORD start = 0, end = 0;
            SendMessageW(window, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            bool selection = GetFocus() == window || (style & ES_NOHIDESEL);
            bool caret = HideCaret(window);
            int first = style & ES_MULTILINE ? int(SendMessageW(window, EM_GETFIRSTVISIBLELINE, 0, 0)) : 0;
            int lines = int(SendMessageW(window, EM_GETLINECOUNT, 0, 0));
            TEXTMETRICW font_metrics{};
            GetTextMetricsW(dc, &font_metrics);
            for (int line = first; line < lines; ++line) {
                int index = int(SendMessageW(window, EM_LINEINDEX, line, 0));
                if (index < 0 || index > int(text.size()))
                    break;
                int length = int(SendMessageW(window, EM_LINELENGTH, index, 0));
                DWORD position = DWORD(SendMessageW(window, EM_POSFROMCHAR, index, 0));
                int x = short(LOWORD(position)), y = short(HIWORD(position));
                if (position == 0xffffffff) {
                    x = content.left;
                    y = content.top + (line - first) * font_metrics.tmHeight;
                }
                if (y >= content.bottom)
                    break;
                size_t last = std::min(text.size(), size_t(index) + size_t(std::max(0, length)));
                size_t from = index;
                while (from < last) {
                    bool selected = selection && from >= start && from < end;
                    size_t stop = last;
                    if (selection && start > from)
                        stop = std::min(stop, size_t(start));
                    if (selected)
                        stop = std::min(stop, size_t(end));
                    DWORD pos = DWORD(SendMessageW(window, EM_POSFROMCHAR, from, 0));
                    int left = pos == 0xffffffff ? x : short(LOWORD(pos));
                    SIZE extent{};
                    GetTextExtentPoint32W(dc, text.data() + from, int(stop - from), &extent);
                    RECT segment{left, y, left + extent.cx, y + font_metrics.tmHeight};
                    if (selected) {
                        fill_rect(dc, &segment, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                        SetTextColor(dc, RGB(255, 255, 255));
                    }
                    else
                        SetTextColor(dc, IsWindowEnabled(window) ? RGB(0, 0, 0) : RGB(128, 128, 128));
                    draw_text(dc, text.data() + from, int(stop - from), &segment, DT_SINGLELINE | DT_NOPREFIX);
                    from = stop;
                }
            }
            if (caret)
                ShowCaret(window);
        }
        RestoreDC(dc, saved);
        if (message == WM_PAINT)
            EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return TRUE;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, text_control_proc, id);
    auto result = DefSubclassProc(window, message, wp, lp);
    if (message == WM_SETTEXT || message == WM_SETFONT || message == WM_ENABLE || message == WM_SETFOCUS ||
        message == WM_KILLFOCUS || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR ||
        message == WM_LBUTTONUP || message == WM_MOUSEMOVE || message == WM_HSCROLL || message == WM_VSCROLL ||
        message == EM_SETSEL || message == EM_REPLACESEL || message == EM_LINESCROLL || message == WM_SIZE ||
        message == STM_SETICON)
        InvalidateRect(window, nullptr, FALSE);
    return result;
}
}

int fill_rect(HDC dc, const RECT* bounds, HBRUSH brush)
{
    if (!bounds)
        return 0;
    ROMAWGraphicsDrawing drawing{};
    drawing.operation = ROMAW_DRAW_FILL;
    drawing.bounds = {0, 0, bounds->right - bounds->left, bounds->bottom - bounds->top};
    if (draw_surface(dc, *bounds, drawing, brush_color(brush)))
        return 1;
    // Detached unit-test/control models have no initialized Toolbox. The
    // integrated Finder attaches its renderer before creating any windows.
    return shared_graphics() ? 0 : ::FillRect(dc, bounds, brush);
}

void paint_view(HWND list, HDC dc, void (*paint)(HDC, void*), void* context)
{
    if (!paint || redraw_suspended(list))
        return;
    RECT client{}, clip{}, bounds{};
    GetClientRect(list, &client);
    const int clip_kind = GetClipBox(dc, &clip);
    if (clip_kind == ERROR || clip_kind == NULLREGION || !IntersectRect(&bounds, &client, &clip))
        return;
    // Compose in view coordinates so icon positions and the desktop pattern
    // stay anchored. The view supplies its own background.
    DrawingSurface surface(dc, bounds, false);
    if (!surface.pixels || !SetViewportOrgEx(surface.dc, -bounds.left, -bounds.top, nullptr))
        return;
    try {
        paint(surface.dc, context);
        GdiFlush();
    }
    catch (...) {
        // Keep the previous complete view if model/raster allocation fails.
        // EndPaint is still balanced by the caller; partial pixels stay private.
        return;
    }
    int saved = SaveDC(dc);
    if (!saved)
        return;
    for (HWND child = GetWindow(list, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
            continue;
        RECT child_bounds{};
        GetWindowRect(child, &child_bounds);
        MapWindowPoints(nullptr, list, reinterpret_cast<POINT*>(&child_bounds), 2);
        ExcludeClipRect(dc, child_bounds.left, child_bounds.top, child_bounds.right, child_bounds.bottom);
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    BitBlt(dc, bounds.left, bounds.top, width, height, surface.dc, bounds.left, bounds.top, SRCCOPY);
    RestoreDC(dc, saved);
}

void fill_color(HDC dc, RECT bounds, COLORREF color)
{
    ROMAWGraphicsDrawing drawing{};
    drawing.operation = ROMAW_DRAW_FILL;
    drawing.bounds = {0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
    if (draw_surface(dc, bounds, drawing, color) || shared_graphics())
        return;
    auto brush = CreateSolidBrush(color);
    ::FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

int frame_rect(HDC dc, const RECT* bounds, HBRUSH brush)
{
    if (!bounds)
        return 0;
    ROMAWGraphicsDrawing drawing{};
    drawing.operation = ROMAW_DRAW_FRAME;
    drawing.pen_width = 1;
    drawing.bounds = {0, 0, bounds->right - bounds->left, bounds->bottom - bounds->top};
    if (draw_surface(dc, *bounds, drawing, brush_color(brush)))
        return 1;
    return shared_graphics() ? 0 : ::FrameRect(dc, bounds, brush);
}

void draw_line(HDC dc, int x, int y, int end_x, int end_y, COLORREF color)
{
    RECT bounds{std::min(x, end_x), std::min(y, end_y), std::max(x, end_x) + 1, std::max(y, end_y) + 1};
    ROMAWGraphicsDrawing drawing{};
    drawing.operation = ROMAW_DRAW_LINE;
    drawing.pen_width = 1;
    drawing.bounds = {x - bounds.left, y - bounds.top, x - bounds.left, y - bounds.top};
    drawing.end_x = end_x - bounds.left;
    drawing.end_y = end_y - bounds.top;
    if (draw_surface(dc, bounds, drawing, color) || shared_graphics())
        return;
    auto pen = CreatePen(PS_SOLID, 1, color);
    auto old = SelectObject(dc, pen);
    MoveToEx(dc, x, y, nullptr);
    LineTo(dc, end_x, end_y);
    ::SetPixelV(dc, end_x, end_y, color);
    SelectObject(dc, old);
    DeleteObject(pen);
}

BOOL paint_pixel(HDC dc, int x, int y, COLORREF color)
{
    fill_color(dc, {x, y, x + 1, y + 1}, color);
    return TRUE;
}

BOOL draw_round_rect(HDC dc, int left, int top, int right, int bottom, int oval_width, int oval_height)
{
    if (!shared_graphics())
        return ::RoundRect(dc, left, top, right, bottom, oval_width, oval_height);
    ROMAWGraphicsDrawing drawing{};
    drawing.operation = ROMAW_DRAW_ROUND_RECT;
    drawing.bounds = {0, 0, right - left, bottom - top};
    drawing.oval_width = oval_width;
    drawing.oval_height = oval_height;
    RECT bounds{left, top, right, bottom};
    LOGBRUSH brush{};
    GetObjectW(GetCurrentObject(dc, OBJ_BRUSH), sizeof brush, &brush);
    if (brush.lbStyle != BS_HOLLOW) {
        drawing.filled = 1;
        if (!draw_surface(dc, bounds, drawing, brush.lbColor))
            return FALSE;
    }
    LOGPEN pen{};
    GetObjectW(GetCurrentObject(dc, OBJ_PEN), sizeof pen, &pen);
    if (pen.lopnStyle != PS_NULL) {
        drawing.filled = 0;
        drawing.pen_width = std::clamp<LONG>(pen.lopnWidth.x, 1, 128);
        if (!draw_surface(dc, bounds, drawing, pen.lopnColor))
            return FALSE;
    }
    return TRUE;
}

BOOL draw_polygon(HDC dc, const POINT* points, int count)
{
    if (!points || count < 3 || count > 4096)
        return FALSE;
    if (!shared_graphics())
        return ::Polygon(dc, points, count);
    RECT bounds{points[0].x, points[0].y, points[0].x + 1, points[0].y + 1};
    for (int i = 1; i < count; ++i) {
        bounds.left = std::min(bounds.left, points[i].x);
        bounds.top = std::min(bounds.top, points[i].y);
        bounds.right = std::max(bounds.right, points[i].x + 1);
        bounds.bottom = std::max(bounds.bottom, points[i].y + 1);
    }
    std::vector<ROMAWGraphicsPoint> local;
    for (int i = 0; i < count; ++i)
        local.push_back({points[i].x - bounds.left, points[i].y - bounds.top});
    ROMAWGraphicsDrawing drawing{};
    drawing.operation = ROMAW_DRAW_POLYGON;
    drawing.points = local.data();
    drawing.point_count = local.size();
    drawing.filled = 1;
    return draw_surface(dc, bounds, drawing, brush_color(static_cast<HBRUSH>(GetCurrentObject(dc, OBJ_BRUSH))));
}

BOOL draw_pixels(HDC dc, RECT bounds, const uint32_t* pixels, size_t bytes)
{
    ROMAWGraphicsDrawing drawing{};
    drawing.operation = ROMAW_DRAW_IMAGE;
    drawing.image = pixels;
    drawing.image_stride = size_t(std::max(0L, bounds.right - bounds.left)) * 4;
    drawing.image_bytes = bytes;
    return draw_surface(dc, bounds, drawing, 0);
}

// Read an HICON once, including the legacy AND mask for icons without alpha.
static bool read_icon(HICON icon, std::vector<uint32_t>& source, int& source_width, int& source_height)
{
    ICONINFO info{};
    if (!GetIconInfo(icon, &info))
        return FALSE;

    struct OwnedIconBits {
        ICONINFO& info;
        HDC dc = CreateCompatibleDC(nullptr);

        ~OwnedIconBits()
        {
            if (info.hbmColor)
                DeleteObject(info.hbmColor);
            if (info.hbmMask)
                DeleteObject(info.hbmMask);
            if (dc)
                DeleteDC(dc);
        }
    } owned{info};

    BITMAP bitmap{};
    if (!owned.dc || !GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof bitmap, &bitmap))
        return FALSE;
    source_width = bitmap.bmWidth;
    source_height = info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
    if (source_width < 1 || source_height < 1 || source_width > 512 || source_height > 512)
        return FALSE;
    BITMAPINFO color{};
    color.bmiHeader.biSize = sizeof color.bmiHeader;
    color.bmiHeader.biWidth = source_width;
    color.bmiHeader.biHeight = -source_height;
    color.bmiHeader.biPlanes = 1;
    color.bmiHeader.biBitCount = 32;
    source.resize(size_t(source_width) * source_height);
    if (info.hbmColor &&
        GetDIBits(owned.dc, info.hbmColor, 0, source_height, source.data(), &color, DIB_RGB_COLORS) != source_height)
        return FALSE;
    bool alpha = std::any_of(source.begin(), source.end(), [](uint32_t pixel) { return pixel >> 24; });
    if (!alpha) {
        struct MaskInfo {
            BITMAPINFOHEADER header;
            RGBQUAD colors[2];
        } mask{};

        mask.header = color.bmiHeader;
        int mask_height = source_height * (info.hbmColor ? 1 : 2);
        mask.header.biHeight = -mask_height;
        mask.header.biBitCount = 1;
        size_t stride = (source_width + 31) / 32 * 4;
        std::vector<unsigned char> bits(stride * mask_height);
        if (!info.hbmMask || GetDIBits(owned.dc, info.hbmMask, 0, mask_height, bits.data(),
                                       reinterpret_cast<BITMAPINFO*>(&mask), DIB_RGB_COLORS) != mask_height)
            return FALSE;
        for (int row = 0; row < source_height; ++row)
            for (int column = 0; column < source_width; ++column) {
                unsigned bit = 0x80 >> (column & 7);
                auto& pixel = source[size_t(row) * source_width + column];
                bool transparent = bits[size_t(row) * stride + column / 8] & bit;
                if (!info.hbmColor)
                    pixel = bits[size_t(row + source_height) * stride + column / 8] & bit ? 0xffffff : 0;
                pixel = transparent ? 0 : pixel | 0xff000000;
            }
    }
    return true;
}

bool save_icon_file(HICON icon, const wchar_t* path)
{
    int width, height;
    std::vector<uint32_t> source;
    if (!read_icon(icon, source, width, height) || width > 256 || height > 256)
        return false;

    // A single ICO directory entry points at a bottom-up 32-bit DIB and AND mask.
    constexpr size_t image_offset = 22;
    const size_t mask_stride = (width + 31) / 32 * 4;
    const size_t pixel_stride = size_t(width) * 4;
    const DWORD image_size = DWORD(sizeof(BITMAPINFOHEADER) + (pixel_stride + mask_stride) * height);
    std::vector<unsigned char> bytes(image_offset + image_size);
    bytes[2] = 1; // Icon file.
    bytes[4] = 1; // One image.
    bytes[6] = static_cast<unsigned char>(width);
    bytes[7] = static_cast<unsigned char>(height);
    bytes[10] = 1; // One plane.
    bytes[12] = 32;
    memcpy(bytes.data() + 14, &image_size, sizeof image_size);
    bytes[18] = image_offset;
    BITMAPINFOHEADER dib{};
    dib.biSize = sizeof dib;
    dib.biWidth = width;
    dib.biHeight = height * 2;
    dib.biPlanes = 1;
    dib.biBitCount = 32;
    memcpy(bytes.data() + image_offset, &dib, sizeof dib);
    auto* pixels = bytes.data() + image_offset + sizeof dib;
    auto* mask = pixels + pixel_stride * height;
    for (int row = 0; row < height; ++row) {
        const auto* from = source.data() + size_t(height - 1 - row) * width;
        memcpy(pixels + size_t(row) * pixel_stride, from, pixel_stride);
        for (int col = 0; col < width; ++col)
            if ((from[col] >> 24) == 0)
                mask[size_t(row) * mask_stride + col / 8] |= 0x80 >> (col % 8);
    }
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written;
    bool saved = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    if (!saved)
        DeleteFileW(path);
    return saved;
}

BOOL draw_icon(HDC dc, int x, int y, HICON icon, int width, int height, UINT frame, HBRUSH background, UINT flags)
{
    if (!shared_graphics())
        return ::DrawIconEx(dc, x, y, icon, width, height, frame, background, flags);
    if (!icon || width < 1 || height < 1 || width > 512 || height > 512)
        return FALSE;
    int source_width, source_height;
    std::vector<uint32_t> source;
    if (!read_icon(icon, source, source_width, source_height))
        return FALSE;
    std::vector<uint32_t> scaled(size_t(width) * height);
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column)
            scaled[size_t(row) * width + column] =
                source[size_t(row * source_height / height) * source_width + column * source_width / width];
    if (background)
        fill_color(dc, {x, y, x + width, y + height}, brush_color(background));
    return draw_pixels(dc, {x, y, x + width, y + height}, scaled.data(), scaled.size() * 4);
}

BOOL draw_image(HIMAGELIST images, int index, HDC dc, int x, int y, int width, int height)
{
    HICON icon = ImageList_GetIcon(images, index, ILD_TRANSPARENT);
    if (!icon)
        return FALSE;
    BOOL result = draw_icon(dc, x, y, icon, width, height);
    DestroyIcon(icon);
    return result;
}

BOOL draw_bitmap(HDC dc, RECT bounds, HBITMAP bitmap)
{
    BITMAP source{};
    if (!GetObjectW(bitmap, sizeof source, &source) || source.bmWidth < 1 || source.bmHeight < 1 ||
        source.bmWidth > 16384 || source.bmHeight > 16384 ||
        uint64_t(source.bmWidth) * source.bmHeight > 64 * 1024 * 1024)
        return FALSE;
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width < 1 || height < 1 || width > 16384 || height > 16384 || uint64_t(width) * height > 64 * 1024 * 1024)
        return FALSE;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = source.bmWidth;
    info.bmiHeader.biHeight = -source.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<uint32_t> pixels(size_t(source.bmWidth) * source.bmHeight), scaled(size_t(width) * height);
    if (GetDIBits(dc, bitmap, 0, source.bmHeight, pixels.data(), &info, DIB_RGB_COLORS) != source.bmHeight)
        return FALSE;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            scaled[size_t(y) * width + x] =
                pixels[size_t(y * source.bmHeight / height) * source.bmWidth + x * source.bmWidth / width] | 0xff000000;
    return draw_pixels(dc, bounds, scaled.data(), scaled.size() * 4);
}

HBITMAP drag_artwork(HWND list, HIMAGELIST images, int image, int item, const wchar_t* name, POINT& origin)
{
    if (!shared_graphics() || !name)
        return nullptr;
    RECT icon{}, caption{};
    int icon_width = 0, icon_height = 0;
    if (!ListView_GetItemRect(list, item, &icon, LVIR_ICON) ||
        !ListView_GetItemRect(list, item, &caption, LVIR_LABEL) ||
        !ImageList_GetIconSize(images, &icon_width, &icon_height))
        return nullptr;
    icon.left += (icon.right - icon.left - icon_width) / 2;
    icon.right = icon.left + icon_width;
    icon.bottom = icon.top + icon_height;
    HDC dc = GetDC(list);
    if (!dc)
        return nullptr;
    auto old_font = SelectObject(dc, small_font());
    if (ListView_GetView(list) == LV_VIEW_ICON)
        caption = icon_label_bounds(dc, name, caption);
    else {
        RECT size{};
        draw_text(dc, name, -1, &size, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        caption.right = caption.left + size.right;
        caption.bottom = caption.top + size.bottom;
    }
    SelectObject(dc, old_font);
    RECT bounds{};
    UnionRect(&bounds, &icon, &caption);
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width < 1 || height < 1 || width > 4096 || height > 4096) {
        ReleaseDC(list, dc);
        return nullptr;
    }
    DrawingSurface black(dc, {0, 0, width, height}), white(dc, {0, 0, width, height});
    ReleaseDC(list, dc);
    if (!black.pixels || !white.pixels)
        return nullptr;
    std::fill_n(black.pixels, size_t(width) * height, 0);
    std::fill_n(white.pixels, size_t(width) * height, 0xffffff);
    OffsetRect(&icon, -bounds.left, -bounds.top);
    OffsetRect(&caption, -bounds.left, -bounds.top);
    for (HDC canvas : {black.dc, white.dc}) {
        SelectObject(canvas, small_font());
        SetTextColor(canvas, RGB(0, 0, 0));
        SetBkMode(canvas, TRANSPARENT);
        if (!draw_image(images, image, canvas, icon.left, icon.top, icon_width, icon_height))
            return nullptr;
        RECT text = caption;
        draw_text(canvas, name, -1, &text,
                  DT_SINGLELINE | DT_NOPREFIX | (ListView_GetView(list) == LV_VIEW_ICON ? DT_CENTER : DT_LEFT));
    }
    GdiFlush();
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        unsigned difference = 0;
        for (unsigned shift : {0u, 8u, 16u})
            difference = std::max(difference, unsigned(std::max(0, int((white.pixels[i] >> shift) & 255) -
                                                                       int((black.pixels[i] >> shift) & 255))));
        unsigned alpha = 255 - difference;
        uint32_t pixel = alpha << 24;
        for (unsigned shift : {0u, 8u, 16u})
            pixel |= std::min(alpha, (black.pixels[i] >> shift) & 255) << shift;
        black.pixels[i] = pixel;
    }
    origin = {bounds.left, bounds.top};
    SelectObject(black.dc, black.previous);
    black.previous = nullptr;
    auto result = black.bitmap;
    black.bitmap = nullptr;
    return result;
}

// Classic fonts intentionally remain MacRoman. Windows may supply glyph masks
// for other scripts/fonts, but those masks enter the same QuickDraw image path;
// host text drawing never writes directly over Finder's completed surfaces.
static int draw_host_glyphs(HDC dc, const wchar_t* text, int length, RECT* bounds, UINT flags)
{
    if (!shared_graphics())
        return ::DrawTextW(dc, text, length, bounds, flags);
    if (!bounds || !text)
        return 0;
    if (flags & DT_CALCRECT) {
        HDC measure = CreateCompatibleDC(dc);
        if (!measure)
            return 0;
        auto font = SelectObject(measure, GetCurrentObject(dc, OBJ_FONT));
        int height = ::DrawTextW(measure, text, length, bounds, flags);
        SelectObject(measure, font);
        DeleteDC(measure);
        return height;
    }
    DrawingSurface mask(dc, *bounds);
    if (!mask.pixels)
        return 0;
    int width = bounds->right - bounds->left, height = bounds->bottom - bounds->top;
    std::fill_n(mask.pixels, size_t(width) * height, 0);
    auto font = SelectObject(mask.dc, GetCurrentObject(dc, OBJ_FONT));
    SetBkMode(mask.dc, TRANSPARENT);
    SetTextColor(mask.dc, RGB(255, 255, 255));
    RECT local{0, 0, width, height};
    int result = ::DrawTextW(mask.dc, text, length, &local, flags);
    SelectObject(mask.dc, font);
    GdiFlush();
    COLORREF color = GetTextColor(dc);
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        unsigned alpha = std::max({(mask.pixels[i] >> 16) & 255, (mask.pixels[i] >> 8) & 255, mask.pixels[i] & 255});
        mask.pixels[i] = (alpha << 24) | ((GetRValue(color) * alpha / 255) << 16) |
                         ((GetGValue(color) * alpha / 255) << 8) | (GetBValue(color) * alpha / 255);
    }
    return draw_pixels(dc, *bounds, mask.pixels, size_t(width) * height * 4) ? result : 0;
}

static int draw_text_impl(HDC dc, const wchar_t* original, int length, RECT* bounds, UINT flags)
{
    const auto* graphics = shared_graphics();
    if (!graphics || !bounds || !original)
        return draw_host_glyphs(dc, original, length, bounds, flags);
    if (length < 0)
        length = int(wcslen(original));
    if (!length)
        return 0;
    std::wstring text(original, length);
    if (!(flags & DT_NOPREFIX)) {
        std::wstring caption;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'&') {
                if (i + 1 < text.size() && text[i + 1] == L'&')
                    ++i;
                else
                    continue;
            }
            caption += text[i];
        }
        text = std::move(caption);
    }
    if (flags & DT_SINGLELINE)
        for (auto& character : text)
            if (character == L'\r' || character == L'\n')
                character = L' ';
    std::string encoded;
    if (!encode_text(text, encoded))
        return draw_host_glyphs(dc, original, length, bounds, flags);
    LOGFONTW font{};
    GetObjectW(GetCurrentObject(dc, OBJ_FONT), sizeof font, &font);
    if (_wcsicmp(font.lfFaceName, L"Chicago") && _wcsicmp(font.lfFaceName, L"Geneva"))
        return draw_host_glyphs(dc, original, length, bounds, flags);
    int family = _wcsicmp(font.lfFaceName, L"Chicago") == 0 ? 0 : 3;
    int size = std::clamp(std::abs(int(font.lfHeight)), 1, 128);
    int face = (font.lfWeight >= FW_BOLD ? 1 : 0) | (font.lfItalic ? 2 : 0) | (font.lfUnderline ? 4 : 0);
    int width = flags & DT_CALCRECT && flags & DT_SINGLELINE ? 16384 : std::max(1L, bounds->right - bounds->left);
    int height = std::max(1L, bounds->bottom - bounds->top);
    if (width > 16384 || height > 16384 || uint64_t(width) * height > 4 * 1024 * 1024)
        return draw_host_glyphs(dc, original, length, bounds, flags);
    COLORREF color = GetTextColor(dc);
    TextKey key{text, width, height, family, size, face, flags, color};
    auto found = text_cache.find(key);
    if (found == text_cache.end()) {
        ROMAWGraphicsRequest request{};
        request.size = sizeof request;
        request.kind = ROMAW_GRAPHICS_TEXT;
        request.key = 0x46494e4454455854ULL;
        request.width = width;
        request.height = height;
        request.text = reinterpret_cast<const unsigned char*>(encoded.data());
        request.text_length = encoded.size();
        request.font = family;
        request.font_size = size;
        request.font_style = face;
        request.foreground = 0xffffff;
        request.background = 0;
        request.text_flags =
            (flags & DT_CENTER ? ROMAW_TEXT_CENTER : 0) | (flags & DT_RIGHT ? ROMAW_TEXT_RIGHT : 0) |
            (flags & DT_VCENTER ? ROMAW_TEXT_VCENTER : 0) | (flags & DT_BOTTOM ? ROMAW_TEXT_BOTTOM : 0) |
            (!(flags & DT_SINGLELINE) && flags & DT_WORDBREAK ? ROMAW_TEXT_WRAP : 0) |
            (flags & (DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS) ? ROMAW_TEXT_ELLIPSIS : 0);
        auto raster = std::make_unique<TextRaster>();
        bool measure = flags & DT_CALCRECT;
        std::vector<uint32_t> pixels(measure ? 0 : size_t(width) * height);
        int error = graphics->render(&request, measure ? nullptr : pixels.data(), size_t(width) * 4, &raster->metrics);
        graphics->release(request.key);
        if (error)
            return draw_host_glyphs(dc, original, length, bounds, flags);
        if (!measure) {
            raster->pixels.resize(pixels.size());
            for (size_t i = 0; i < pixels.size(); ++i) {
                unsigned alpha = std::max({(pixels[i] >> 16) & 255, (pixels[i] >> 8) & 255, pixels[i] & 255});
                raster->pixels[i] = (alpha << 24) | ((GetRValue(color) * alpha / 255) << 16) |
                                    ((GetGValue(color) * alpha / 255) << 8) | (GetBValue(color) * alpha / 255);
            }
        }
        size_t bytes = measure ? 0 : size_t(width) * height * 4;
        if (text_cache.size() >= 256 || cache_bytes + bytes > 8 * 1024 * 1024) {
            text_cache.clear();
            cache_bytes = 0;
        }
        cache_bytes += bytes;
        found = text_cache.emplace(std::move(key), std::move(raster)).first;
    }
    if (flags & DT_CALCRECT) {
        bounds->right = bounds->left + found->second->metrics.text_width;
        bounds->bottom = bounds->top + found->second->metrics.text_height;
    }
    else if (!found->second->pixels.empty())
        draw_pixels(dc, {bounds->left, bounds->top, bounds->left + width, bounds->top + height},
                    found->second->pixels.data(), found->second->pixels.size() * 4);
    return found->second->metrics.text_height;
}

// Allocation failures must not escape a Win32 window procedure. Names retain
// their Unicode spelling; unavailable classic glyphs use offscreen host masks.
int draw_text(HDC dc, const wchar_t* text, int length, RECT* bounds, UINT flags)
{
    try {
        return draw_text_impl(dc, text, length, bounds, flags);
    }
    catch (...) {
        // Allocation failure must not escape a Win32 window procedure.
        return 0;
    }
}

RECT icon_label_bounds(HDC dc, const wchar_t* text, RECT bounds, const RECT* viewport)
{
    RECT measured{};
    draw_text(dc, text, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    // Native cells can be narrower and shorter than their QuickDraw text.
    // Keep the icon's center, with one clear pixel beside the complete name;
    // the viewport, rather than a neighboring icon cell, clips the result.
    LONG center = bounds.left + (bounds.right - bounds.left) / 2;
    LONG width = std::max(1L, measured.right - measured.left) + 2;
    bounds.left = center - width / 2;
    if (viewport && center >= viewport->left && center < viewport->right)
        // Keep an edge icon's complete name visible whenever it fits. Names
        // wider than the window start at its left edge and remain single-line.
        bounds.left = std::clamp(bounds.left, viewport->left, std::max(viewport->left, viewport->right - width));
    bounds.right = bounds.left + width;
    bounds.bottom = bounds.top + std::max(1L, measured.bottom - measured.top);
    return bounds;
}

void style_text_control(HWND window, bool edit_frame)
{
    wchar_t name[32]{};
    GetClassNameW(window, name, std::size(name));
    if (_wcsicmp(name, L"BUTTON") == 0) {
        auto kind = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
        if (kind == BS_AUTOCHECKBOX || kind == BS_CHECKBOX || kind == BS_AUTORADIOBUTTON || kind == BS_RADIOBUTTON)
            style_checkbox(window);
        else if (kind == BS_GROUPBOX) {
            SetWindowSubclass(window, text_control_proc, 5, 4);
            InvalidateRect(window, nullptr, TRUE);
        }
        return;
    }
    if (_wcsicmp(name, WC_HEADERW) == 0) {
        SetWindowSubclass(window, header_proc, 6, 0);
        InvalidateRect(window, nullptr, TRUE);
        return;
    }
    bool edit = _wcsicmp(name, L"EDIT") == 0;
    if (!edit && _wcsicmp(name, L"STATIC") != 0)
        return;
    if (!edit) {
        auto kind = GetWindowLongPtrW(window, GWL_STYLE) & SS_TYPEMASK;
        if (kind != SS_LEFT && kind != SS_RIGHT && kind != SS_CENTER && kind != SS_SIMPLE &&
            kind != SS_LEFTNOWORDWRAP && kind != SS_ICON)
            return;
    }
    if (edit && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_CLIENTEDGE)) {
        SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
        SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_BORDER);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    SetWindowSubclass(window, text_control_proc, 5,
                      edit                                                              ? (edit_frame ? 5 : 1)
                      : (GetWindowLongPtrW(window, GWL_STYLE) & SS_TYPEMASK) == SS_ICON ? 2
                                                                                        : 0);
    InvalidateRect(window, nullptr, TRUE);
}

void finish_text()
{
    text_cache.clear();
    cache_bytes = 0;
    if (auto* graphics = shared_graphics())
        graphics->release(0x46494e4444524157ULL);
}
}
