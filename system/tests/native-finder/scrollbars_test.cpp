/*+===================================================================
  File:      scrollbars_test.cpp

  Summary:   Exercise native list layout and the production chrome painter on an owned bitmap. Scrollbar pixels must
             never enter the surface copied by icon scrolling.

  Classes:   Used: WaitingCursor

  Functions: check, check_buttons, check_content_zoom, check_desktop_repaint, check_frame, check_grow_corner,
             check_zoomed_layout, click_frame_button, drag_thumb, frame_probe, frame_resource, main, matches_icon,
             press_frame_button, zoom_layout

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Exercise native list layout and the production chrome painter on an owned
// bitmap. Scrollbar pixels must never enter the surface copied by icon scrolling.
#include "finder/classic.h"
#include "rsys/icon_color.h"
#include <commctrl.h>
#include <algorithm>
#include <iostream>
#include "scrollbar_art.inc"
#include "chrome_art.inc"
#include "desktop_pattern.inc"

bool tested_active = false, tested_inactive = false;

// Compare the production rectangle against the actual shared icon payload.
// Exclude its outer frame, which the scrollbar painter adds after the artwork.
bool matches_icon(HDC actual, RECT box, unsigned image)
{
    int width = box.right - box.left, height = box.bottom - box.top;
    HDC expected = CreateCompatibleDC(actual);
    HBITMAP bitmap = CreateCompatibleBitmap(actual, width, height);
    auto icon = CreateIconFromResourceEx(const_cast<BYTE*>(scrollbar_resources[image].data),
                                         scrollbar_resources[image].size, TRUE, 0x00030000, 16, 16, 0);
    if (!expected || !bitmap || !icon) {
        if (expected)
            DeleteDC(expected);
        if (bitmap)
            DeleteObject(bitmap);
        if (icon)
            DestroyIcon(icon);
        return false;
    }
    auto previous = SelectObject(expected, bitmap);
    auto brush = CreateSolidBrush(RGB(221, 221, 221));
    RECT bounds{0, 0, width, height};
    FillRect(expected, &bounds, brush);
    int size = 16;
    DrawIconEx(expected, image == 10 ? 1 : (width - size) / 2, image == 10 ? 1 : (height - size) / 2, icon, size, size,
               0, nullptr, DI_NORMAL);
    bool good = true;
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
            good = good && GetPixel(actual, box.left + x, box.top + y) == GetPixel(expected, x, y);
    if (!good)
        std::cout << "Sprite mismatch: " << image << " at " << box.left << "," << box.top << " size " << width << "x"
                  << height << std::endl;
    SelectObject(expected, previous);
    DeleteObject(brush);
    DeleteObject(bitmap);
    DeleteDC(expected);
    DestroyIcon(icon);
    return good;
}

// Verify both reserved gutters and every client pixel after a layout operation.
bool check(HWND list, const char* step, int pressed_direction = -1)
{
    RECT client{}, window{};
    GetClientRect(list, &client);
    MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&client), 2);
    GetWindowRect(finder::classic::scrollbar_window(list), &window);
    OffsetRect(&client, -window.left, -window.top);
    int width = window.right - window.left;
    int height = window.bottom - window.top;
    bool good = width - client.right == 16 && height - client.bottom == 16;
    if (!good)
        std::cout << "Unexpected gutters: " << width - client.right << ", " << height - client.bottom << std::endl;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        return false;
    }
    auto previous = SelectObject(dc, bitmap);
    auto* pixels = static_cast<unsigned*>(bits);
    constexpr unsigned marker = 0x00345678;
    std::fill(pixels, pixels + width * height, marker);
    finder::classic::paint_list_scrollbars(list, dc);
    GdiFlush();
    for (int y = client.top; y < client.bottom; ++y)
        for (int x = client.left; x < client.right; ++x)
            good = good && pixels[y * width + x] == marker;
    for (LONG object : {OBJID_VSCROLL, OBJID_HSCROLL}) {
        bool vertical = object == OBJID_VSCROLL;
        RECT area = vertical ? RECT{client.right, client.top, width, client.bottom}
                             : RECT{client.left, client.bottom, client.right, height};
        SCROLLINFO range{};
        range.cbSize = sizeof range;
        range.fMask = SIF_ALL;
        GetScrollInfo(list, vertical ? SB_VERT : SB_HORZ, &range);
        long long maximum =
            std::max<long long>(range.nMin, static_cast<long long>(range.nMax) - std::max(1u, range.nPage) + 1);
        bool enabled =
            (GetWindowLongPtrW(list, GWL_STYLE) & (vertical ? WS_VSCROLL : WS_HSCROLL)) && maximum > range.nMin;
        tested_active |= enabled;
        tested_inactive |= !enabled;
        RECT first = area, last = area, thumb = area;
        int size = 16;
        int travel = (vertical ? area.bottom - area.top : area.right - area.left) - size * 2 - 16;
        int offset =
            enabled ? int((range.nPos - static_cast<long long>(range.nMin)) * travel / (maximum - range.nMin)) : 0;
        if (vertical) {
            first.bottom = first.top + size;
            last.top = last.bottom - size;
            thumb.top = first.bottom + offset;
            thumb.bottom = thumb.top + 16;
        }
        else {
            first.right = first.left + size;
            last.left = last.right - size;
            thumb.left = first.right + offset;
            thumb.right = thumb.left + 16;
        }
        for (unsigned end = 0; end < 2; ++end) {
            bool inactive = !enabled;
            unsigned direction = vertical ? end : 2 + end;
            unsigned base = inactive ? 4 : int(direction) == pressed_direction ? 11 : 0;
            good = matches_icon(dc, end ? last : first, direction + base) && good;
        }
        if (enabled)
            good = matches_icon(dc, thumb, vertical ? 9 : 8) && good;
        // Inspect a full pattern period outside arrows/thumb, not just whether
        // a track pixel was painted. Reversed GDI colours make it much darker.
        int dark = 0, light = 0;
        for (int y = area.top + 1; y < area.bottom - 1; ++y) {
            for (int x = area.left + 1; x < area.right - 1; ++x) {
                POINT point{x, y};
                if (PtInRect(&first, point) || PtInRect(&last, point) || (enabled && PtInRect(&thumb, point)))
                    continue;
                COLORREF color = GetPixel(dc, x, y);
                if (enabled) {
                    dark += color == RGB(115, 113, 115);
                    light += color == RGB(222, 218, 222);
                    good = good && (color == RGB(115, 113, 115) || color == RGB(222, 218, 222));
                }
                else
                    good = good && color == RGB(222, 218, 222);
            }
        }
        if (enabled && dark + light > 0)
            good = good && std::abs(light - 3 * dark) <= 8;
        // The light inner bevel separates each gutter from the parent's one
        // outline/shadow pair, including the arrow ends.
        for (int pixel = (vertical ? area.top : area.left) + 1; pixel < (vertical ? area.bottom : area.right) - 1;
             ++pixel)
            good = good && GetPixel(dc, vertical ? area.right - 1 : pixel, vertical ? pixel : area.bottom - 1) ==
                               RGB(238, 238, 238);
    }
    RECT grow_icon{client.right, client.bottom, client.right + 16, client.bottom + 16};
    good = matches_icon(dc, grow_icon, 10) && good;
    good = good && GetPixel(dc, client.right, client.bottom) == RGB(0, 0, 0);
    for (int pixel = 1; pixel < 16; ++pixel)
        good = good && GetPixel(dc, client.right + 15, client.bottom + pixel) == RGB(238, 238, 238) &&
               GetPixel(dc, client.right + pixel, client.bottom + 15) == RGB(238, 238, 238);
    // Both disabled and active scrollbar tracks must actually be painted.
    good = good && pixels[(client.top + 2) * width + client.right + 2] != marker &&
           pixels[(client.bottom + 2) * width + client.left + 2] != marker;
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    std::cout << step << ": " << (good ? "passed" : "FAILED") << std::endl;
    return good;
}

// Drive the real nonclient tracker in hidden fixture windows. Verify the list
// view's position, rather than merely checking the painted thumb rectangle.
bool drag_thumb(HWND list, bool vertical, int numerator, int denominator)
{
    RECT client{}, window{};
    GetClientRect(list, &client);
    MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&client), 2);
    GetWindowRect(finder::classic::scrollbar_window(list), &window);
    SCROLLINFO range{};
    range.cbSize = sizeof range;
    range.fMask = SIF_ALL;
    GetScrollInfo(list, vertical ? SB_VERT : SB_HORZ, &range);
    int maximum = range.nMax - std::max(1u, range.nPage) + 1;
    int start = (vertical ? client.top : client.left) + 16;
    int travel = (vertical ? client.bottom - client.top : client.right - client.left) - 16 * 2 - 16;
    if (maximum <= range.nMin || travel <= 0)
        return false;
    int current = start + int((static_cast<long long>(range.nPos) - range.nMin) * travel / (maximum - range.nMin));
    POINT grab = vertical ? POINT{client.right + 8, current + 5} : POINT{current + 5, client.bottom + 8};
    SendMessageW(list, WM_NCLBUTTONDOWN, vertical ? HTVSCROLL : HTHSCROLL, MAKELPARAM(grab.x, grab.y));
    bool good = GetCapture() == list;
    POINT target = grab;
    int distance = travel * numerator / denominator;
    if (vertical)
        target.y = start + distance + 5;
    else
        target.x = start + distance + 5;
    ScreenToClient(list, &target);
    SendMessageW(list, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(target.x, target.y));
    SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(target.x, target.y));
    GetScrollInfo(list, vertical ? SB_VERT : SB_HORZ, &range);
    int expected = range.nMin + int(static_cast<long long>(maximum - range.nMin) * distance / travel);
    good = good && GetCapture() != list && std::abs(range.nPos - expected) <= 1;
    std::cout << "Thumb view " << ListView_GetView(list) << (vertical ? " vertical" : " horizontal") << ": "
              << range.nPos << " expected " << expected << std::endl;
    return good;
}

// Arrow repeat must stop on cancellation/capture loss, and page clicks must
// advance the list without entering Windows' proportional-thumb tracker.
bool check_buttons(HWND list, HWND parent, bool vertical)
{
    RECT client;
    GetClientRect(list, &client);
    MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&client), 2);
    POINT arrow = vertical ? POINT{client.right + 8, client.bottom - 8} : POINT{client.right - 8, client.bottom + 8};
    auto position = [&] {
        SCROLLINFO range{};
        range.cbSize = sizeof range;
        range.fMask = SIF_POS;
        GetScrollInfo(list, vertical ? SB_VERT : SB_HORZ, &range);
        return range.nPos;
    };
    auto down = [&](POINT point) {
        SendMessageW(finder::classic::scrollbar_window(list), WM_NCLBUTTONDOWN, vertical ? HTVSCROLL : HTHSCROLL,
                     MAKELPARAM(point.x, point.y));
    };
    int before = position();
    down(arrow);
    bool good = GetCapture() == list && position() > before;
    good = check(list, "Pressed arrow sprite", vertical ? 1 : 3) && good;
    before = position();
    SendMessageW(list, WM_TIMER, 0x464e4453, 0);
    good = good && position() > before;
    SendMessageW(list, WM_CANCELMODE, 0, 0);
    before = position();
    SendMessageW(list, WM_TIMER, 0x464e4453, 0);
    good = good && GetCapture() != list && position() == before;
    good = check(list, "Cancelled arrow sprite") && good;
    down(arrow);
    SetCapture(parent);
    before = position();
    SendMessageW(list, WM_TIMER, 0x464e4453, 0);
    good = good && position() == before;
    ReleaseCapture();
    POINT page = arrow;
    if (vertical)
        page.y -= 16;
    else
        page.x -= 16;
    down(page);
    good = good && position() > before;
    SendMessageW(list, WM_LBUTTONUP, 0, 0);
    good = good && GetCapture() != list;
    std::cout << (vertical ? "Vertical" : "Horizontal")
              << " arrow/repeat/page/cancel/capture: " << (good ? "passed" : "FAILED") << std::endl;
    return good;
}

unsigned default_frame_paints = 0;

// Load the actual RGBA zoom resource, including its transparent edge margins.
HICON frame_resource(const unsigned char* rgba)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 16;
    info.bmiHeader.biHeight = -16;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color)
        return nullptr;
    auto* pixels = static_cast<unsigned char*>(bits);
    unsigned char mask[32]{};
    for (unsigned i = 0; i < 256; ++i) {
        unsigned alpha = rgba[i * 4 + 3];
        for (unsigned channel = 0; channel < 3; ++channel)
            pixels[i * 4 + channel] = rgba[i * 4 + 2 - channel] * alpha / 255;
        pixels[i * 4 + 3] = alpha;
        if (!alpha)
            mask[i / 8] |= 0x80 >> (i % 8);
    }
    HBITMAP mono = CreateBitmap(16, 16, 1, 1, mask);
    ICONINFO icon{};
    icon.fIcon = TRUE;
    icon.hbmColor = color;
    icon.hbmMask = mono;
    HICON result = mono ? CreateIconIndirect(&icon) : nullptr;
    DeleteObject(mono);
    DeleteObject(color);
    return result;
}

POINT press_frame_button(HWND window, UINT part);

LRESULT CALLBACK frame_probe(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (message == WM_NCPAINT)
        ++default_frame_paints;
    return DefSubclassProc(window, message, wp, lp);
}

// A custom child frame must consume native frame painting, and its own painter
// must not touch any client pixels, including the list's nonclient gutters.
bool check_frame(HWND window)
{
    HICON zoom = frame_resource(chrome_zoom_rgba), clicked = frame_resource(chrome_close_clicked_rgba);
    if (!zoom || !clicked)
        return false;
    finder::classic::chrome_art(nullptr, zoom, nullptr, clicked);
    SetWindowSubclass(window, frame_probe, 44, 0);
    finder::classic::decorate(window);
    finder::classic::activate(window);
    default_frame_paints = 0;
    SendMessageW(window, WM_NCPAINT, 1, 0);
    bool good = default_frame_paints == 0;
    RECT bounds, client;
    GetWindowRect(window, &bounds);
    GetClientRect(window, &client);
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&client), 2);
    OffsetRect(&client, -bounds.left, -bounds.top);
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc)
        return false;
    auto previous = SelectObject(dc, bitmap);
    auto* pixels = static_cast<unsigned*>(bits);
    constexpr unsigned marker = 0x00345678;
    std::fill(pixels, pixels + width * height, marker);
    finder::classic::paint_window_chrome(window, dc);
    GdiFlush();
    for (int y = client.top; y < client.bottom; ++y)
        for (int x = client.left; x < client.right; ++x)
            good = good && pixels[y * width + x] == marker;
    good = good && pixels[0] != marker;
    // The real icon's background previously erased this line beneath Zoom.
    for (int x = width - 30; x < width - 3; ++x)
        good = good && GetPixel(dc, x, 18) == RGB(0, 0, 0);
    // The resource's transparent final three columns reveal the title rules.
    for (int x = width - 13; x < width - 10; ++x)
        good = good && GetPixel(dc, x, 8) == GetPixel(dc, width - 30, 8);
    good = good && GetPixel(dc, 1, 1) == RGB(204, 204, 255) && GetPixel(dc, width - 3, 8) == RGB(153, 153, 204) &&
           GetPixel(dc, 30, 17) == RGB(153, 153, 204) && GetPixel(dc, width - 2, height - 10) == RGB(0, 0, 0) &&
           GetPixel(dc, 30, height - 2) == RGB(0, 0, 0);
    for (UINT part : {HTCLOSE, HTMAXBUTTON}) {
        press_frame_button(window, part);
        finder::classic::paint_window_chrome(window, dc);
        GdiFlush();
        for (int y = 0; y < 13; ++y)
            for (int x = 0; x < 13; ++x) {
                const unsigned char* rgba = chrome_close_clicked_rgba + (y * 16 + x) * 4;
                if (rgba[3] == 255)
                    good = good && GetPixel(dc, (part == HTCLOSE ? 10 : width - 26) + x, 3 + y) ==
                                       RGB(rgba[0], rgba[1], rgba[2]);
            }
        SendMessageW(window, WM_CANCELMODE, 0, 0);
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    RemoveWindowSubclass(window, frame_probe, 44);
    finder::classic::chrome_art(nullptr, nullptr, nullptr);
    DestroyIcon(zoom);
    DestroyIcon(clicked);
    std::cout << "Custom frame dispatch/client clipping: " << (good ? "passed" : "FAILED") << std::endl;
    return good;
}

// A fixture layout mirrors the folder's status strip and list on WM_SIZE.
LRESULT CALLBACK zoom_layout(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (message == WM_SIZE) {
        HWND list = GetDlgItem(window, 42);
        if (list)
            MoveWindow(list, 0, 18, LOWORD(lp), std::max(0, int(HIWORD(lp)) - 18), TRUE);
    }
    return DefSubclassProc(window, message, wp, lp);
}

// Frame tracking receives nonclient screen coordinates on down and client
// coordinates while captured. Do not bypass the production release path.
POINT press_frame_button(HWND window, UINT part)
{
    RECT bounds;
    GetWindowRect(window, &bounds);
    POINT screen{part == HTCLOSE ? bounds.left + 16 : bounds.right - 20, bounds.top + 9};
    SendMessageW(window, WM_NCLBUTTONDOWN, part, MAKELPARAM(screen.x, screen.y));
    ScreenToClient(window, &screen);
    return screen;
}

void click_frame_button(HWND window, UINT part)
{
    POINT point = press_frame_button(window, part);
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
}

// The list paints the grow corner, while the parent must receive its sizing
// hit. This must work even when both scrollbar ranges are inactive.
bool check_grow_corner(HWND window, HWND list)
{
    RECT bounds;
    GetWindowRect(finder::classic::scrollbar_window(list), &bounds);
    bool good = true;
    for (int inset : {1, 8, 15}) {
        LPARAM point = MAKELPARAM(bounds.right - inset, bounds.bottom - inset);
        good = good && SendMessageW(list, WM_NCHITTEST, 0, point) == HTTRANSPARENT &&
               SendMessageW(window, WM_NCHITTEST, 0, point) == HTBOTTOMRIGHT;
    }
    std::cout << "Grow corner with " << ListView_GetItemCount(list) << " items: " << (good ? "passed" : "FAILED")
              << std::endl;
    return good;
}

bool check_content_zoom()
{
    HINSTANCE module = GetModuleHandleW(nullptr);
    HWND owner =
        CreateWindowExW(0, L"STATIC", L"Host", WS_OVERLAPPEDWINDOW, 0, 0, 900, 650, nullptr, nullptr, module, nullptr);
    HWND child = CreateWindowExW(0, L"STATIC", L"Content zoom", WS_CHILD | WS_CAPTION | WS_THICKFRAME, 45, 55, 500, 400,
                                 owner, nullptr, module, nullptr);
    if (!owner || !child)
        return false;
    finder::classic::decorate(owner, true);
    finder::classic::decorate(child);
    SetWindowSubclass(child, zoom_layout, 45, 0);
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | LVS_ICON, 0, 18, 496, 361, child,
                                reinterpret_cast<HMENU>(42), module, nullptr);
    if (!list)
        return false;
    finder::classic::style_list(list);
    bool grow_routed = check_grow_corner(child, list);
    auto bounds = [owner](HWND window) {
        RECT r;
        GetWindowRect(window, &r);
        MapWindowPoints(nullptr, owner, reinterpret_cast<POINT*>(&r), 2);
        return r;
    };
    RECT original = bounds(child);
    auto toggle = [child] { click_frame_button(child, HTMAXBUTTON); };
    toggle();
    RECT empty = bounds(child);
    bool good = grow_routed && !IsZoomed(child) && empty.right - empty.left == 240 && empty.bottom - empty.top == 120;
    toggle();
    RECT restored = bounds(child);
    good = good && EqualRect(&original, &restored);
    for (int i = 0; i < 3; ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(L"Folder icon");
        ListView_InsertItem(list, &item);
        ListView_SetItemPosition(list, i, 16 + i * 64, 8);
    }
    good = check_grow_corner(child, list) && good;
    toggle();
    RECT compact = bounds(child);
    RECT workspace;
    GetClientRect(owner, &workspace);
    good = good && !IsZoomed(child) && compact.right < workspace.right && compact.bottom < workspace.bottom;
    RECT client;
    GetClientRect(list, &client);
    for (int index = 0; index < 3; ++index) {
        RECT item;
        good = good && ListView_GetItemRect(list, index, &item, LVIR_BOUNDS) && item.left >= 0 && item.top >= 0 &&
               item.right <= client.right && item.bottom <= client.bottom;
    }
    toggle();
    restored = bounds(child);
    good = good && EqualRect(&original, &restored);
    ListView_SetItemPosition(list, 2, 1200, 1100);
    ListView_Scroll(list, 300, 200);
    toggle();
    RECT large = bounds(child);
    good = good && !IsZoomed(child) && EqualRect(&large, &workspace);
    MoveWindow(owner, 0, 0, 600, 400, TRUE);
    GetClientRect(owner, &workspace);
    large = bounds(child);
    good = good && !IsZoomed(child) && EqualRect(&large, &workspace);
    toggle();
    restored = bounds(child);
    good = good && EqualRect(&original, &restored);
    for (DWORD view : {LV_VIEW_LIST, LV_VIEW_DETAILS}) {
        ListView_SetView(list, view);
        toggle();
        RECT fitted = bounds(child);
        good = good && !IsZoomed(child) && fitted.right <= workspace.right && fitted.bottom <= workspace.bottom;
        toggle();
        restored = bounds(child);
        good = good && EqualRect(&original, &restored) && ListView_GetItemCount(list) == 3;
    }
    toggle();
    MoveWindow(child, 20, 25, 310, 220, TRUE);
    SendMessageW(child, WM_EXITSIZEMOVE, 0, 0);
    RECT manual = bounds(child);
    toggle();
    toggle();
    restored = bounds(child);
    good = good && EqualRect(&manual, &restored);
    // TrackBox does not execute until release inside the same visible box.
    POINT point = press_frame_button(child, HTMAXBUTTON);
    RECT pending = bounds(child);
    good = good && GetCapture() == child && EqualRect(&manual, &pending);
    SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(point.x + 70, point.y));
    SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(point.x + 70, point.y));
    restored = bounds(child);
    good = good && GetCapture() != child && EqualRect(&manual, &restored);
    RECT double_bounds;
    GetWindowRect(child, &double_bounds);
    SendMessageW(child, WM_NCLBUTTONDBLCLK, HTMAXBUTTON, MAKELPARAM(double_bounds.right - 20, double_bounds.top + 9));
    pending = bounds(child);
    good = good && GetCapture() == child && EqualRect(&manual, &pending);
    SendMessageW(child, WM_CANCELMODE, 0, 0);
    for (UINT message : {WM_CANCELMODE, WM_KEYDOWN, WM_CAPTURECHANGED}) {
        press_frame_button(child, HTMAXBUTTON);
        if (message == WM_CAPTURECHANGED)
            SetCapture(owner);
        else
            SendMessageW(child, message, message == WM_KEYDOWN ? VK_ESCAPE : 0, 0);
        restored = bounds(child);
        good = good && GetCapture() != child && EqualRect(&manual, &restored);
        if (GetCapture() == owner)
            ReleaseCapture();
    }
    // Leaving and returning to Zoom should rearm it, just like TrackBox.
    point = press_frame_button(child, HTMAXBUTTON);
    SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(point.x + 70, point.y));
    SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(point.x, point.y));
    SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
    pending = bounds(child);
    good = good && !EqualRect(&manual, &pending);
    toggle();
    point = press_frame_button(child, HTCLOSE);
    good = good && IsWindow(child) && GetCapture() == child;
    SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(point.x + 70, point.y));
    good = good && IsWindow(child) && GetCapture() != child;
    // The three transparent columns after either tile are draggable title.
    RECT screen;
    GetWindowRect(child, &screen);
    for (POINT gap : {POINT{screen.left + 24, screen.top + 9}, POINT{screen.right - 12, screen.top + 9}})
        good = good && SendMessageW(child, WM_NCHITTEST, 0, MAKELPARAM(gap.x, gap.y)) == HTCAPTION;
    // An inactive window's hidden Close/Zoom boxes cannot execute on the first
    // click; the WDEF classifies that title area as dragging/activation only.
    finder::classic::activate(owner);
    for (POINT box : {POINT{screen.left + 16, screen.top + 9}, POINT{screen.right - 20, screen.top + 9}})
        good = good && SendMessageW(child, WM_NCHITTEST, 0, MAKELPARAM(box.x, box.y)) == HTCAPTION;
    finder::classic::activate(child);
    press_frame_button(child, HTMAXBUTTON);
    SendMessageW(owner, WM_ACTIVATEAPP, FALSE, 0);
    restored = bounds(child);
    good = good && GetCapture() != child && EqualRect(&manual, &restored);
    click_frame_button(child, HTCLOSE);
    good = good && !IsWindow(child) && GetCapture() != child;
    DestroyWindow(owner);
    std::cout << "Content Zoom, restore, host resize and cancellable frame buttons: " << (good ? "passed" : "FAILED")
              << std::endl;
    return good;
}

// Zoomed children must follow changes to the host client rectangle without
// changing their normal restore placement or resizing ordinary siblings.
bool check_zoomed_layout()
{
    auto module = GetModuleHandleW(nullptr);
    HWND owner = CreateWindowExW(0, L"STATIC", L"Hidden native host", WS_OVERLAPPEDWINDOW, 0, 0, 600, 400, nullptr,
                                 nullptr, module, nullptr);
    HWND child = CreateWindowExW(0, L"STATIC", L"Zoomed child", WS_CHILD | WS_CAPTION | WS_THICKFRAME, 40, 35, 300, 180,
                                 owner, nullptr, module, nullptr);
    HWND sibling = CreateWindowExW(0, L"STATIC", L"Normal sibling", WS_CHILD | WS_CAPTION, 70, 65, 250, 140, owner,
                                   nullptr, module, nullptr);
    if (!owner || !child || !sibling)
        return false;
    finder::classic::decorate(owner, true);
    finder::classic::decorate(child);
    finder::classic::decorate(sibling);
    auto bounds = [owner](HWND window) {
        RECT rect;
        GetWindowRect(window, &rect);
        MapWindowPoints(nullptr, owner, reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    };
    RECT normal = bounds(child), sibling_normal = bounds(sibling);
    ShowWindow(child, SW_MAXIMIZE);
    bool good = IsZoomed(child);
    for (SIZE size : {SIZE{900, 600}, SIZE{480, 300}, SIZE{750, 500}}) {
        MoveWindow(owner, 0, 0, size.cx, size.cy, TRUE);
        RECT workspace;
        GetClientRect(owner, &workspace);
        auto zoomed = bounds(child);
        auto unchanged = bounds(sibling);
        bool fits = EqualRect(&workspace, &zoomed);
        good = good && fits && IsZoomed(child) && EqualRect(&unchanged, &sibling_normal);
        std::cout << "Zoomed child after host resize " << size.cx << "x" << size.cy << ": "
                  << (fits ? "passed" : "FAILED") << std::endl;
    }
    ShowWindow(child, SW_RESTORE);
    auto restored = bounds(child);
    good = good && EqualRect(&restored, &normal);
    std::cout << "Normal placement retained: " << (EqualRect(&restored, &normal) ? "passed" : "FAILED") << std::endl;
    DestroyWindow(owner);
    return good;
}

// Simulate disjoint exposed strips and buffered paint DCs without reading the
// user's screen. The expected pixels come directly from the System resource,
// and must retain their desktop phase regardless of clip/viewport origins.
bool check_desktop_repaint(HWND parent)
{
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_ICON | LVS_NOSCROLL,
                                0, 0, 160, 120, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!list)
        return false;
    ListView_SetExtendedListViewStyle(list, LVS_EX_DOUBLEBUFFER);
    finder::classic::style_desktop(list);
    finder::classic::style_desktop(list); // Repeated styling must not leak/replace the brush.
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 160;
    info.bmiHeader.biHeight = -120;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        DestroyWindow(list);
        return false;
    }
    auto previous = SelectObject(dc, bitmap);
    auto* pixels = static_cast<unsigned*>(bits);
    constexpr unsigned marker = 0x00345678;
    bool good = true;
    for (POINT viewport : {POINT{0, 0}, POINT{-17, -23}, POINT{11, 7}}) {
        for (bool partial : {false, true}) {
            std::fill(pixels, pixels + 160 * 120, marker);
            int saved = SaveDC(dc);
            SetViewportOrgEx(dc, viewport.x, viewport.y, nullptr);
            SetBrushOrgEx(dc, 5, 9, nullptr);
            HRGN update = CreateRectRgn(0, 0, 160, 120);
            if (partial) {
                SetRectRgn(update, 7, 13, 39, 109);
                HRGN strip = CreateRectRgn(44, 63, 151, 81);
                CombineRgn(update, update, strip, RGN_OR);
                DeleteObject(strip);
            }
            SelectClipRgn(dc, update);
            SendMessageW(list, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
            if (viewport.x == 0 && viewport.y == 0 && !partial) {
                int print_saved = SaveDC(dc);
                SendMessageW(list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
                RestoreDC(dc, print_saved);
            }

            GdiFlush();
            bool reported = false;
            for (int y = 0; y < 120; ++y) {
                for (int x = 0; x < 160; ++x) {
                    int logical_x = x - viewport.x, logical_y = y - viewport.y;
                    unsigned expected = marker;
                    if (PtInRegion(update, x, y) && logical_x >= 0 && logical_x < 160 && logical_y >= 0 &&
                        logical_y < 120)
                        expected = desktop_pattern_pixels[(logical_y % desktop_pattern_height) * desktop_pattern_width +
                                                          logical_x % desktop_pattern_width] &
                                   0x00ffffff;
                    if ((pixels[y * 160 + x] & 0x00ffffff) != expected && !reported) {
                        std::cout << "Desktop mismatch viewport " << viewport.x << "," << viewport.y << " partial "
                                  << partial << " at " << x << "," << y << " actual " << std::hex << pixels[y * 160 + x]
                                  << " expected " << expected << std::dec << std::endl;
                        reported = true;
                    }
                    good = good && (pixels[y * 160 + x] & 0x00ffffff) == expected;
                }
            }
            POINT brush_origin;
            GetBrushOrgEx(dc, &brush_origin);
            good = good && brush_origin.x == 5 && brush_origin.y == 9;
            RestoreDC(dc, saved);
            DeleteObject(update);
        }
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(list);
    std::cout << "Desktop pattern partial/buffered background repaint: " << (good ? "passed" : "FAILED") << std::endl;
    return good;
}

// Classic selection darkens every covered pixel instead of dithering a
// Windows highlight over the image. Check alpha and legacy AND masks too.
static bool check_selected_icon()
{
    HDC dc = CreateCompatibleDC(nullptr);
    bool good = dc != nullptr;
    uint16_t detail[] = {0xcccc, 0xcccc, 0xffff};
    ROMlib_icon_selected_index_rgb(detail, 8, 6);
    good = detail[0] == 0xcccc && detail[1] == 0xcccc && detail[2] == 0xffff && good;
    ROMlib_icon_selected_index_rgb(detail, 8, 51);
    good = detail[0] == 0x3333 && detail[1] == 0x3333 && detail[2] == 0x7fff && good;
    for (bool legacy : {false, true}) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof info.bmiHeader;
        info.bmiHeader.biWidth = 4;
        info.bmiHeader.biHeight = -1;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        HBITMAP color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        unsigned char original[] = {255, 255, 255, 255, 255, 128, 64, 255, 64, 64, 64, 128, 0, 0, 0, 0};
        if (legacy)
            for (unsigned i = 3; i < sizeof original; i += 4)
                original[i] = 0;
        memcpy(pixels, original, sizeof original);
        unsigned char mask_bits[] = {0x10, 0};
        HBITMAP mask = CreateBitmap(4, 1, 1, 1, mask_bits);
        ICONINFO source_info{};
        source_info.fIcon = TRUE;
        source_info.hbmColor = color;
        source_info.hbmMask = mask;
        HICON source = CreateIconIndirect(&source_info);
        HICON selected = finder::classic::selected_icon(source);
        ICONINFO result{};
        unsigned char transformed[16]{};
        good = selected && GetIconInfo(selected, &result) && good;
        if (result.hbmColor) {
            good = GetDIBits(dc, result.hbmColor, 0, 1, transformed, &info, DIB_RGB_COLORS) == 1 && good;
            good = transformed[0] == 127 && transformed[1] == 127 && transformed[2] == 127 && transformed[3] == 255 &&
                   transformed[4] == 127 && transformed[5] == 32 && transformed[6] == 16 && transformed[7] == 255 &&
                   transformed[15] == 0 && good;
            if (!legacy)
                good = transformed[8] == 32 && transformed[9] == 32 && transformed[10] == 32 &&
                       transformed[11] == 128 && good;
            DeleteObject(result.hbmColor);
        }
        if (result.hbmMask)
            DeleteObject(result.hbmMask);
        HGDIOBJ previous = SelectObject(dc, color);
        RECT area{0, 0, 4, 1};
        HBRUSH background = CreateSolidBrush(RGB(20, 40, 60));
        FillRect(dc, &area, background);
        DrawIconEx(dc, 0, 0, selected, 4, 1, 0, nullptr, DI_NORMAL);
        good = GetPixel(dc, 3, 0) == RGB(20, 40, 60) && GetPixel(dc, 0, 0) == RGB(127, 127, 127) && good;
        DrawIconEx(dc, 0, 0, source, 4, 1, 0, nullptr, DI_NORMAL);
        good = GetPixel(dc, 0, 0) == RGB(255, 255, 255) && good;
        SelectObject(dc, previous);
        DeleteObject(background);
        DestroyIcon(selected);
        DestroyIcon(source);
        DeleteObject(color);
        DeleteObject(mask);
    }
    if (dc)
        DeleteDC(dc);
    std::cout << "Classic selected colors, source preservation, alpha and AND masks: " << (good ? "passed" : "FAILED")
              << std::endl;
    return good;
}

// A real 32-pixel icon and its label must fit above the horizontal gutter
// when the vertical thumb reaches the end, including after a host resize.
static bool check_icon_bottom(HWND parent)
{
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_ICON, 0, 0, 400, 200, parent, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    finder::classic::style_list(list);
    HIMAGELIST images = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 1, 1);
    ImageList_AddIcon(images, LoadIconW(nullptr, IDI_APPLICATION));
    ListView_SetImageList(list, images, LVSIL_NORMAL);
    ListView_SetIconSpacing(list, 80, 64);
    for (int i = 0; i < 12; ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = i;
        item.iImage = 0;
        item.pszText = const_cast<wchar_t*>(L"Bottom icon");
        ListView_InsertItem(list, &item);
        ListView_SetItemPosition(list, i, 20 + (i % 3) * 100, 20 + (i / 3) * 76);
    }
    bool good = true;
    for (int height : {200, 131, 245}) {
        MoveWindow(list, 0, 0, 400, height, TRUE);
        bool moved = drag_thumb(list, true, 1, 1);
        RECT bounds{}, client{};
        ListView_GetItemRect(list, 11, &bounds, LVIR_BOUNDS);
        GetClientRect(list, &client);
        std::cout << "Bottom icon at height " << height << ": " << bounds.bottom << " client " << client.bottom
                  << std::endl;
        good = moved && bounds.bottom <= client.bottom && good;
    }
    ListView_SetItemPosition32(list, 11, 220, 100000);
    good = drag_thumb(list, true, 1, 1) && good;
    RECT last{}, client{};
    ListView_GetItemRect(list, 11, &last, LVIR_BOUNDS);
    GetClientRect(list, &client);
    good = last.bottom <= client.bottom && good;
    SendMessageW(list, WM_VSCROLL, SB_BOTTOM, 0);
    ListView_GetItemRect(list, 11, &last, LVIR_BOUNDS);
    good = last.bottom <= client.bottom && good;
    HWND host = finder::classic::scrollbar_window(list);
    HRGN visible = CreateRectRgn(0, 0, 0, 0);
    good = GetWindowRgn(host, visible) != ERROR && !PtInRegion(visible, 10, 10) &&
           PtInRegion(visible, client.right + 8, 10) && good;
    DeleteObject(visible);
    SendMessageW(list, WM_VSCROLL, SB_TOP, 0);
    POINT arrow{client.right + 8, client.bottom - 8};
    ClientToScreen(list, &arrow);
    good = SendMessageW(host, WM_NCHITTEST, 0, MAKELPARAM(arrow.x, arrow.y)) == HTVSCROLL && good;
    SendMessageW(host, WM_NCLBUTTONDOWN, HTVSCROLL, MAKELPARAM(arrow.x, arrow.y));
    SCROLLINFO position{sizeof position};
    position.fMask = SIF_POS;
    GetScrollInfo(list, SB_VERT, &position);
    good = GetCapture() == list && position.nPos > 0 && good;
    SendMessageW(list, WM_LBUTTONUP, 0, 0);
    DestroyWindow(list);
    good = !IsWindow(host) && good;
    ImageList_Destroy(images);
    return good;
}

// Busy cursors nest, survive control cursor messages and unwind on failure.
static bool check_waiting_cursor(HWND window)
{
    HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);
    HCURSOR wait = LoadCursorW(nullptr, IDC_WAIT);
    HCURSOR original = SetCursor(arrow);
    bool good = !finder::classic::waiting_cursor();
    try {
        finder::classic::WaitingCursor outer;
        good = good && GetCursor() == wait;
        {
            finder::classic::WaitingCursor inner;
            SetCursor(arrow);
            good = good && SendMessageW(window, WM_SETCURSOR, WPARAM(window), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            good = good && GetCursor() == wait;
        }
        good = good && GetCursor() == wait;
        throw 1;
    }
    catch (int) {
    }
    good = good && GetCursor() == arrow && !finder::classic::waiting_cursor();
    SetCursor(original);
    printf("Scoped waiting cursor, nested controls and exception restoration: %s\n", good ? "passed" : "FAILED");
    return good;
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--selection"))
        return check_selected_icon() ? 0 : 1;
    INITCOMMONCONTROLSEX init{sizeof init, ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&init);
    HWND parent = CreateWindowExW(0, L"STATIC", L"Scroll fixture", WS_OVERLAPPEDWINDOW, 0, 0, 500, 400, nullptr,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    if (argc == 2 && !strcmp(argv[1], "--viewport")) {
        bool good = parent && check_icon_bottom(parent);
        if (parent)
            DestroyWindow(parent);
        std::cout << "Content viewport, large range, gutter input/region/lifetime: " << (good ? "passed" : "FAILED")
                  << std::endl;
        return good ? 0 : 1;
    }
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_ICON, 0, 0, 400, 300, parent, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!parent || !list)
        return 2;
    finder::classic::style_list(list);
    bool good = check_selected_icon() && check_icon_bottom(parent) && check_waiting_cursor(list) &&
                check_desktop_repaint(parent);
    good = check(list, "Empty folder") && good;
    for (int i = 0; i < 20; ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(L"Icon");
        ListView_InsertItem(list, &item);
        ListView_SetItemPosition(list, i, 30 + (i % 3) * 80, 30 + (i / 3) * 80);
    }
    good = check(list, "Vertical range") && good;
    for (int i = 0; i < 5; ++i) {
        ListView_Scroll(list, 0, 31);
        good = check(list, "Vertical scroll") && good;
    }
    SendMessageW(list, WM_VSCROLL, SB_BOTTOM, 0);
    good = check(list, "Scrollbar navigation") && good;
    ListView_SetItemPosition(list, 19, 900, 600);
    ListView_EnsureVisible(list, 19, FALSE);
    good = check(list, "Both ranges") && good;
    good = drag_thumb(list, true, 1, 2) && good;
    good = drag_thumb(list, true, 1, 1) && good;
    good = drag_thumb(list, true, 0, 1) && good;
    good = drag_thumb(list, false, 1, 2) && good;
    good = drag_thumb(list, false, 1, 1) && good;
    good = drag_thumb(list, false, 0, 1) && good;
    good = check_buttons(list, parent, true) && good;
    good = check_buttons(list, parent, false) && good;
    good = check(list, "Thumb tracking") && good;
    ListView_Scroll(list, -73, -37);
    good = check(list, "Horizontal and vertical scroll") && good;
    MoveWindow(list, 0, 0, 450, 350, TRUE);
    good = check(list, "Resize") && good;
    ListView_DeleteAllItems(list);
    good = check(list, "Ranges removed") && good;
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.pszText = const_cast<wchar_t*>(L"Wide folder");
    ListView_InsertItem(list, &item);
    ListView_SetItemPosition(list, 0, 900, 30);
    ListView_EnsureVisible(list, 0, FALSE);
    good = check(list, "Horizontal range only") && good;
    ListView_Scroll(list, -40, 0);
    good = check(list, "Horizontal scroll only") && good;
    // Details use row-based vertical ranges; List uses column-based horizontal
    // ranges. Both must track accurately at intermediate positions, not just ends.
    for (int i = 1; i < 100; ++i) {
        item.iItem = i;
        ListView_InsertItem(list, &item);
    }
    ListView_SetView(list, LV_VIEW_DETAILS);
    LVCOLUMNW column{};
    column.mask = LVCF_WIDTH;
    column.cx = 1000;
    ListView_InsertColumn(list, 0, &column);
    good = drag_thumb(list, true, 1, 2) && good;
    good = drag_thumb(list, true, 1, 1) && good;
    good = drag_thumb(list, false, 1, 2) && good;
    for (int i = 100; i < 500; ++i) {
        item.iItem = i;
        ListView_InsertItem(list, &item);
    }
    ListView_SetView(list, LV_VIEW_LIST);
    ListView_SetColumnWidth(list, 0, 150);
    good = drag_thumb(list, false, 1, 2) && good;
    good = drag_thumb(list, false, 1, 1) && good;
    good = good && tested_active && tested_inactive;
    good = check_frame(parent) && good;
    DestroyWindow(parent);
    good = check_zoomed_layout() && good;
    good = check_content_zoom() && good;
    finder::classic::finish();
    return good ? 0 : 1;
}
