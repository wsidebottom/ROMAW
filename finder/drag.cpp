/*+===================================================================
  File:      drag.cpp

  Summary:   Native Finder drag feedback and window-hierarchy drop routing. All state belongs to the UI thread. Resolve
             the destination before deciding whether a drop repositions an icon, copies files or opens an application.

  Classes:   Declared: DragDib; Used: DragDib, std::vector

  Functions: finder::DragImage::begin, finder::DragImage::end, finder::DragImage::move, finder::DragImage::window_at,
             finder::DragImage::~DragImage, finder::drag_destination

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Native Finder drag feedback and window-hierarchy drop routing. All state
// belongs to the UI thread. Resolve the destination before deciding whether
// a drop repositions an icon, copies files or opens an application.

#include "finder/drag.h"
#include <algorithm>
#include <cstdint>

namespace finder {
namespace {
// GDI writes ordinary RGB even when drawing text into a 32-bit bitmap. Draw
// against black and white to recover alpha for both masked icons and captions.
struct DragDib {
    DragDib() = default;
    DragDib(const DragDib&) = delete;
    DragDib& operator=(const DragDib&) = delete;

    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    uint32_t* pixels = nullptr;

    bool create(int width, int height)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof info.bmiHeader;
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        if (!dc)
            return false;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!bitmap)
            return false;
        previous = SelectObject(dc, bitmap);
        return previous && previous != HGDI_ERROR;
    }

    ~DragDib()
    {
        if (previous && previous != HGDI_ERROR)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
    }
};

HBITMAP drag_bitmap(HIMAGELIST image, int width, int height)
{
    DragDib black, white;
    if (!black.create(width, height) || !white.create(width, height))
        return nullptr;
    size_t count = size_t(width) * height;
    std::fill_n(black.pixels, count, 0);
    std::fill_n(white.pixels, count, 0x00ffffff);
    if (!ImageList_Draw(image, 0, black.dc, 0, 0, ILD_TRANSPARENT) ||
        !ImageList_Draw(image, 0, white.dc, 0, 0, ILD_TRANSPARENT))
        return nullptr;
    GdiFlush();
    for (size_t i = 0; i < count; ++i) {
        unsigned difference = 0;
        for (unsigned shift : {0u, 8u, 16u}) {
            int light = (white.pixels[i] >> shift) & 255;
            int dark = (black.pixels[i] >> shift) & 255;
            difference = std::max(difference, unsigned(std::max(0, light - dark)));
        }
        unsigned alpha = 255 - difference;
        uint32_t pixel = alpha << 24;
        for (unsigned shift : {0u, 8u, 16u})
            pixel |= std::min(alpha, (black.pixels[i] >> shift) & 255u) << shift;
        black.pixels[i] = pixel; // UpdateLayeredWindow requires premultiplied BGRA.
    }
    SelectObject(black.dc, black.previous);
    black.previous = nullptr;
    auto bitmap = black.bitmap;
    black.bitmap = nullptr;
    return bitmap;
}

LRESULT CALLBACK drag_overlay_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wp, lp);
}
}

// Use the list's icon/caption artwork, retaining the original mouse-down
// hotspot. The compositor owns presentation, so repaints cannot leave trails.
bool DragImage::begin(HWND list, int index, POINT grab, POINT position, HWND target, HBITMAP artwork, POINT origin)
{
    end();
    bitmap = artwork;
    if (index < 0 || index >= ListView_GetItemCount(list)) {
        end();
        return false;
    }
    int width = 0, height = 0;
    if (bitmap) {
        BITMAP bounds{};
        if (GetObjectW(bitmap, sizeof bounds, &bounds)) {
            width = bounds.bmWidth;
            height = bounds.bmHeight;
        }
        if (width < 1 || height < 1 || width > 4096 || height > 4096) {
            end();
            return false;
        }
    }
    else {
        HIMAGELIST image = ListView_CreateDragImage(list, index, &origin);
        if (!image)
            return false;
        if (ImageList_GetIconSize(image, &width, &height) && width > 0 && height > 0 && width <= 4096 && height <= 4096)
            bitmap = drag_bitmap(image, width, height);
        ImageList_Destroy(image);
    }
    if (!bitmap)
        return false;
    ClientToScreen(list, &origin);
    hotspot = POINT{grab.x - origin.x, grab.y - origin.y};
    size = SIZE{width, height};
    surface = target;
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"FinderDragOverlay";
    type.lpfnWndProc = drag_overlay_proc;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        end();
        return false;
    }
    overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                              type.lpszClassName, L"", WS_POPUP, position.x - hotspot.x, position.y - hotspot.y, width,
                              height, GetAncestor(list, GA_ROOT), nullptr, type.hInstance, nullptr);
    HDC dc = CreateCompatibleDC(nullptr);
    auto previous = dc ? SelectObject(dc, bitmap) : nullptr;
    POINT source{}, destination{position.x - hotspot.x, position.y - hotspot.y};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 160, AC_SRC_ALPHA};
    bool ready = overlay && previous && previous != HGDI_ERROR &&
                 UpdateLayeredWindow(overlay, nullptr, &destination, &size, dc, &source, 0, &blend, ULW_ALPHA);
    if (previous && previous != HGDI_ERROR)
        SelectObject(dc, previous);
    if (dc)
        DeleteDC(dc);
    if (!ready) {
        end();
        return false;
    }
    move(position);
    return true;
}

// Captured mouse coordinates are screen-relative. Moving this independent
// surface never copies pixels from the old destination onto the new one.
void DragImage::move(POINT position)
{
    if (!overlay)
        return;
    int x = position.x - hotspot.x, y = position.y - hotspot.y;
    if (surface) {
        RECT bounds{}, image{x, y, x + size.cx, y + size.cy}, clipped{};
        if (!GetWindowRect(surface, &bounds)) {
            end();
            return;
        }
        IntersectRect(&clipped, &bounds, &image);
        OffsetRect(&clipped, -x, -y);
        HRGN region = CreateRectRgn(clipped.left, clipped.top, clipped.right, clipped.bottom);
        if (region && !SetWindowRgn(overlay, region, FALSE))
            DeleteObject(region);
    }
    UINT flags = SWP_NOSIZE | SWP_NOACTIVATE;
    flags |= !surface || IsWindowVisible(surface) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
    SetWindowPos(overlay, HWND_TOPMOST, x, y, 0, 0, flags);
}

// Hit testing cannot select the overlay, including on Windows versions where
// WindowFromPoint does not honor transparent extended styles across windows.
HWND DragImage::window_at(POINT position) const
{
    bool visible = overlay && IsWindowVisible(overlay);
    if (visible)
        ShowWindow(overlay, SW_HIDE);
    HWND window = WindowFromPoint(position);
    if (visible)
        ShowWindow(overlay, SW_SHOWNOACTIVATE);
    return window;
}

// Repaint only changed targets. Composited drag feedback remains independent
// of these updates and never saves or restores the destination's pixels.
void DragHighlight::set(HWND list, int index)
{
    if (!list || !IsWindow(list) || index < 0 || index >= ListView_GetItemCount(list)) {
        list = nullptr;
        index = -1;
    }
    if (target == list && item == index)
        return;
    auto paint = [](HWND window, int row, UINT state) {
        if (!IsWindow(window) || row < 0 || row >= ListView_GetItemCount(window))
            return;
        ListView_SetItemState(window, row, state, LVIS_DROPHILITED);
        RECT bounds;
        if (ListView_GetItemRect(window, row, &bounds, LVIR_BOUNDS))
            RedrawWindow(window, &bounds, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    };
    paint(target, item, 0);
    target = list;
    item = index;
    paint(target, item, LVIS_DROPHILITED);
}

void DragHighlight::clear(HWND list)
{
    if (!list || list == target)
        set(nullptr, -1);
}

DragHighlight::~DragHighlight()
{
    clear();
}

bool drag_copies(bool same_volume, bool option, bool shift)
{
    return option || (!same_volume && !shift);
}

bool drag_target_allowed(HWND source, const std::vector<int>& selected, HWND destination, int index, bool accepts)
{
    return destination && index >= 0 && accepts &&
           (source != destination || std::find(selected.begin(), selected.end(), index) == selected.end());
}

// Remove the overlay before drop handling, capture loss or window destruction.
void DragImage::end()
{
    if (overlay)
        DestroyWindow(overlay);
    if (bitmap)
        DeleteObject(bitmap);
    overlay = nullptr;
    bitmap = nullptr;
    surface = nullptr;
    hotspot = {};
    size = {};
}

// Release any outstanding drag state when the UI owner shuts down.
DragImage::~DragImage()
{
    end();
}

// Resolve a hit control to its nearest registered folder, stopping before a
// top-level popup's owner chain.
HWND drag_destination(HWND hit, const std::vector<HWND>& folder_windows)
{
    // Search from the hit control outward: testing the desktop first with
    // IsChild would also match controls inside every child folder window.
    for (HWND current = hit; current; current = GetParent(current)) {
        if (std::find(folder_windows.begin(), folder_windows.end(), current) != folder_windows.end())
            return current;
        // GetParent returns an owner for a top-level popup. Crossing that edge
        // would turn a drop over that separate surface into a desktop drop.
        if (!(GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD))
            break;
    }
    return nullptr;
}
}
