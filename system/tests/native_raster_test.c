/*+===================================================================
  File:      native_raster_test.c

  Summary:   Intercept GDI: exercise submission without sending a real print job.

  Classes:   None.

  Functions: ROMlib_get_fcw_fsw, ROMlib_set_fcw_fsw, host_dialog_error, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Intercept GDI: exercise submission without sending a real print job. */
#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <assert.h>
static int starts, pages, draws, ends, aborts, deletes, fail, errors;

static int WINAPI test_start(HDC dc, const DOCINFOW* info)
{
    assert(dc == (HDC)42 && info->lpszDocName);
    ++starts;
    return fail == 1 ? -1 : 1;
}

static int WINAPI test_page(HDC dc)
{
    ++pages;
    return fail == 2 ? -1 : 1;
}

static int WINAPI test_endpage(HDC dc)
{
    return fail == 4 ? -1 : 1;
}

static int WINAPI test_end(HDC dc)
{
    ++ends;
    return fail == 5 ? -1 : 1;
}

static int WINAPI test_abort(HDC dc)
{
    ++aborts;
    return 1;
}

static BOOL WINAPI test_delete(HDC dc)
{
    assert(dc == (HDC)42);
    ++deletes;
    return TRUE;
}

static int WINAPI test_caps(HDC dc, int cap)
{
    assert(dc == (HDC)42);
    return cap == PHYSICALOFFSETX ? 75 : cap == PHYSICALOFFSETY ? 100 : 0;
}

static int WINAPI test_dib(HDC dc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void* pixels,
                           const BITMAPINFO* bitmap, UINT usage, DWORD rop)
{
    assert(dc == (HDC)42 && x == -75 && y == -100 && w == 2550 && h == 3300);
    assert(!sx && !sy && sw == 9 && sh == 2 && usage == DIB_RGB_COLORS && rop == SRCCOPY);
    assert(bitmap->bmiHeader.biHeight == -2 && bitmap->bmiHeader.biBitCount == 1);
    assert(bitmap->bmiColors[0].rgbRed == 255 && !bitmap->bmiColors[1].rgbRed);
    const unsigned char expected[] = {0x80, 0, 0, 0, 0, 0x80, 0, 0};
    assert(!memcmp(pixels, expected, sizeof expected));
    ++draws;
    return fail == 3 ? GDI_ERROR : 2;
}

#define StartDocW test_start
#define StartPage test_page
#define EndPage test_endpage
#define EndDoc test_end
#define AbortDoc test_abort
#define StretchDIBits test_dib
#define GetDeviceCaps test_caps
#define DeleteDC test_delete
#include "../../platform/os/cygwin32/win_print.c"
#undef main

uint32 ROMlib_get_fcw_fsw(void)
{
    return 0;
}

void ROMlib_set_fcw_fsw(uint32 value)
{
}

void host_dialog_error(const char* message)
{
    ++errors;
}

static void prepare(void)
{
    native_job = calloc(1, sizeof *native_job);
    assert(native_job);
    native_job->info.hDC = (HDC)42;
    native_job->info.hres = native_job->info.vres = 300;
}

int main(void)
{
    const unsigned char pixels[] = {0x80, 0, 0xaa, 0, 0x80, 0xbb};
    assert(host_raster_begin(612, 792) < 0);
    prepare();
    assert(host_raster_begin(612, 792) == 1 && !native_job);
    assert(host_raster_page(pixels, 9, 2, 3) == 1);
    assert(host_raster_page(pixels, 9, 2, 3) == 1);
    assert(host_raster_end(0) == 1 && !raster_job && ends == 1 && !aborts && deletes == 1 && draws == 2);
    for (fail = 1; fail <= 5; ++fail) {
        prepare();
        int before = deletes;
        int result = host_raster_begin(612, 792);
        if (fail == 1)
            assert(result < 0 && !raster_job);
        else {
            assert(result == 1);
            result = host_raster_page(pixels, 9, 2, 3);
            if (fail != 5)
                assert(result < 0);
            assert(host_raster_end(0) < 0 && !raster_job);
        }
        assert(deletes == before + 1);
    }
    fail = 0;
    prepare();
    assert(host_raster_begin(612, 792) == 1);
    assert(host_raster_page(pixels, -1, 2, 3) < 0);
    assert(host_raster_end(0) < 0);
    prepare();
    assert(host_raster_begin(612, 792) == 1);
    assert(host_raster_end(1) == 1);
    assert(host_raster_end(1) == 1);
    puts(
        "Native raster print: pages, padded rows, margins, GDI failures, cancellation and DC ownership passed; no jobs submitted");
    return 0;
}
