/*+===================================================================
  File:      toolbox_benchmark.c

  Summary:   Measure actual QuickDraw rendering inside a running ROMAW and record deterministic output checksums.

  Classes:   None.

  Functions: ROMAWGraphicsProbe

  Origin:    Executor performance fixture using the existing host graphics ABI and original test artwork/text.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "classic-ui/host_graphics.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static uint64_t checksum(const uint32_t* pixels, size_t count)
{
    uint64_t value = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < count; ++i)
        value = (value ^ pixels[i]) * UINT64_C(1099511628211);
    return value;
}

static int measure(FILE* file, const ROMAWClassicGraphics* graphics, const char* name, ROMAWGraphicsRequest* r)
{
    const int iterations = 100;
    size_t count = (size_t)r->width * r->height;
    uint32_t* pixels = calloc(count + 4, sizeof *pixels);
    ROMAWGraphicsMetrics metrics;
    LARGE_INTEGER frequency, start, end;
    if (!pixels || !QueryPerformanceFrequency(&frequency)) {
        free(pixels);
        return 1;
    }
    for (size_t i = count; i < count + 4; ++i)
        pixels[i] = 0xa5a5a5a5;
    // Warm guest resources, fonts and reusable GWorlds before timing host work.
    for (int i = 0; i < 5; ++i)
        if (graphics->render(r, pixels, r->width * 4, &metrics)) {
            free(pixels);
            return 2;
        }
    QueryPerformanceCounter(&start);
    int error = 0;
    for (int i = 0; i < iterations && !error; ++i)
        error = graphics->render(r, pixels, r->width * 4, &metrics);
    QueryPerformanceCounter(&end);
    for (size_t i = count; i < count + 4; ++i)
        if (pixels[i] != 0xa5a5a5a5)
            error = 3;
    if (!error)
        fprintf(file, "%s,%d,%d,%d,%.6f,%016" PRIx64 ",%d,%d\n", name, r->width, r->height, iterations,
                (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart / iterations, checksum(pixels, count),
                metrics.text_width, metrics.text_height);
    graphics->release(r->key);
    free(pixels);
    return error;
}

__declspec(dllexport) int32_t ROMAWGraphicsProbe(void)
{
    const char* output = getenv("ROMAW_BENCHMARK_OUTPUT");
    FARPROC symbol = GetProcAddress(GetModuleHandleW(NULL), "romaw_classic_graphics");
    const ROMAWClassicGraphics* (*factory)(void) = NULL;
    if (!output || !symbol)
        return 10;
    memcpy(&factory, &symbol, sizeof factory);
    const ROMAWClassicGraphics* graphics = factory();
    if (!graphics || graphics->version != ROMAW_GRAPHICS_ABI)
        return 11;
    FILE* file = fopen(output, "w");
    if (!file)
        return 12;
    fprintf(file, "workload,width,height,iterations,ms_per_operation,checksum,text_width,text_height\n");
    ROMAWGraphicsRequest request = {0};
    request.size = sizeof request;
    request.key = UINT64_C(0x42454e43484d4152);
    request.width = 640;
    request.height = 480;
    request.kind = ROMAW_GRAPHICS_DOCUMENT;
    request.title = (const unsigned char*)"\x09"
                                          "Benchmark";
    request.flags = ROMAW_GRAPHICS_ACTIVE | ROMAW_GRAPHICS_CLOSE | ROMAW_GRAPHICS_ZOOM | ROMAW_GRAPHICS_SCROLLBARS;
    request.status_height = 18;
    request.vertical = request.horizontal = (ROMAWGraphicsScroll){0, 100, 37, 1, -1};
    int error = measure(file, graphics, "window_chrome", &request);
    unsigned char text[2048];
    const char sentence[] = "ROMAW benchmark: packed pixels, clipping, text and classic controls. ";
    for (size_t i = 0; i < sizeof text; ++i)
        text[i] = sentence[i % (sizeof sentence - 1)];
    request.kind = ROMAW_GRAPHICS_TEXT;
    request.height = 320;
    request.text = text;
    request.text_length = sizeof text;
    request.text_flags = ROMAW_TEXT_WRAP;
    request.foreground = 0x102030;
    request.background = 0xffffff;
    request.font_size = 12;
    if (!error)
        error = measure(file, graphics, "wrapped_text", &request);
    request.kind = ROMAW_GRAPHICS_DESKTOP;
    request.height = 480;
    if (!error)
        error = measure(file, graphics, "desktop_pattern", &request);
    // Opaque source pixels make repeated copies independent of destination history.
    uint32_t* image = malloc(320 * 240 * sizeof *image);
    if (!image)
        error = 13;
    else {
        for (unsigned i = 0; i < 320 * 240; ++i)
            image[i] = 0xff000000 | ((i * 2654435761u) & 0xffffff);
        ROMAWGraphicsDrawing drawing = {0};
        drawing.operation = ROMAW_DRAW_IMAGE;
        drawing.image = image;
        drawing.image_stride = 320 * 4;
        drawing.image_bytes = 320 * 240 * 4;
        request.kind = ROMAW_GRAPHICS_DRAWING;
        request.width = 320;
        request.height = 240;
        request.drawing = &drawing;
        if (!error)
            error = measure(file, graphics, "image_copy", &request);
        free(image);
    }
    if (fclose(file) && !error)
        error = 14;
    return error;
}
