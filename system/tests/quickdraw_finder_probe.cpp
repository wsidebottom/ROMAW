/*+===================================================================
  File:      quickdraw_finder_probe.cpp

  Summary:   The native Finder adapter must preserve client pixels while presenting the very same WDEF/CDEF output as a
             direct call to the host QuickDraw renderer.

  Classes:   Used: ROMAWClassicGraphics, ROMAWGraphicsMetrics, ROMAWGraphicsRequest, std::vector

  Functions: ROMAWGraphicsProbe

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* The native Finder adapter must preserve client pixels while presenting the
   very same WDEF/CDEF output as a direct call to the host QuickDraw renderer. */
#include <stdint.h>
#include "finder/actions.h"
#include "finder/classic.h"
#include "finder/views.h"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include "assets.inc"
#include "label_art.inc"

#define CHECK(test)                                                       \
    do {                                                                  \
        if (!(test)) {                                                    \
            std::fprintf(stderr, "QUICKDRAW_FINDER_FAIL:%d\n", __LINE__); \
            return __LINE__;                                              \
        }                                                                 \
    } while (0)

// The same reference assets as Finder; ownership remains in this module.
static HICON label_icon(const unsigned char* rgba)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    uint32_t* pixels = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!color)
        return nullptr;
    unsigned char mask[128]{};
    for (unsigned i = 0; i < 1024; ++i) {
        unsigned alpha = rgba[i * 4 + 3];
        pixels[i] = (alpha << 24) | ((rgba[i * 4] * alpha / 255) << 16) | ((rgba[i * 4 + 1] * alpha / 255) << 8) |
                    (rgba[i * 4 + 2] * alpha / 255);
        if (!alpha)
            mask[i / 8] |= 0x80 >> (i & 7);
    }
    HBITMAP mono = CreateBitmap(32, 32, 1, 1, mask);
    ICONINFO icon{};
    icon.fIcon = TRUE;
    icon.hbmColor = color;
    icon.hbmMask = mono;
    HICON result = mono ? CreateIconIndirect(&icon) : nullptr;
    if (mono)
        DeleteObject(mono);
    DeleteObject(color);
    return result;
}

static LRESULT CALLBACK decorated_window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_NCCREATE)
        finder::classic::decorate(window);
    return DefWindowProcW(window, message, wp, lp);
}

// Drive only the Find window created by this fixture. The dialog still runs
// its normal Windows message loop, focus handling and resource controls.
struct FindDialogCheck {
    HWND owner;
    HWND dialog = nullptr;
    unsigned phase = 0;
    bool valid = true;
    bool driving = false;
};

static FindDialogCheck* find_check;

static void CALLBACK check_find_dialog(HWND, UINT, UINT_PTR, DWORD)
{
    if (!find_check)
        return;
    if (!find_check->dialog)
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM param) -> BOOL {
                auto& check = *reinterpret_cast<FindDialogCheck*>(param);
                wchar_t name[32]{};
                if (GetWindow(window, GW_OWNER) != check.owner || !GetClassNameW(window, name, std::size(name)) ||
                    wcscmp(name, L"FinderNotice"))
                    return TRUE;
                check.dialog = window;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(find_check));
    HWND window = find_check->dialog;
    if (!window || !IsWindowVisible(window))
        return;
    constexpr int field = 11052, value = 11054, batch = 11055, toggle = 11057;
    if (!GetDlgItem(window, toggle))
        return;
    wchar_t title[32]{};
    auto& check = *find_check;
    if (check.driving)
        return;
    check.driving = true;
    GetDlgItemTextW(window, toggle, title, std::size(title));
    if (check.phase < 3) {
        bool simple = check.phase != 1;
        bool layout = (GetDlgItem(window, field) == nullptr) == simple &&
                      !wcscmp(title, simple ? L"More Choices" : L"Fewer Choices");
        if (!layout)
            std::fprintf(stderr, "Find layout mismatch after transition %u, toggle title '%ls'\n", check.phase, title);
        check.valid &= layout;
        ++check.phase;
        SendMessageW(window, WM_COMMAND, toggle, 0);
        check.driving = false;
        return;
    }
    check.valid &= GetDlgItem(window, field) != nullptr && !wcscmp(title, L"Fewer Choices");
    SetDlgItemTextW(window, value, L"Needle");
    SendDlgItemMessageW(window, batch, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(window, WM_COMMAND, IDOK, 0);
    check.driving = false;
}

extern "C" __declspec(dllexport) int32_t ROMAWGraphicsProbe(void)
{
    FARPROC symbol = GetProcAddress(GetModuleHandleW(nullptr), "romaw_classic_graphics");
    CHECK(symbol);
    const ROMAWClassicGraphics* (*factory)();
    std::memcpy(&factory, &symbol, sizeof factory);
    const auto* graphics = factory();
    CHECK(graphics && graphics->size == sizeof *graphics);
    INITCOMMONCONTROLSEX common{sizeof common, ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&common);
    finder::classic::initialize(font_chicago, sizeof font_chicago, font_geneva, sizeof font_geneva);
    finder::classic::use_graphics(graphics);
    const unsigned char* swatches[] = {label_1_rgba, label_2_rgba, label_3_rgba, label_4_rgba,
                                       label_5_rgba, label_6_rgba, label_7_rgba};
    HICON label_icons[7]{};
    for (unsigned i = 0; i < 7; ++i) {
        label_icons[i] = label_icon(swatches[i]);
        CHECK(label_icons[i]);
    }
    finder::classic::artwork(nullptr, nullptr, nullptr, nullptr, label_icons);
    void* label_memory = nullptr;
    HBITMAP label_bitmap = finder::classic::label_menu_bitmap(&label_memory);
    CHECK(label_bitmap && label_memory);
    auto* label_pixels = static_cast<const uint32_t*>(label_memory);
    auto glyph_hash = [&](RECT bounds) {
        uint32_t hash = 2166136261u;
        for (int y = bounds.top; y < bounds.bottom; ++y)
            for (int x = bounds.left; x < bounds.right; ++x) {
                uint32_t pixel = label_pixels[y * 145 + x];
                bool ink = ((pixel >> 16) & 255) < 128 && ((pixel >> 8) & 255) < 128 && (pixel & 255) < 128;
                hash = (hash ^ unsigned(ink)) * 16777619u;
            }
        return hash;
    };
    // Existing classic-reference masks, now checked inside the aggregate app.
    const uint32_t expected_glyphs[] = {0x89666720, 0x89efb0de, 0xdaaee233, 0xa37ee976,
                                        0xe8b94385, 0x615f2e2c, 0x4a523bce, 0x6a971e86};
    for (unsigned i = 0; i < 8; ++i) {
        RECT bounds = i ? RECT{43, 31 + int(i - 1) * 16, 141, 47 + int(i - 1) * 16} : RECT{15, 1, 100, 17};
        auto actual = glyph_hash(bounds);
        if (actual != expected_glyphs[i])
            std::fprintf(stderr, "LABEL_GLYPH_%u:%08x expected %08x\n", i, actual, expected_glyphs[i]);
        CHECK(actual == expected_glyphs[i]);
    }
    CHECK(glyph_hash({3, 3, 14, 13}) == 0xcd2e1097u);
    // Keep the user-edited label-menu.r palette. Geometry/glyph masks come
    // from the fixed classic reference; swatch RGB comes from current artwork.
    for (unsigned i = 0; i < 7; ++i) {
        const auto* center = swatches[i] + (16 * 32 + 16) * 4;
        uint32_t expected_color = (uint32_t(center[0]) << 16) | (uint32_t(center[1]) << 8) | center[2];
        CHECK(center[3] == 255);
        for (unsigned y = 33 + i * 16; y < 45 + i * 16; ++y)
            for (unsigned x = 15; x < 39; ++x) {
                if ((label_pixels[y * 145 + x] & 0xffffff) != expected_color)
                    std::fprintf(stderr, "LABEL_SWATCH_%u:%u,%u=%06x expected %06x\n", i, x, y,
                                 label_pixels[y * 145 + x] & 0xffffff, expected_color);
                CHECK((label_pixels[y * 145 + x] & 0xffffff) == expected_color);
            }
    }
    DeleteObject(label_bitmap);
    HWND root = CreateWindowExW(0, L"STATIC", L"QuickDraw adapter fixture", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 20,
                                20, 600, 450, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(root);
    finder::classic::decorate(root, true);
    HMENU model = CreateMenu(), file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, 1, L"Open;(&&/!x\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(model, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    finder::classic::style_menu(model);
    RECT before, after;
    GetClientRect(root, &before);
    finder::classic::attach_menu(root, model);
    GetClientRect(root, &after);
    CHECK(!GetMenu(root) && finder::classic::window_menu(root) == model);
    CHECK(before.bottom - after.bottom == 19);
    POINT menu_point{15, -10};
    ClientToScreen(root, &menu_point);
    CHECK(SendMessageW(root, WM_NCHITTEST, 0, MAKELPARAM(menu_point.x, menu_point.y)) == HTMENU);

    HWND child = CreateWindowExW(0, L"STATIC", L"Document",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_CAPTION | WS_SYSMENU |
                                     WS_THICKFRAME,
                                 30, 30, 400, 300, root, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(child);
    finder::classic::decorate(child);
    SetWindowPos(child, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    RECT client;
    GetClientRect(child, &client);
    CHECK(client.right == 398 && client.bottom == 280);
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_ICON, 0, 18, client.right,
                                client.bottom - 18, child, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(list);
    finder::classic::style_list(list);
    for (int i = 0; i < 20; ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(L"Item");
        ListView_InsertItem(list, &item);
        ListView_SetItemPosition(list, i, 20 + (i % 3) * 80, 20 + (i / 3) * 64);
    }
    finder::classic::activate(child);
    WNDCLASSW temporary_class{};
    temporary_class.hInstance = GetModuleHandleW(nullptr);
    temporary_class.lpszClassName = L"QuickDrawDecoratedFixture";
    temporary_class.lpfnWndProc = decorated_window_proc;
    CHECK(RegisterClassW(&temporary_class));
    HWND temporary =
        CreateWindowExW(0, temporary_class.lpszClassName, L"Temporary", WS_CHILD | WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
                        40, 40, 100, 80, root, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(temporary);
    finder::classic::activate(temporary);
    CHECK(finder::classic::active_window() == temporary);
    DestroyWindow(temporary);
    CHECK(finder::classic::active_window() == child);
    MSG activation{};
    CHECK(PeekMessageW(&activation, child, WM_APP, WM_APP + 0x7fff, PM_REMOVE));
    DispatchMessageW(&activation);
    CHECK(finder::classic::active_window() == child);
    CHECK(UnregisterClassW(temporary_class.lpszClassName, temporary_class.hInstance));
    RECT viewport, list_bounds, child_bounds;
    GetClientRect(list, &viewport);
    GetWindowRect(finder::classic::scrollbar_window(list), &list_bounds);
    GetWindowRect(child, &child_bounds);
    const int width = list_bounds.right - list_bounds.left, height = list_bounds.bottom - list_bounds.top;
    CHECK(width - viewport.right == 16 && height - viewport.bottom == 16);
    ROMAWGraphicsRequest request{};
    request.size = sizeof request;
    request.kind = ROMAW_GRAPHICS_DOCUMENT;
    request.key = 0xfeed;
    request.width = 400;
    request.height = 300;
    request.status_height = 18;
    request.flags = ROMAW_GRAPHICS_ACTIVE | ROMAW_GRAPHICS_CLOSE | ROMAW_GRAPHICS_ZOOM | ROMAW_GRAPHICS_SCROLLBARS;
    request.title = reinterpret_cast<const unsigned char*>("\x08"
                                                           "Document");
    request.vertical.pressed = request.horizontal.pressed = -1;
    for (bool vertical : {true, false}) {
        SCROLLINFO info{};
        info.cbSize = sizeof info;
        info.fMask = SIF_ALL;
        GetScrollInfo(list, vertical ? SB_VERT : SB_HORZ, &info);
        long long maximum =
            std::max<long long>(info.nMin, static_cast<long long>(info.nMax) - std::max(1u, info.nPage) + 1);
        int extent = vertical ? viewport.bottom + 1 : viewport.right + 1;
        auto& scroll = vertical ? request.vertical : request.horizontal;
        scroll.maximum = std::max(0, extent - 48);
        scroll.enabled = maximum > info.nMin && scroll.maximum > 0 &&
                         (GetWindowLongPtrW(list, GWL_STYLE) & (vertical ? WS_VSCROLL : WS_HSCROLL));
        if (maximum > info.nMin)
            scroll.value =
                int((static_cast<long long>(info.nPos) - info.nMin) * scroll.maximum / (maximum - info.nMin));
    }
    std::vector<uint32_t> expected(400 * 300);
    ROMAWGraphicsMetrics metrics;
    CHECK(!graphics->render(&request, expected.data(), 400 * 4, &metrics));
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    void* bits;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    CHECK(bitmap && dc);
    auto previous = SelectObject(dc, bitmap);
    auto* pixels = static_cast<uint32_t*>(bits);
    std::fill(pixels, pixels + width * height, 0x345678);
    finder::classic::paint_list_scrollbars(list, dc);
    GdiFlush();
    const int offset_x = list_bounds.left - child_bounds.left, offset_y = list_bounds.top - child_bounds.top;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            uint32_t want =
                x < viewport.right && y < viewport.bottom ? 0x345678 : expected[(y + offset_y) * 400 + x + offset_x];
            if ((pixels[y * width + x] & 0xffffff) != want) {
                std::fprintf(stderr, "gutter mismatch %d,%d: %x expected %x\n", x, y, pixels[y * width + x], want);
                CHECK(false);
            }
        }
    POINT close{15, 10};
    close.x += child_bounds.left;
    close.y += child_bounds.top;
    CHECK(SendMessageW(child, WM_NCHITTEST, 0, MAKELPARAM(close.x, close.y)) == HTCLOSE);

    // A buffered list can clear a larger area than its dirty-item callbacks.
    // Its owner must restore every visible image, including after scrolling,
    // rather than draw just the last few items Windows happened to notify.
    HIMAGELIST images = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 2, 0);
    CHECK(images);
    int normal_image = ImageList_AddIcon(images, LoadIconW(nullptr, IDI_APPLICATION));
    int selected_image = ImageList_AddIcon(images, LoadIconW(nullptr, IDI_WARNING));
    CHECK(normal_image >= 0 && selected_image >= 0);
    ListView_SetImageList(list, images, LVSIL_NORMAL);
    std::vector<finder::classic::IconViewImage> image_items(20, {normal_image, selected_image});
    for (int i = 0; i < 20; ++i) {
        LVITEMW item{};
        item.mask = LVIF_IMAGE;
        item.iItem = i;
        item.iImage = normal_image;
        CHECK(ListView_SetItem(list, &item));
    }
    ListView_SetItemState(list, 1, LVIS_SELECTED, LVIS_SELECTED);
    CHECK(finder::selected_item_after(list, 20, -1) == 1);
    CHECK(finder::selected_item_after(list, 20, 1) == -1);
    CHECK(finder::selected_item_after(list, 1, -1) == -1);
    CHECK(finder::selected_item_after(nullptr, 20, -1) == -1);
    CHECK(finder::selected_item_after(root, 20, -1) == -1);
    POINT drag_origin{};
    HBITMAP drag_art = finder::classic::drag_artwork(list, images, selected_image, 1, L"QuickDraw drag", drag_origin);
    CHECK(drag_art);
    BITMAP drag_bits{};
    CHECK(GetObjectW(drag_art, sizeof drag_bits, &drag_bits));
    CHECK(drag_bits.bmWidth >= 32 && drag_bits.bmHeight >= 32 && drag_bits.bmBitsPixel == 32 && drag_bits.bmBits);
    auto* drag_pixels = static_cast<const uint32_t*>(drag_bits.bmBits);
    unsigned opaque = 0, transparent = 0;
    for (int i = 0; i < drag_bits.bmWidth * drag_bits.bmHeight; ++i) {
        opaque += (drag_pixels[i] >> 24) == 255;
        transparent += (drag_pixels[i] >> 24) == 0;
    }
    CHECK(opaque > 20 && transparent > 20);
    DeleteObject(drag_art);
    for (int scroll : {0, 128}) {
        if (scroll)
            ListView_Scroll(list, 0, scroll);
        GetClientRect(list, &viewport);
        std::fill_n(pixels, width * height, 0x345678);
        finder::classic::paint_icon_view(list, dc, images, image_items.data(), image_items.size(), false);
        GdiFlush();
        unsigned visible_images = 0;
        RECT partial{};
        for (int i = 0; i < 20; ++i) {
            RECT image{};
            CHECK(ListView_GetItemRect(list, i, &image, LVIR_ICON));
            int left = image.left + (image.right - image.left - 32) / 2;
            if (left < 0 || left + 32 > viewport.right || image.top < 0 || image.top + 32 > viewport.bottom)
                continue;
            unsigned ink = 0;
            for (int y = image.top; y < image.top + 32; ++y)
                for (int x = left; x < left + 32; ++x)
                    ink += (pixels[y * width + x] & 0xffffff) != 0xffffff;
            CHECK(ink > 20);
            partial = {left, image.top, left + 32, image.top + 32};
            ++visible_images;
        }
        CHECK(visible_images >= 6);
        std::vector<uint32_t> complete(pixels, pixels + width * height);
        // Simulate the bad buffered clear with no item callbacks to restore it.
        FillRect(dc, &viewport, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        finder::classic::paint_icon_view(list, dc, images, image_items.data(), image_items.size(), false);
        GdiFlush();
        for (int i = 0; i < width * height; ++i)
            CHECK((pixels[i] & 0xffffff) == (complete[i] & 0xffffff));
        // A small update must also retain every pixel outside its clip.
        int clip = SaveDC(dc);
        IntersectClipRect(dc, partial.left, partial.top, partial.right, partial.bottom);
        FillRect(dc, &partial, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        finder::classic::paint_icon_view(list, dc, images, image_items.data(), image_items.size(), false);
        RestoreDC(dc, clip);
        GdiFlush();
        for (int i = 0; i < width * height; ++i)
            CHECK((pixels[i] & 0xffffff) == (complete[i] & 0xffffff));
    }
    ListView_SetImageList(list, nullptr, LVSIL_NORMAL);
    ImageList_Destroy(images);

    // Finder 7's update-region contract: complete view drawing happens once,
    // only invalid pixels are published, and independently painted children
    // (column headers/rename fields) keep their own pixels.
    HWND field = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 18, 18, 10, 10, list, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    CHECK(field);
    std::fill_n(pixels, width * height, 0x345678);
    int update_clip = SaveDC(dc);
    IntersectClipRect(dc, 10, 10, 40, 40);
    finder::classic::paint_view(
        list, dc,
        [](HDC canvas, void* context) {
            RECT bounds{};
            GetClientRect(static_cast<HWND>(context), &bounds);
            finder::classic::fill_color(canvas, bounds, RGB(0x12, 0x34, 0x56));
        },
        list);
    RestoreDC(dc, update_clip);
    GdiFlush();
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            bool updated = x >= 10 && x < 40 && y >= 10 && y < 40 && !(x >= 18 && x < 28 && y >= 18 && y < 28);
            CHECK((pixels[y * width + x] & 0xffffff) == (updated ? 0x123456 : 0x345678));
        }
    DestroyWindow(field);
    CHECK(finder::selected_item_after(field, 20, -1) == -1);
    std::fill_n(pixels, width * height, 0x345678);
    finder::classic::paint_view(
        list, dc,
        [](HDC canvas, void*) {
            finder::classic::fill_color(canvas, {0, 0, 20, 20}, RGB(0, 0, 0));
            throw 1;
        },
        nullptr);
    for (int i = 0; i < width * height; ++i)
        CHECK((pixels[i] & 0xffffff) == 0x345678);

    // Actual WM_MEASUREITEM/WM_DRAWITEM dispatch must use StandardMDEF metrics
    // and pixels, including transitions through the native row's paint cache.
    MENUITEMINFOW entry{};
    entry.cbSize = sizeof entry;
    entry.fMask = MIIM_DATA;
    CHECK(GetMenuItemInfoW(file, 0, TRUE, &entry));
    ROMAWGraphicsRequest row{};
    row.size = sizeof row;
    row.kind = ROMAW_GRAPHICS_MENU_ITEM;
    row.key = 0xf00d;
    row.width = 240;
    row.height = 16;
    row.title = reinterpret_cast<const unsigned char*>("\x0a"
                                                       "Open;(&/!x");
    row.shortcut = reinterpret_cast<const unsigned char*>("\6Ctrl+O");
    row.flags = ROMAW_GRAPHICS_ITEM_ENABLED;
    ROMAWGraphicsMetrics measured{};
    CHECK(!graphics->render(&row, nullptr, 0, &measured));
    MEASUREITEMSTRUCT measure{};
    measure.CtlType = ODT_MENU;
    measure.itemData = entry.dwItemData;
    CHECK(SendMessageW(root, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)));
    CHECK(measure.itemHeight == unsigned(measured.menu_item_height));
    CHECK(measure.itemWidth == unsigned(measured.menu_item_width));
    std::vector<uint32_t> expected_row(240 * 16);
    for (unsigned state : {0u, unsigned(ODS_SELECTED), unsigned(ODS_DISABLED | ODS_SELECTED), unsigned(ODS_CHECKED)}) {
        row.flags = state & ODS_DISABLED ? 0 : ROMAW_GRAPHICS_ITEM_ENABLED;
        if (state & ODS_SELECTED)
            row.flags |= ROMAW_GRAPHICS_ITEM_SELECTED;
        if (state & ODS_CHECKED)
            row.flags |= ROMAW_GRAPHICS_ITEM_CHECKED;
        CHECK(!graphics->render(&row, expected_row.data(), 240 * 4, &measured));
        std::fill(pixels, pixels + width * height, 0x345678);
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_MENU;
        draw.itemData = entry.dwItemData;
        draw.itemState = state;
        draw.hDC = dc;
        draw.rcItem = {0, 0, 240, 16};
        CHECK(SendMessageW(root, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw)));
        GdiFlush();
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                CHECK((pixels[y * width + x] & 0xffffff) == (x < 240 && y < 16 ? expected_row[y * 240 + x] : 0x345678));
    }
    graphics->release(row.key);
    // Database refresh can replace a cached HICON that Get Info/title controls
    // still borrow. Update those references before its owner destroys it.
    HICON old_icon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    HICON new_icon = CopyIcon(LoadIconW(nullptr, IDI_WARNING));
    CHECK(old_icon && new_icon);
    HWND notice = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_ICON, 0, 0, 32, 32, root, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    CHECK(notice);
    SendMessageW(root, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(old_icon));
    SendMessageW(notice, STM_SETICON, reinterpret_cast<WPARAM>(old_icon), 0);
    finder::classic::replace_icon(old_icon, new_icon);
    CHECK(reinterpret_cast<HICON>(SendMessageW(root, WM_GETICON, ICON_SMALL, 0)) == new_icon);
    CHECK(reinterpret_cast<HICON>(SendMessageW(notice, STM_GETICON, 0, 0)) == new_icon);
    DestroyIcon(old_icon);
    // A 12px Get Info row uses the exact same CDEF pixels as guest controls.
    // Styling must retain the native automatic-checkbox state/input model.
    HWND checkbox = CreateWindowW(L"BUTTON", L"", WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 64, 12, root, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    CHECK(checkbox);
    finder::classic::style_checkbox(checkbox);
    ROMAWGraphicsRequest check_request{};
    check_request.size = sizeof check_request;
    check_request.kind = ROMAW_GRAPHICS_CHECKBOX;
    check_request.key = 0xfade;
    check_request.width = 16;
    check_request.height = 12;
    std::vector<uint32_t> expected_check(16 * 12);
    for (bool enabled : {true, false})
        for (bool checked : {true, false}) {
            EnableWindow(checkbox, enabled);
            SendMessageW(checkbox, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
            check_request.flags =
                (enabled ? ROMAW_GRAPHICS_ITEM_ENABLED : 0) | (checked ? ROMAW_GRAPHICS_ITEM_CHECKED : 0);
            CHECK(!graphics->render(&check_request, expected_check.data(), 16 * 4, &metrics));
            std::fill_n(pixels, width * height, 0x345678);
            SendMessageW(checkbox, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            GdiFlush();
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < 16; ++x)
                    CHECK((pixels[y * width + x] & 0xffffff) == expected_check[y * 16 + x]);
        }
    EnableWindow(checkbox, TRUE);
    SendMessageW(checkbox, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(checkbox, BM_CLICK, 0, 0);
    CHECK(SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
    SendMessageW(checkbox, BM_CLICK, 0, 0);
    CHECK(SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    graphics->release(check_request.key);
    DestroyWindow(checkbox);
    // Native text must preserve clipping/background while using the exact
    // Geneva glyphs supplied by the initialized QuickDraw Font Manager.
    SelectObject(dc, finder::classic::small_font());
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    RECT text_bounds{4, 4, 104, 16};
    std::fill_n(pixels, width * height, 0x345678);
    finder::classic::draw_text(dc, L"Finder File", -1, &text_bounds, DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    ROMAWGraphicsRequest text_request{};
    text_request.size = sizeof text_request;
    text_request.kind = ROMAW_GRAPHICS_TEXT;
    text_request.key = 0xf11e;
    text_request.width = 100;
    text_request.height = 12;
    text_request.font = 3;
    text_request.font_size = 9;
    text_request.text = (const unsigned char*)"Finder File";
    text_request.text_length = 11;
    text_request.foreground = 0;
    text_request.background = 0x345678;
    std::vector<uint32_t> expected_text(100 * 12);
    CHECK(!graphics->render(&text_request, expected_text.data(), 100 * 4, &metrics));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            CHECK((pixels[y * width + x] & 0xffffff) ==
                  (x >= 4 && x < 104 && y >= 4 && y < 16 ? expected_text[(y - 4) * 100 + x - 4] : 0x345678));
    RECT text_measured{0, 0, 100, 12};
    finder::classic::draw_text(dc, L"Finder File", -1, &text_measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    CHECK(text_measured.right == metrics.text_width && text_measured.bottom == metrics.text_height);
    // List-view labels use Windows font metrics. Their selection rectangle
    // must instead contain the complete QuickDraw baseline and descenders.
    RECT native_label{4, 4, 104, 8};
    auto label_bounds = finder::classic::icon_label_bounds(dc, L"Temporary Items", native_label);
    CHECK(label_bounds.top == 4 && (label_bounds.left + label_bounds.right) / 2 == 54);
    CHECK(label_bounds.bottom - label_bounds.top == metrics.text_height);
    std::fill_n(pixels, width * height, 0x345678);
    FillRect(dc, &label_bounds, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(dc, RGB(255, 255, 255));
    finder::classic::draw_text(dc, L"Temporary Items", -1, &label_bounds, DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    text_request.height = label_bounds.bottom - label_bounds.top;
    text_request.width = label_bounds.right - label_bounds.left;
    text_request.text = (const unsigned char*)"Temporary Items";
    text_request.text_length = 15;
    text_request.foreground = 0xffffff;
    text_request.background = 0;
    expected_text.resize(text_request.width * text_request.height);
    CHECK(!graphics->render(&text_request, expected_text.data(), text_request.width * 4, &metrics));
    CHECK(text_request.width == metrics.text_width + 2);
    auto long_label = finder::classic::icon_label_bounds(dc, L"System 9 Folder Temporary Items", {90, 4, 110, 8});
    RECT long_measured{};
    finder::classic::draw_text(dc, L"System 9 Folder Temporary Items", -1, &long_measured,
                               DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    CHECK(long_label.right - long_label.left == long_measured.right + 2);
    CHECK(long_label.left < 90 && long_label.right > 110);
    RECT label_viewport{0, 0, 200, 100};
    auto left_edge = finder::classic::icon_label_bounds(dc, L"Temporary Items", {0, 4, 20, 8}, &label_viewport);
    auto right_edge = finder::classic::icon_label_bounds(dc, L"Temporary Items", {180, 4, 200, 8}, &label_viewport);
    CHECK(left_edge.left == 0 && left_edge.right == text_request.width);
    CHECK(right_edge.right == 200 && right_edge.left == 200 - text_request.width);
    auto offscreen = finder::classic::icon_label_bounds(dc, L"Temporary Items", {-200, 4, -180, 8}, &label_viewport);
    CHECK(offscreen.right < 0); // Scrolling an icon out must not pin its name to the window edge.
    unsigned selected_ink = 0, descender_ink = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            bool inside = x >= label_bounds.left && x < label_bounds.right && y >= 4 && y < label_bounds.bottom;
            uint32_t expected = inside ? expected_text[(y - 4) * text_request.width + x - label_bounds.left] : 0x345678;
            CHECK((pixels[y * width + x] & 0xffffff) == expected);
            if (inside && expected) {
                ++selected_ink;
                if (y >= 4 + metrics.text_ascent)
                    ++descender_ink;
            }
        }
    CHECK(selected_ink > 20 && descender_ink > 0);
    // The edit model keeps text, selection and keyboard behavior after styling.
    HWND edit = CreateWindowExW(0, L"EDIT", L"original", WS_CHILD | WS_BORDER | ES_MULTILINE, 0, 0, 180, 50, root,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(edit);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(finder::classic::small_font()), 0);
    finder::classic::style_text_control(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Finder File"));
    wchar_t edited[32]{};
    GetWindowTextW(edit, edited, std::size(edited));
    CHECK(wcscmp(edited, L"Finder File") == 0);
    SendMessageW(edit, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    unsigned edit_ink = 0;
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 180; ++x)
            edit_ink += (pixels[y * width + x] & 0xffffff) == 0;
    CHECK(edit_ink > 20);
    CHECK(IsWindow(edit));
    DestroyWindow(edit);
    FindDialogCheck dialog{root};
    find_check = &dialog;
    UINT_PTR timer = SetTimer(nullptr, 0, 15, check_find_dialog);
    CHECK(timer);
    finder::FindOptions find{{finder::FindField::name, finder::FindRelation::contains, L"initial"}};
    bool accepted = finder::classic::find_box(root, find, true, {});
    KillTimer(nullptr, timer);
    find_check = nullptr;
    if (!dialog.valid || dialog.phase != 3 || !accepted)
        std::fprintf(stderr, "Find dialog state after closing: phase=%u valid=%d accepted=%d\n", dialog.phase,
                     dialog.valid, accepted);
    CHECK(dialog.valid && dialog.phase == 3 && accepted && IsWindowEnabled(root));
    CHECK(find.expanded && find.all_at_once && find.query.text == L"Needle");
    graphics->release(text_request.key);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    graphics->release(request.key);
    DestroyWindow(root);
    DestroyIcon(new_icon);
    finder::classic::finish();
    for (auto icon : label_icons)
        DestroyIcon(icon);
    std::fprintf(
        stderr,
        "PASS: Finder chrome, activation, clipped view updates, artwork, selection lifetime, menus, text and More Choices\n");
    std::fprintf(stderr, "QUICKDRAW_FINDER_PROBE_OK\n");
    return 0;
}
