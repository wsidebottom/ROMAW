/*+===================================================================
  File:      quickdraw_host_probe.c

  Summary:   Exercise the actual host adapter inside the initialized Finder desktop.

  Classes:   None.

  Functions: ROMAWGraphicsProbe

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise the actual host adapter inside the initialized Finder desktop. */
#include <stdint.h>
#include "classic-ui/host_graphics.h"
#include "classic-ui/classic_ui.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* This generated resource include is shared with the C++ Finder frontend. */
#define constexpr const
#include "desktop_pattern.inc"
#undef constexpr

#define CHECK(test)                                                \
    do {                                                           \
        if (!(test)) {                                             \
            fprintf(stderr, "QUICKDRAW_HOST_FAIL:%d\n", __LINE__); \
            return __LINE__;                                       \
        }                                                          \
    } while (0)

static int write_bitmap(const char* path, const uint32_t* pixels, int width, int height, size_t stride)
{
    const char* directory = getenv("ROMAW_QD_ARTIFACTS");
    if (!directory || !*directory)
        return 1;
    char output[1024];
    int length = snprintf(output, sizeof output, "%s/%s", directory, path);
    if (length < 0 || (size_t)length >= sizeof output)
        return 0;
    FILE* file = fopen(output, "wb");
    if (!file)
        return 0;
    BITMAPFILEHEADER header = {0};
    BITMAPINFOHEADER info = {0};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof header + sizeof info;
    header.bfSize = header.bfOffBits + width * height * 4;
    info.biSize = sizeof info;
    info.biWidth = width;
    info.biHeight = -height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    int good = fwrite(&header, sizeof header, 1, file) == 1 && fwrite(&info, sizeof info, 1, file) == 1;
    for (int row = 0; good && row < height; ++row)
        good = fwrite((const char*)pixels + row * stride, 4, width, file) == (unsigned)width;
    return fclose(file) == 0 && good;
}

struct ThreadProbe {
    const ROMAWClassicGraphics* graphics;
    const ROMAWGraphicsRequest* request;
    const ROMAWClassicGraphics* (*factory)(void);
};

static DWORD WINAPI wrong_thread(void* data)
{
    struct ThreadProbe* probe = data;
    ROMAWGraphicsMetrics metrics;
    if (probe->factory() != NULL || probe->graphics->render(probe->request, NULL, 0, &metrics) != -50)
        return 1;
    probe->graphics->release(probe->request->key); // Must not destroy the UI thread's object.
    return 0;
}

__declspec(dllexport) int32_t ROMAWGraphicsProbe(void)
{
    FARPROC symbol = GetProcAddress(GetModuleHandleW(NULL), "romaw_classic_graphics");
    CHECK(symbol);
    const ROMAWClassicGraphics* (*factory)(void);
    memcpy(&factory, &symbol, sizeof factory);
    const ROMAWClassicGraphics* graphics = factory();
    CHECK(graphics && graphics->size == sizeof *graphics && graphics->version == ROMAW_GRAPHICS_ABI);
    ROMAWGraphicsRequest request = {0};
    request.size = sizeof request;
    request.kind = ROMAW_GRAPHICS_DOCUMENT;
    request.key = 100;
    request.width = 320;
    request.height = 240;
    request.status_height = 18;
    request.flags = ROMAW_GRAPHICS_ACTIVE | ROMAW_GRAPHICS_CLOSE | ROMAW_GRAPHICS_ZOOM | ROMAW_GRAPHICS_SCROLLBARS;
    request.title = (const unsigned char*)"\x0c"
                                          "Finder Probe";
    request.vertical = (ROMAWGraphicsScroll){0, 10, 5, 1, -1};
    request.horizontal = (ROMAWGraphicsScroll){0, 0, 0, 0, -1};
    size_t stride = 320 * 4 + 16, extent = stride * 240;
    uint32_t* pixels = malloc(extent + 16);
    CHECK(pixels);
    memset(pixels, 0xa5, extent + 16);
    ROMAWGraphicsMetrics metrics;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(metrics.client.left == 1 && metrics.client.top == 19 && metrics.client.right == 319 &&
          metrics.client.bottom == 239);
    CHECK(metrics.grow.left == 303 && metrics.grow.top == 223);
    CHECK(metrics.vertical.right - metrics.vertical.left == 16 &&
          metrics.horizontal.bottom - metrics.horizontal.top == 16);
    CHECK(metrics.vertical.bottom == metrics.grow.top + 1 && metrics.horizontal.right == metrics.grow.left + 1);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 320 * 4; x < stride; ++x)
            CHECK(((unsigned char*)pixels)[y * stride + x] == 0xa5);
    for (unsigned i = 0; i < 16; ++i)
        CHECK(((unsigned char*)pixels)[extent + i] == 0xa5);
    /* Measure a complete pattern period away from arrows, frame and thumb.
       Both palette values and the 3:1 light/dark coverage are reference data. */
    unsigned track_light = 0, track_dark = 0;
    for (int y = metrics.vertical.top + 24; y < metrics.vertical.top + 40; ++y)
        for (int x = metrics.vertical.left + 2; x < metrics.vertical.left + 10; ++x) {
            uint32_t pixel = *(uint32_t*)((char*)pixels + y * stride + x * 4);
            track_light += pixel == ROMAW_CLASSIC_SCROLL_TRACK_LIGHT_RGB;
            track_dark += pixel == ROMAW_CLASSIC_SCROLL_TRACK_DARK_RGB;
        }
    fprintf(stderr, "Scrollbar track: %u light, %u dark\n", track_light, track_dark);
    CHECK(track_light == 96 && track_dark == 32);
    for (int y = metrics.horizontal.top + 2; y < metrics.horizontal.top + 10; ++y)
        for (int x = metrics.horizontal.left + 24; x < metrics.horizontal.left + 40; ++x)
            CHECK(*(uint32_t*)((char*)pixels + y * stride + x * 4) == ROMAW_CLASSIC_SCROLL_TRACK_LIGHT_RGB);
    CHECK(write_bitmap("quickdraw-document.bmp", pixels, 320, 240, stride));
    CHECK(*(uint32_t*)((char*)pixels + stride + 4) == 0xccccff); // Preserve RGB order, not just gray pixels.
    struct ThreadProbe thread_probe = {graphics, &request, factory};
    HANDLE thread = CreateThread(NULL, 0, wrong_thread, &thread_probe, 0, NULL);
    CHECK(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    DWORD thread_result;
    CHECK(GetExitCodeThread(thread, &thread_result) && !thread_result);
    CloseHandle(thread);
    CHECK(graphics->document_hit(request.key, 15, 10) == ROMAW_GRAPHICS_CLOSE_HIT);
    CHECK(graphics->document_hit(request.key, 300, 10) == ROMAW_GRAPHICS_ZOOM_OUT);
    CHECK(graphics->document_hit(request.key, 100, 10) == ROMAW_GRAPHICS_DRAG);
    CHECK(graphics->document_hit(request.key, 100, 100) == ROMAW_GRAPHICS_CONTENT);
    CHECK(graphics->document_hit(request.key, 310, 230) == ROMAW_GRAPHICS_GROW);
    unsigned row = metrics.grow.top - 1;
    fprintf(stderr, "outer arrow pixels: %06x %06x %06x; end %06x/%06x\n",
            *(uint32_t*)((char*)pixels + 210 * stride + 317 * 4), *(uint32_t*)((char*)pixels + 210 * stride + 318 * 4),
            *(uint32_t*)((char*)pixels + 210 * stride + 319 * 4), *(uint32_t*)((char*)pixels + row * stride + 310 * 4),
            *(uint32_t*)((char*)pixels + (row + 1) * stride + 310 * 4));
    CHECK(*(uint32_t*)((char*)pixels + row * stride + 310 * 4) != 0);
    CHECK(*(uint32_t*)((char*)pixels + (row + 1) * stride + 310 * 4) == 0);
    request.vertical.pressed = 1;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(write_bitmap("quickdraw-document-pressed.bmp", pixels, 320, 240, stride));
    graphics->release(request.key);

    const unsigned char apple[] = {1, 20};
    const unsigned char file[] = {4, 'F', 'i', 'l', 'e'};
    const unsigned char edit[] = {4, 'E', 'd', 'i', 't'};
    const unsigned char view[] = {4, 'V', 'i', 'e', 'w'};
    const unsigned char label[] = {5, 'L', 'a', 'b', 'e', 'l'};
    const unsigned char special[] = {7, 'S', 'p', 'e', 'c', 'i', 'a', 'l'};
    const unsigned char help[] = {4, 'H', 'e', 'l', 'p'};
    ROMAWGraphicsTitle titles[] = {{apple, 1}, {file, 1}, {edit, 1}, {view, 1}, {label, 1}, {special, 1}, {help, 1}};
    request.kind = ROMAW_GRAPHICS_MENU_BAR;
    request.key = 101;
    request.height = 64;
    request.menus = titles;
    request.menu_count = 7;
    request.selected_menu = -1;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    fprintf(stderr, "QuickDraw menu height %d; titles:", metrics.menu_height);
    for (unsigned i = 0; i < 7; ++i)
        fprintf(stderr, " %d..%d", metrics.menu_titles[i].left, metrics.menu_titles[i].right);
    fprintf(stderr, "\n");
    CHECK(metrics.menu_height > 0 && metrics.menu_height <= 32 && metrics.menu_titles[0].left == 10);
    CHECK(write_bitmap("quickdraw-menu.bmp", pixels, 320, metrics.menu_height, stride));
    request.selected_menu = 1;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(write_bitmap("quickdraw-menu-selected.bmp", pixels, 320, metrics.menu_height, stride));
    graphics->release(request.key);
    request.kind = ROMAW_GRAPHICS_DESKTOP;
    request.menu_count = 0;
    request.width = 128;
    request.height = 128;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(write_bitmap("quickdraw-desktop.bmp", pixels, 128, 128, stride));
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            CHECK(*(uint32_t*)((char*)pixels + y * stride + x * 4) ==
                  (desktop_pattern_pixels[(y % desktop_pattern_height) * desktop_pattern_width +
                                          x % desktop_pattern_width] &
                   0xffffff));
    graphics->release(request.key);
    /* Cross the 14-bit PixMap rowBytes boundary. The pattern must keep its
       logical phase at the tile seam, and the far WDEF/CDEF edges must draw. */
    free(pixels);
    request.width = 5000;
    request.height = 128;
    stride = request.width * 4;
    pixels = malloc(stride * 240);
    CHECK(pixels);
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 5000; ++x)
            CHECK(pixels[y * 5000 + x] == (desktop_pattern_pixels[(y % desktop_pattern_height) * desktop_pattern_width +
                                                                  x % desktop_pattern_width] &
                                           0xffffff));
    graphics->release(request.key);
    request.kind = ROMAW_GRAPHICS_DOCUMENT;
    request.height = 240;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(metrics.vertical.left == 4983);
    CHECK(pixels[210 * 5000 + 4997] != 0xffffff);
    CHECK(graphics->document_hit(request.key, 4990, 230) == ROMAW_GRAPHICS_GROW);
    graphics->release(request.key);
    request.width = 20000;
    CHECK(graphics->render(&request, pixels, stride, &metrics) == -50);
    free(pixels);

    /* StandardMDEF rows retain literal text and native shortcut captions.
       Selection is exact inversion; disabled rows never acquire a highlight. */
    request = (ROMAWGraphicsRequest){0};
    request.size = sizeof request;
    request.kind = ROMAW_GRAPHICS_MENU_ITEM;
    request.key = 102;
    request.width = 240;
    request.height = 16;
    request.title = (const unsigned char*)"\x0a"
                                          "Open;(-/!x";
    request.shortcut = (const unsigned char*)"\6Ctrl+O";
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED;
    CHECK(!graphics->render(&request, NULL, 0, &metrics));
    fprintf(stderr, "MDEF row metrics: %d x %d\n", metrics.menu_item_width, metrics.menu_item_height);
    CHECK(metrics.menu_item_height >= 12 && metrics.menu_item_height <= 20 && metrics.menu_item_width > 80);
    stride = request.width * 4 + 16;
    extent = stride * request.height;
    pixels = malloc(extent + 16);
    uint32_t* normal = malloc(extent);
    CHECK(pixels && normal);
    memset(pixels, 0xa5, extent + 16);
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    memcpy(normal, pixels, extent);
    CHECK(write_bitmap("quickdraw-popup-row.bmp", pixels, 240, 16, stride));
    request.flags |= ROMAW_GRAPHICS_ITEM_SELECTED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    for (unsigned y = 0; y < 16; ++y) {
        for (unsigned x = 0; x < 240; ++x)
            CHECK(*(uint32_t*)((char*)pixels + y * stride + x * 4) ==
                  (*(uint32_t*)((char*)normal + y * stride + x * 4) ^ 0xffffff));
        for (unsigned x = 240 * 4; x < stride; ++x)
            CHECK(((unsigned char*)pixels)[y * stride + x] == 0xa5);
    }
    for (unsigned i = 0; i < 16; ++i)
        CHECK(((unsigned char*)pixels)[extent + i] == 0xa5);
    request.flags = ROMAW_GRAPHICS_ITEM_SELECTED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(pixels[0] == 0xffffff);
    unsigned gray_pixels = 0;
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 240; ++x) {
            uint32_t color = *(uint32_t*)((char*)pixels + y * stride + x * 4);
            gray_pixels += color != 0 && color != 0xffffff;
        }
    CHECK(gray_pixels > 10);
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED | ROMAW_GRAPHICS_ITEM_CHECKED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    unsigned check_pixels = 0, shortcut_pixels = 0;
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 240; ++x) {
            uint32_t color = *(uint32_t*)((char*)pixels + y * stride + x * 4);
            check_pixels += x < 10 && color != *(uint32_t*)((char*)normal + y * stride + x * 4);
            shortcut_pixels += x > 180 && color != 0xffffff;
        }
    CHECK(check_pixels && shortcut_pixels);
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED | ROMAW_GRAPHICS_ITEM_SUBMENU;
    request.shortcut = NULL;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    unsigned arrow_pixels = 0;
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 225; x < 240; ++x)
            arrow_pixels += *(uint32_t*)((char*)pixels + y * stride + x * 4) != 0xffffff;
    CHECK(arrow_pixels);
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED | ROMAW_GRAPHICS_ITEM_SEPARATOR;
    request.height = 8;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(metrics.menu_item_height == 8 && pixels[0] == 0xffffff);
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED;
    request.icon_width = 20;
    CHECK(!graphics->render(&request, NULL, 0, &metrics));
    CHECK(metrics.menu_item_height == 20);
    request.icon_width = 0;
    request.title = (const unsigned char*)"\2-X";
    CHECK(!graphics->render(&request, NULL, 0, &metrics));
    CHECK(metrics.menu_item_height > 8);
    request.icon_width = 1000;
    CHECK(graphics->render(&request, NULL, 0, &metrics) == -50);
    graphics->release(request.key);
    request.kind = ROMAW_GRAPHICS_CHECKBOX;
    request.width = 16;
    request.height = 12;
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    for (unsigned y = 0; y < 12; ++y)
        for (unsigned x = 0; x < 16; ++x) {
            uint32_t expected = (x == 2 || x == 13 || y == 0 || y == 11) && x >= 2 && x <= 13 ? 0 : 0xffffff;
            CHECK(*(uint32_t*)((char*)pixels + y * stride + x * 4) == expected);
        }
    request.flags |= ROMAW_GRAPHICS_ITEM_CHECKED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 5 * stride + 7 * 4) == 0);
    request.flags |= ROMAW_GRAPHICS_ITEM_SELECTED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 1 * stride + 6 * 4) == 0);
    request.flags = ROMAW_GRAPHICS_ITEM_CHECKED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 5 * stride + 7 * 4) != 0);
    graphics->release(request.key);
    pixels = realloc(pixels, stride * 32 + 16);
    CHECK(pixels);
    // Font Manager metrics and pixels are shared by labels, fields and editing.
    request.kind = ROMAW_GRAPHICS_TEXT;
    request.width = 120;
    request.height = 24;
    request.text = (const unsigned char*)"Finder File";
    request.text_length = 11;
    request.font = 3;
    request.font_size = 9;
    request.foreground = 0;
    request.background = 0xffffff;
    request.flags = 0;
    request.text_flags = 0;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(metrics.text_width > 20 && metrics.text_width < 120 && metrics.text_height > 0 && metrics.text_ascent > 0);
    int one_line = metrics.text_height;
    request.width = 25;
    request.text_flags = ROMAW_TEXT_WRAP;
    CHECK(!graphics->render(&request, NULL, 0, &metrics));
    CHECK(metrics.text_height > one_line);
    request.text_length = 32768;
    CHECK(graphics->render(&request, NULL, 0, &metrics) == -50);
    request.text_length = 11;
    request.font_size = 0;
    CHECK(graphics->render(&request, NULL, 0, &metrics) == -50);
    request.font_size = 9;
    request.text_flags = 0;
    request.kind = ROMAW_GRAPHICS_BUTTON;
    request.width = 80;
    request.height = 24;
    request.title = (const unsigned char*)"";
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 12 * stride + 40 * 4) == 0xffffff);
    request.flags |= ROMAW_GRAPHICS_ITEM_SELECTED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 12 * stride + 40 * 4) == 0);
    request.kind = ROMAW_GRAPHICS_RADIO;
    request.width = 16;
    request.height = 12;
    request.flags = ROMAW_GRAPHICS_ITEM_ENABLED | ROMAW_GRAPHICS_ITEM_CHECKED;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 6 * stride + 8 * 4) == 0);
    request.kind = ROMAW_GRAPHICS_FIELD;
    request.width = 80;
    request.height = 12;
    request.background = 0xffffff;
    request.flags = 0;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(pixels[0] == 0 && *(uint32_t*)((char*)pixels + 6 * stride + 40 * 4) == 0xffffff);
    request.kind = ROMAW_GRAPHICS_PROGRESS;
    request.horizontal.minimum = 0;
    request.horizontal.maximum = 100;
    request.horizontal.value = 50;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 6 * stride + 10 * 4) == 0x737373);
    CHECK(*(uint32_t*)((char*)pixels + 6 * stride + 60 * 4) == 0xffffff);
    graphics->release(request.key);
    /* The same module covers drawing, artwork alpha, validation and wide
       canvases, rather than adding a separate executable for each primitive. */
    ROMAWGraphicsDrawing drawing = {0};
    request.kind = ROMAW_GRAPHICS_DRAWING;
    request.width = 80;
    request.height = 12;
    request.foreground = 0x123456;
    request.drawing = &drawing;
    drawing.operation = ROMAW_DRAW_FILL;
    drawing.bounds = (ROMAWGraphicsRect){2, 3, 10, 8};
    memset(pixels, 0xa5, stride * 12 + 16);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 80; ++x)
            *(uint32_t*)((char*)pixels + y * stride + x * 4) = 0xabcdef;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 80; ++x)
            CHECK(*(uint32_t*)((char*)pixels + y * stride + x * 4) ==
                  (x >= 2 && x < 10 && y >= 3 && y < 8 ? 0x123456 : 0xabcdef));
        for (size_t x = 80 * 4; x < stride; ++x)
            CHECK(((unsigned char*)pixels)[y * stride + x] == 0xa5);
    }
    for (unsigned x = 0; x < 16; ++x)
        CHECK(((unsigned char*)pixels)[12 * stride + x] == 0xa5);
    drawing.operation = ROMAW_DRAW_LINE;
    drawing.bounds = (ROMAWGraphicsRect){1, 1, 1, 1};
    drawing.end_x = 15;
    drawing.end_y = 1;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + stride + 15 * 4) == 0x123456);
    uint32_t art[80 * 12];
    for (unsigned i = 0; i < 80 * 12; ++i)
        art[i] = i % 3 == 0 ? 0x00fedcba : i % 3 == 1 ? 0xffcc2211 : 0x80402010;
    drawing.operation = ROMAW_DRAW_IMAGE;
    drawing.image = art;
    drawing.image_stride = 80 * 4;
    drawing.image_bytes = sizeof art;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 80; ++x)
            *(uint32_t*)((char*)pixels + y * stride + x * 4) = 0x204060;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 80; ++x) {
            unsigned i = y * 80 + x;
            uint32_t expected = i % 3 == 0 ? 0x204060 : i % 3 == 1 ? 0xcc2211 : 0x504040;
            CHECK(*(uint32_t*)((char*)pixels + y * stride + x * 4) == expected);
        }
    drawing.image_bytes--;
    CHECK(graphics->render(&request, pixels, stride, &metrics) == -50);
    drawing.image_bytes++;
    drawing.image_stride = SIZE_MAX - 3;
    CHECK(graphics->render(&request, pixels, stride, &metrics) == -50);
    drawing.operation = ROMAW_DRAW_POLYGON;
    drawing.points = NULL;
    drawing.point_count = 3;
    CHECK(graphics->render(&request, pixels, stride, &metrics) == -50);
    ROMAWGraphicsPoint triangle[] = {{2, 2}, {18, 2}, {2, 10}};
    drawing.points = triangle;
    drawing.filled = 1;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 4 * stride + 4 * 4) == 0x123456);
    drawing.operation = ROMAW_DRAW_ROUND_RECT;
    drawing.bounds = (ROMAWGraphicsRect){20, 1, 40, 11};
    drawing.oval_width = drawing.oval_height = 6;
    CHECK(!graphics->render(&request, pixels, stride, &metrics));
    CHECK(*(uint32_t*)((char*)pixels + 6 * stride + 30 * 4) == 0x123456);
    drawing.operation = ROMAW_DRAW_FILL;
    drawing.bounds.right = 40000;
    CHECK(graphics->render(&request, pixels, stride, &metrics) == -50);
    request.drawing = NULL;
    CHECK(graphics->render(&request, pixels, stride, &metrics) == -50);
    graphics->release(request.key);
    /* Artwork transfers must also cross the PixMap rowBytes band boundary. */
    uint32_t* wide_image = malloc(5000 * 4);
    uint32_t* wide_pixels = malloc(5000 * 4 + 16);
    CHECK(wide_image && wide_pixels);
    for (int x = 0; x < 5000; ++x) {
        wide_image[x] = 0xff000000 | (unsigned)x;
        wide_pixels[x] = 0xabcdef;
    }
    memset(wide_pixels + 5000, 0xa5, 16);
    drawing = (ROMAWGraphicsDrawing){0};
    drawing.operation = ROMAW_DRAW_IMAGE;
    drawing.image = wide_image;
    drawing.image_stride = drawing.image_bytes = 5000 * 4;
    request.width = 5000;
    request.height = 1;
    request.drawing = &drawing;
    CHECK(!graphics->render(&request, wide_pixels, 5000 * 4 + 16, &metrics));
    for (int x = 0; x < 5000; ++x)
        CHECK(wide_pixels[x] == (unsigned)x);
    for (unsigned x = 0; x < 16; ++x)
        CHECK(((unsigned char*)(wide_pixels + 5000))[x] == 0xa5);
    graphics->release(request.key);
    free(wide_pixels);
    free(wide_image);
    free(normal);
    free(pixels);
    fprintf(stderr,
            "PASS: QuickDraw managers, text, shapes, image alpha, wide bands, buffer guards and invalid requests\n");
    fprintf(stderr, "QUICKDRAW_HOST_PROBE_OK\n");
    return 0;
}
