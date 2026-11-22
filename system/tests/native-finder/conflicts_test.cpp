/*+===================================================================
  File:      conflicts_test.cpp

  Summary:   Verify transfer conflict decisions and keyboard routing in owned classic notice windows.

  Classes:   None.

  Functions: main, answer_notice

  Origin:    Executor native Finder regression fixture.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#include "finder/classic.h"
#include "assets.inc"
#include <cassert>
#include <algorithm>
#include <commctrl.h>
#include <iostream>

// Fail unattended probes without opening a Windows assertion dialog.
#undef assert
#define assert(condition)                                               \
    do {                                                                \
        if (!(condition)) {                                             \
            std::cerr << __LINE__ << ": " << #condition << " failed\n"; \
            std::exit(2);                                               \
        }                                                               \
    } while (false)

namespace {
HWND owner;
int action;
UINT_PTR timer;
unsigned delivered_clicks = 0;

LRESULT CALLBACK count_click(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (message == WM_LBUTTONDOWN) {
        ++delivered_clicks;
        return 0;
    }
    return DefSubclassProc(window, message, wp, lp);
}

void CALLBACK answer_notice(HWND, UINT, UINT_PTR, DWORD)
{
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM) -> BOOL {
            wchar_t name[64]{};
            GetClassNameW(window, name, 64);
            if (wcscmp(name, L"FinderNotice") || GetWindow(window, GW_OWNER) != owner)
                return TRUE;
            KillTimer(nullptr, timer);
            assert(!IsWindowEnabled(owner));
            for (int id : {IDYES, IDNO, IDCANCEL})
                assert(GetDlgItem(window, id));
            wchar_t label[32]{};
            GetWindowTextW(GetDlgItem(window, IDYES), label, 32);
            assert(wcscmp(label, L"Replace") == 0);
            GetWindowTextW(GetDlgItem(window, IDNO), label, 32);
            assert(wcscmp(label, L"Skip") == 0);
            GetWindowTextW(GetDlgItem(window, IDCANCEL), label, 32);
            assert(wcscmp(label, L"Stop") == 0);
            if (action == VK_ESCAPE)
                PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
            else if (action == VK_RETURN) {
                SetFocus(GetDlgItem(window, IDNO));
                PostMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
            }
            else
                PostMessageW(window, WM_COMMAND, action, 0);
            return FALSE;
        },
        0);
}
}

// Bulk list rebuilding and hiding the completed progress window must not
// present intermediate WDEF/CDEF/desktop pixels, even with nested refreshes.
void check_redraw_batch(HWND parent)
{
    INITCOMMONCONTROLSEX common{sizeof common, ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&common);
    HWND frame = CreateWindowExW(0, L"STATIC", L"Folder", WS_CHILD | WS_VISIBLE | WS_CAPTION | WS_THICKFRAME, 10, 10,
                                 400, 280, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(frame);
    finder::classic::decorate(frame);
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_ICON, 0, 18, 360, 220, frame, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    assert(list);
    SetWindowSubclass(list, count_click, 7, 0);
    finder::classic::style_list(list);
    finder::classic::style_desktop(list);
    RECT before{}, after{};
    GetWindowRect(list, &before);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 500;
    info.bmiHeader.biHeight = -400;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* data;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    assert(bitmap && dc);
    auto previous = SelectObject(dc, bitmap);
    auto* pixels = static_cast<unsigned*>(data);
    constexpr unsigned marker = 0x00345678;
    std::fill_n(pixels, 500 * 400, marker);
    {
        finder::classic::RedrawBatch batch(frame);
        assert(finder::classic::redraw_suspended(parent));
        {
            finder::classic::RedrawBatch nested(list);
            SendMessageW(list, WM_SETREDRAW, FALSE, 0);
            for (int pass = 0; pass < 3; ++pass) {
                ListView_DeleteAllItems(list);
                for (int row = 0; row < 20; ++row) {
                    LVITEMW item{};
                    item.mask = LVIF_TEXT;
                    item.iItem = row;
                    item.pszText = const_cast<wchar_t*>(L"Copied item");
                    ListView_InsertItem(list, &item);
                    ListView_SetItemPosition(list, row, (row % 5) * 80, (row / 5) * 60);
                }
                ShowScrollBar(list, SB_BOTH, TRUE);
            }
            finder::classic::paint_window_chrome(frame, dc);
            finder::classic::paint_list_scrollbars(list, dc);
            SendMessageW(list, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
            SendMessageW(list, WM_SETREDRAW, TRUE, 0);
            GdiFlush();
            assert(std::all_of(pixels, pixels + 500 * 400, [](unsigned pixel) { return pixel == marker; }));
        }
        assert(finder::classic::redraw_suspended(list));
    }
    assert(!finder::classic::redraw_suspended(list));
    assert(!(GetWindowLongPtrW(parent, GWL_STYLE) & WS_VISIBLE));
    GetWindowRect(list, &after);
    assert(EqualRect(&before, &after));
    SendMessageW(list, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
    GdiFlush();
    assert(std::any_of(pixels, pixels + 500 * 400, [](unsigned pixel) { return pixel != marker; }));
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    {
        finder::classic::RedrawBatch batch(parent);
        // Batch painting must not hide Finder or its controls from Windows
        // hit testing; queued clicks still belong to this application.
        assert(IsWindowVisible(parent));
        SendMessageW(parent, WM_SETREDRAW, FALSE, 0);
        assert(IsWindowVisible(parent));
        assert(IsWindowVisible(frame));
        assert(IsWindowVisible(list));
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        assert(IsWindowVisible(list));
        assert(IsWindowEnabled(list));
        assert(PostMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20)));
        MSG click;
        assert(PeekMessageW(&click, list, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_REMOVE));
        DispatchMessageW(&click);
        assert(delivered_clicks == 1);
        POINT point{450, 250};
        assert(ChildWindowFromPointEx(parent, point, CWP_SKIPINVISIBLE) == parent);
        {
            finder::classic::RedrawBatch nested(frame);
            ShowWindow(frame, SW_HIDE);
        }
        assert(finder::classic::redraw_suspended(parent));
        assert(IsWindowVisible(parent));
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    }
    assert(GetWindowLongPtrW(parent, GWL_STYLE) & WS_VISIBLE);
    assert(!(GetWindowLongPtrW(frame, GWL_STYLE) & WS_VISIBLE));
    ShowWindow(parent, SW_HIDE);
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
    DestroyWindow(frame);
    // Repainting the desktop must not cover any document sibling, even before
    // the final hierarchy paint reaches that document.
    HWND canvas = CreateWindowExW(0, L"STATIC", L"Desktop", WS_CHILD | WS_VISIBLE, 0, 0, 300, 200, parent, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    HWND overlay = CreateWindowExW(0, L"STATIC", L"Document", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 40, 40, 80, 80,
                                   parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(canvas && overlay);
    finder::classic::style_desktop(canvas);
    assert(GetWindowLongPtrW(canvas, GWL_STYLE) & WS_CLIPSIBLINGS);
    SetWindowPos(overlay, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(parent, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    HDC screen = GetDC(canvas);
    assert(screen);
    assert(!PtVisible(screen, 60, 60));
    ReleaseDC(canvas, screen);
    ShowWindow(parent, SW_HIDE);
    DestroyWindow(overlay);
    DestroyWindow(canvas);
    std::cout
        << "PASS: nested completion redraw, no intermediate pixels, stable gutters, preserved hit testing/visibility and desktop sibling clipping\n";
}

int main()
{
    finder::classic::initialize(font_chicago, sizeof font_chicago, font_geneva, sizeof font_geneva);
    owner = CreateWindowExW(0, L"STATIC", L"Conflict fixture owner", WS_OVERLAPPEDWINDOW, 100, 100, 500, 300, nullptr,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(owner);
    check_redraw_batch(owner);
    for (int requested : {IDYES, IDNO, IDCANCEL, int(VK_RETURN), int(VK_ESCAPE)}) {
        action = requested;
        timer = SetTimer(nullptr, 0, 10, answer_notice);
        assert(timer);
        int answer = finder::classic::conflict_box(owner, L"An item already exists. Replace it?");
        assert(answer == (requested == VK_ESCAPE ? IDCANCEL : requested == VK_RETURN ? IDNO : requested));
        assert(IsWindowEnabled(owner));
    }
    DestroyWindow(owner);
    finder::classic::finish();
    std::cout << "PASS: classic Replace/Skip/Stop decisions, Escape, focused Enter and owner restoration\n";
}
