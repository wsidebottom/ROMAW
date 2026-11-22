/*+===================================================================
  File:      win_font.c

  Summary:   Scalable host glyphs for enlarged classic bitmap text. No host window is created: a monochrome DIB supplies
             a mask to the normal QuickDraw blitter.

  Classes:   None.

  Functions: ROMlib_win_font_set_directory, ROMlib_win_font_shutdown, ROMlib_win_text_mask

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Scalable host glyphs for enlarged classic bitmap text. No host window is
   created: a monochrome DIB supplies a mask to the normal QuickDraw blitter. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsys/winfont.h"

#define FONT_CACHE_SIZE 12

static struct {
    HFONT font;
    unsigned family, style;
    int size;
} font_cache[FONT_CACHE_SIZE];

static unsigned next_font;
static const char* font_directory;

void ROMlib_win_font_set_directory(const char* installation)
{
    font_directory = installation;
}

/* Private registrations live for the host process. The GDI handles in the
   cache can be discarded between guest launches without reloading the files. */
static int register_family(unsigned family)
{
    static const char* files[][5] = {
        {"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic", NULL},
        {"Helvetica", "Helvetica-Bold", NULL},
        {"Courier", "Courier-Bold", NULL},
    };
    static unsigned registered[3];
    unsigned i = registered[family];

    if (!font_directory)
        return 0;
    for (; files[family][i]; ++i) {
        char path[strlen(font_directory) + sizeof "/fonts/Times-BoldItalic.ttf"];
        sprintf(path, "%s/fonts/%s.ttf", font_directory, files[family][i]);
        if (!AddFontResourceExA(path, FR_PRIVATE, NULL))
            return 0;
        ++registered[family];
    }
    return 1;
}

void ROMlib_win_font_shutdown(void)
{
    unsigned i;
    for (i = 0; i < FONT_CACHE_SIZE; ++i)
        if (font_cache[i].font)
            DeleteObject(font_cache[i].font);
    memset(font_cache, 0, sizeof font_cache);
    next_font = 0;
}

static HFONT host_font(unsigned family, int size, unsigned style)
{
    static const WCHAR* faces[] = {L"Times", L"Helvetica", L"Courier"};
    HFONT font;
    unsigned i;
    for (i = 0; i < FONT_CACHE_SIZE; ++i)
        if (font_cache[i].font && font_cache[i].family == family && font_cache[i].size == size &&
            font_cache[i].style == style)
            return font_cache[i].font;
    if (!register_family(family))
        return NULL;
    font = CreateFontW(-size, 0, 0, 0, style & 1 ? FW_BOLD : FW_NORMAL, !!(style & 2), !!(style & 4), FALSE,
                       DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH,
                       faces[family]);
    if (!font)
        return NULL;
    i = next_font++ % FONT_CACHE_SIZE;
    if (font_cache[i].font)
        DeleteObject(font_cache[i].font);
    font_cache[i].font = font;
    font_cache[i].family = family;
    font_cache[i].size = size;
    font_cache[i].style = style;
    return font;
}

unsigned char* ROMlib_win_text_mask(const char* family, int size, unsigned style, const unsigned char* text, int count,
                                    const int* positions, int x, int baseline, int width, int height, int* pitch_out)
{
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[2];
    } info = {0};

    WCHAR wide[1024];
    int advances[1024];
    HDC dc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ old_bitmap = NULL, old_font = NULL;
    HFONT font;
    void* dib = NULL;
    unsigned char* result = NULL;
    unsigned family_id;
    int pitch, i;
    size_t bytes;

    if (!family || !text || !positions || !pitch_out || count <= 0 || count > 1024 || size <= 0 || size > 512 ||
        style & ~7U || width <= 0 || width > 8192 || height <= 0 || height > 4096 || x < -65535 || x > 65535 ||
        baseline < -65535 || baseline > 65535)
        return NULL;
    if (!strcmp(family, "Times"))
        family_id = 0;
    else if (!strcmp(family, "Helvetica"))
        family_id = 1;
    else if (!strcmp(family, "Courier"))
        family_id = 2;
    else
        return NULL;
    /* Classic bitmap fonts may assign pictures to control characters. */
    for (i = 0; i < count; ++i)
        if (text[i] < 32 || text[i] == 127)
            return NULL;
    if (MultiByteToWideChar(10000, 0, (const char*)text, count, wide, count) != count)
        return NULL;
    for (i = 0; i < count; ++i) {
        long long advance = (long long)positions[i + 1] - positions[i];
        if (advance < -32767 || advance > 32767)
            return NULL;
        advances[i] = (int)advance;
    }
    font = host_font(family_id, size, style);
    if (!font || !(dc = CreateCompatibleDC(NULL)))
        return NULL;
    pitch = (width + 31) / 32 * 4;
    bytes = (size_t)pitch * height;
    info.header.biSize = sizeof info.header;
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 1;
    info.header.biCompression = BI_RGB;
    info.header.biClrUsed = 2;
    info.colors[1].rgbRed = 255;
    info.colors[1].rgbGreen = 255;
    info.colors[1].rgbBlue = 255;
    bitmap = CreateDIBSection(dc, (BITMAPINFO*)&info, DIB_RGB_COLORS, &dib, NULL, 0);
    if (!bitmap || !dib)
        goto done;
    old_bitmap = SelectObject(dc, bitmap);
    if (!old_bitmap || old_bitmap == HGDI_ERROR)
        goto done;
    old_font = SelectObject(dc, font);
    if (!old_font || old_font == HGDI_ERROR)
        goto done;
    memset(dib, 0, bytes);
    if (SetTextColor(dc, RGB(255, 255, 255)) == CLR_INVALID || !SetBkMode(dc, TRANSPARENT) ||
        SetTextAlign(dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP) == GDI_ERROR ||
        !ExtTextOutW(dc, x, baseline, 0, NULL, wide, count, advances) || !GdiFlush())
        goto done;
    result = malloc(bytes);
    if (result) {
        memcpy(result, dib, bytes);
        /* Clear unused bits and row padding regardless of GDI implementation. */
        for (i = 0; i < height; ++i) {
            unsigned char* row = result + (size_t)i * pitch;
            int used = (width + 7) / 8;
            if (width & 7)
                row[used - 1] &= 0xff << (8 - (width & 7));
            memset(row + used, 0, pitch - used);
        }
        *pitch_out = pitch;
    }
done:
    if (old_font && old_font != HGDI_ERROR)
        SelectObject(dc, old_font);
    if (old_bitmap && old_bitmap != HGDI_ERROR)
        SelectObject(dc, old_bitmap);
    if (bitmap)
        DeleteObject(bitmap);
    DeleteDC(dc);
    return result;
}
