/*+===================================================================
  File:      drag_test.cpp

  Summary:   Implements drag test support for regression tests and test fixtures.

  Classes:   Used: DragImage, std::vector

  Functions: main

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "finder/drag.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

int main()
{
    // Hidden windows reproduce Finder's desktop/child/control hierarchy without
    // injecting input into a user's desktop or touching any files.
    auto create = [](HWND parent, DWORD style) {
        HWND window = CreateWindowExW(0, L"STATIC", L"Finder drag fixture", style, 0, 0, 100, 100, parent, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
        assert(window);
        return window;
    };
    HWND desktop = create(nullptr, WS_OVERLAPPEDWINDOW);
    HWND desktop_list = create(desktop, WS_CHILD);
    HWND first = create(desktop, WS_CHILD);
    HWND first_list = create(first, WS_CHILD);
    HWND inline_editor = create(first_list, WS_CHILD);
    HWND first_status = create(first, WS_CHILD);
    HWND second = create(desktop, WS_CHILD);
    HWND second_list = create(second, WS_CHILD);
    HWND popup = create(desktop, WS_POPUP);
    HWND popup_button = create(popup, WS_CHILD);
    HWND unrelated = create(nullptr, WS_OVERLAPPEDWINDOW);
    std::vector<HWND> folders{desktop, first, second};
    for (unsigned order = 0; order < 2; ++order) {
        assert(finder::drag_destination(desktop_list, folders) == desktop);
        assert(finder::drag_destination(first_list, folders) == first);
        assert(finder::drag_destination(inline_editor, folders) == first);
        assert(finder::drag_destination(first_status, folders) == first);
        assert(finder::drag_destination(first, folders) == first);
        assert(finder::drag_destination(second_list, folders) == second);
        assert(!finder::drag_destination(popup, folders));
        assert(!finder::drag_destination(popup_button, folders));
        assert(!finder::drag_destination(unrelated, folders));
        assert(!finder::drag_destination(nullptr, folders));
        std::reverse(folders.begin(), folders.end());
    }
    // Build composited feedback inside our hidden fixture. It must preserve
    // the hotspot without creating common-controls' saved-background state.
    INITCOMMONCONTROLSEX init{sizeof init, ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&init);
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"Drag image fixture", WS_CHILD | LVS_ICON | LVS_SHAREIMAGELISTS, 0, 0,
                                300, 300, desktop, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(list);
    HIMAGELIST images = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 1, 1);
    assert(images);
    // Known red/green artwork with transparent margins avoids depending on
    // the host's stock application icon or high-contrast theme.
    std::array<unsigned char, 128> mask;
    mask.fill(255);
    std::array<unsigned, 1024> color{};
    for (int y = 4; y < 28; ++y)
        for (int x = 4; x < 28; ++x) {
            mask[y * 4 + x / 8] &= ~(0x80 >> (x & 7));
            color[y * 32 + x] = x < 16 ? 0xffff4000 : 0xff00c020;
        }
    HICON glyph =
        CreateIcon(GetModuleHandleW(nullptr), 32, 32, 1, 32, mask.data(), reinterpret_cast<const BYTE*>(color.data()));
    assert(glyph && ImageList_AddIcon(images, glyph) == 0);
    DestroyIcon(glyph);
    ListView_SetImageList(list, images, LVSIL_NORMAL);
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_IMAGE;
    item.pszText = const_cast<wchar_t*>(L"Dragged icon");
    assert(ListView_InsertItem(list, &item) == 0);
    ListView_SetItemPosition(list, 0, 80, 60);
    POINT origin{};
    auto reference = ListView_CreateDragImage(list, 0, &origin);
    assert(reference);
    ImageList_Destroy(reference);
    ClientToScreen(list, &origin);
    POINT grab{origin.x + 7, origin.y + 9};
    POINT position{grab.x + 40, grab.y + 20};
    auto check_overlay = [&](const finder::DragImage& feedback) {
        HWND window = feedback.feedback_window();
        assert(window && IsWindow(window));
        assert(!IsWindowVisible(window)); // Hidden fixture never paints the user's desktop.
        auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        assert((style & (WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT)) ==
               (WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT));
        RECT bounds{};
        assert(GetWindowRect(window, &bounds));
        assert(bounds.left == position.x - 7 && bounds.top == position.y - 9);
        assert(!ImageList_GetDragImage(nullptr, nullptr));
    };
    auto pixels = [](HBITMAP bitmap) {
        BITMAP dimensions{};
        assert(GetObjectW(bitmap, sizeof dimensions, &dimensions));
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof info.bmiHeader;
        info.bmiHeader.biWidth = dimensions.bmWidth;
        info.bmiHeader.biHeight = -dimensions.bmHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        std::vector<unsigned> result(size_t(dimensions.bmWidth) * dimensions.bmHeight);
        HDC dc = CreateCompatibleDC(nullptr);
        assert(dc);
        assert(GetDIBits(dc, bitmap, 0, dimensions.bmHeight, result.data(), &info, DIB_RGB_COLORS));
        DeleteDC(dc);
        return result;
    };
    HWND foreground = GetForegroundWindow();
    {
        finder::DragImage feedback;
        assert(feedback.begin(list, 0, grab, position, desktop));
        check_overlay(feedback);
        auto artwork = pixels(feedback.feedback_bitmap());
        assert(std::any_of(artwork.begin(), artwork.end(), [](unsigned pixel) { return !(pixel >> 24); }));
        assert(std::any_of(artwork.begin(), artwork.end(), [](unsigned pixel) { return pixel >> 24; }));
        assert(std::any_of(artwork.begin(), artwork.end(), [](unsigned pixel) {
            return (pixel >> 24) && ((pixel & 255) != ((pixel >> 8) & 255));
        })); // Feedback retains color instead of becoming an opaque gray mask.
        for (auto pixel : artwork)
            for (unsigned shift : {0u, 8u, 16u})
                assert(((pixel >> shift) & 255) <= (pixel >> 24)); // Premultiplied alpha, including text.
        for (int pass = 0; pass < 30; ++pass) {
            position.x += 5;
            position.y -= 3;
            // Repainting beneath the image cannot contaminate the next frame.
            RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            feedback.move(position);
            check_overlay(feedback);
            assert(pixels(feedback.feedback_bitmap()) == artwork);
        }
        assert(GetForegroundWindow() == foreground);
        auto window = feedback.feedback_window();
        feedback.end();
        feedback.end();
        assert(!IsWindow(window) && !feedback.feedback_window() && !feedback.feedback_bitmap());
        assert(feedback.begin(list, 0, grab, position, desktop));
        assert(!feedback.begin(list, -1, grab, position, desktop));
        assert(!feedback.feedback_window() && !feedback.feedback_bitmap());
        assert(!feedback.begin(list, 1, grab, position, desktop));
        assert(feedback.begin(list, 0, grab, position, desktop));
        check_overlay(feedback);
    }
    assert(!ImageList_GetDragImage(nullptr, nullptr));
    // Repeated drags/cancellation must release both the composited surface and
    // its bitmap, rather than accumulating invisible windows or GDI objects.
    DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    DWORD user_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    {
        finder::DragImage feedback;
        for (int pass = 0; pass < 40; ++pass) {
            assert(feedback.begin(list, 0, grab, position, desktop));
            auto window = feedback.feedback_window();
            assert(feedback.window_at(position) != window);
            feedback.end();
            assert(!IsWindow(window));
        }
    }
    assert(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdi_before);
    assert(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == user_before);
    {
        HWND surface = create(nullptr, WS_OVERLAPPEDWINDOW);
        finder::DragImage feedback;
        assert(feedback.begin(list, 0, grab, position, surface));
        auto window = feedback.feedback_window();
        DestroyWindow(surface);
        feedback.move(position);
        assert(!IsWindow(window) && !feedback.feedback_window() && !feedback.feedback_bitmap());
    }
    // A hover must never replace the user's selection, including across lists.
    auto target_list = [&] {
        HWND target = CreateWindowExW(0, WC_LISTVIEWW, L"Drop target", WS_CHILD | LVS_ICON, 0, 0, 300, 300, desktop,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
        assert(target);
        for (int row = 0; row < 2; ++row) {
            item.iItem = row;
            assert(ListView_InsertItem(target, &item) == row);
        }
        return target;
    };
    HWND target = target_list(), other_target = target_list();
    ListView_SetItemState(target, 0, LVIS_SELECTED, LVIS_SELECTED);
    {
        finder::DragHighlight highlight;
        highlight.set(target, 1);
        assert(ListView_GetItemState(target, 1, LVIS_DROPHILITED));
        assert(!ListView_GetItemState(target, 1, LVIS_SELECTED));
        assert(ListView_GetItemState(target, 0, LVIS_SELECTED));
        highlight.clear(other_target);
        assert(ListView_GetItemState(target, 1, LVIS_DROPHILITED));
        highlight.set(target, 0);
        assert(!ListView_GetItemState(target, 1, LVIS_DROPHILITED));
        highlight.set(other_target, 1);
        assert(!ListView_GetItemState(target, 0, LVIS_DROPHILITED));
        assert(ListView_GetItemState(target, 0, LVIS_SELECTED));
        highlight.set(other_target, 99);
        assert(!ListView_GetItemState(other_target, 1, LVIS_DROPHILITED));
        finder::DragImage feedback;
        assert(feedback.begin(list, 0, grab, position, desktop));
        highlight.set(target, 1);
        check_overlay(feedback);
        highlight.clear();
        assert(!ListView_GetItemState(target, 1, LVIS_DROPHILITED));
        highlight.set(target, 1);
    }
    assert(!ListView_GetItemState(target, 1, LVIS_DROPHILITED));
    {
        finder::DragHighlight highlight;
        highlight.set(other_target, 0);
        DestroyWindow(other_target);
        highlight.clear();
    }
    assert(!finder::drag_target_allowed(first, {0}, first, 0, true));
    assert(finder::drag_target_allowed(first, {0}, first, 1, true));
    assert(finder::drag_target_allowed(first, {0}, second, 0, true));
    assert(!finder::drag_target_allowed(first, {0}, second, 0, false));
    assert(!finder::drag_target_allowed(first, {0}, second, -1, true));
    assert(!finder::drag_target_allowed(first, {0}, nullptr, 0, true));
    assert(!finder::drag_copies(true, false, false));
    assert(finder::drag_copies(false, false, false));
    assert(finder::drag_copies(true, true, false));
    assert(finder::drag_copies(false, true, false));
    assert(!finder::drag_copies(false, false, true));
    assert(!finder::drag_copies(true, false, true));
    assert(finder::drag_copies(true, true, true));
    assert(finder::drag_copies(false, true, true));
    DestroyWindow(unrelated);
    DestroyWindow(popup);
    DestroyWindow(desktop);
    ImageList_Destroy(images);
    std::cout
        << "PASS: folder-local and cross-folder drag routing, desktop drops, nested controls, popup rejection, enumeration independence, composited drag alpha/hotspot/repaint/movement/lifetime, transient destination highlighting and selection preservation\n";
}
