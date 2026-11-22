/*+===================================================================
  File:      win_print.c

  Summary:   Implements win print support for host-platform configuration and integration.

  Classes:   None.

  Functions: get_info, host_print_dialog, host_print_reset, host_print_spool, host_print_submit, host_raster_begin,
             host_raster_end, host_raster_page

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1998 - 2004 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"

#include "SegmentLdr.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "win_print.h"
#include "win_win.h"
#include "rsys/hostdialog.h"
#include "win_print_private.h"
#include "rsys/gestalt.h"
#include "rsys/error.h"
#include "rsys/float_fcw.h"
#include "rsys/segment.h"
#include "rsys/launch.h"
#include "rsys/option.h"

/*
 * Some of the Win32 routines smash the floating point environment registers,
 * which can lead to ROMAW blowing up when it deliberately creates or
 * accesses NaNs.
 */

#define SAVE_FP_ENVIRONMENT() uint32 __save_fpe = ROMlib_get_fcw_fsw()
#define RESTORE_FP_ENVIRONMENT() ROMlib_set_fcw_fsw(__save_fpe)

PRIVATE HWND main_window(void)
{
    HWND retval;

    retval = cygwin_sdlwindow();
    return retval;
}

PRIVATE boolean_t release_info(win_printp_t wp, uint32* last_errorp)
{
    boolean_t retval;

    if (wp && wp->info.hDC)
        DeleteDC(wp->info.hDC);
    free(wp);

    retval = TRUE;
    return retval;
}

/* Keep native printer settings between Page Setup and Print. Dialogs receive
   copies so Cancel cannot change the current printer or pending job. */
static HGLOBAL native_devmode, native_devnames;
static win_printp_t native_job;

static HGLOBAL copy_global(HGLOBAL original)
{
    SIZE_T size;
    void *source, *destination;
    HGLOBAL copy;
    if (!original)
        return NULL;
    size = GlobalSize(original);
    copy = GlobalAlloc(GMEM_MOVEABLE, size);
    source = GlobalLock(original);
    destination = copy ? GlobalLock(copy) : NULL;
    if (source && destination)
        memcpy(destination, source, size);
    if (source)
        GlobalUnlock(original);
    if (destination)
        GlobalUnlock(copy);
    if (!source || !destination) {
        if (copy)
            GlobalFree(copy);
        return NULL;
    }
    return copy;
}

static void save_printer(HGLOBAL mode, HGLOBAL names)
{
    if (native_devmode)
        GlobalFree(native_devmode);
    if (native_devnames)
        GlobalFree(native_devnames);
    native_devmode = mode;
    native_devnames = names;
}

void host_print_reset(void)
{
    host_raster_end(1);
    release_info(native_job, NULL);
    native_job = NULL;
    save_printer(NULL, NULL);
}

int host_print_dialog(int page_setup, host_print_settings* settings)
{
    HGLOBAL mode = copy_global(native_devmode);
    HGLOBAL names = copy_global(native_devnames);
    PRINTDLGW pd = {0};
    PAGESETUPDLGW ps = {0};
    host_print_settings selected = *settings;
    win_printp_t job = NULL;
    int result = -1;
    SAVE_FP_ENVIRONMENT();
    if ((native_devmode && !mode) || (native_devnames && !names))
        goto done;
    if (page_setup) {
        ps.lStructSize = sizeof ps;
        ps.hwndOwner = main_window();
        ps.hDevMode = mode;
        ps.hDevNames = names;
        ps.Flags = PSD_INHUNDREDTHSOFMILLIMETERS | PSD_MARGINS;
        ps.rtMargin.left = MulDiv(settings->left, 2540, 72);
        ps.rtMargin.top = MulDiv(settings->top, 2540, 72);
        ps.rtMargin.right = MulDiv(settings->right, 2540, 72);
        ps.rtMargin.bottom = MulDiv(settings->bottom, 2540, 72);
        result = PageSetupDlgW(&ps);
        mode = ps.hDevMode;
        names = ps.hDevNames;
        if (!result) {
            result = CommDlgExtendedError() ? -1 : 0;
            goto done;
        }
        selected.width = (int)((ps.ptPaperSize.x * 72LL + 1270) / 2540);
        selected.height = (int)((ps.ptPaperSize.y * 72LL + 1270) / 2540);
        selected.left = MulDiv(ps.rtMargin.left, 72, 2540);
        selected.top = MulDiv(ps.rtMargin.top, 72, 2540);
        selected.right = MulDiv(ps.rtMargin.right, 72, 2540);
        selected.bottom = MulDiv(ps.rtMargin.bottom, 72, 2540);
    }
    else {
        pd.lStructSize = sizeof pd;
        pd.hwndOwner = main_window();
        pd.hDevMode = mode;
        pd.hDevNames = names;
        pd.Flags = PD_HIDEPRINTTOFILE | PD_NOSELECTION | PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE;
        pd.nMinPage = 1;
        pd.nMaxPage = 32767;
        pd.nFromPage = MAX(1, MIN(32767, settings->first));
        pd.nToPage = MAX(pd.nFromPage, MIN(32767, settings->last));
        pd.nCopies = MAX(1, MIN(32767, settings->copies));
        if (settings->first > 1 || settings->last < 9999)
            pd.Flags |= PD_PAGENUMS;
        result = PrintDlgW(&pd);
        mode = pd.hDevMode;
        names = pd.hDevNames;
        if (!result) {
            result = CommDlgExtendedError() ? -1 : 0;
            goto done;
        }
        result = -1;
        if (!pd.hDC || !(GetDeviceCaps(pd.hDC, RASTERCAPS) & RC_DIBTODEV))
            goto done;
        job = calloc(1, sizeof *job);
        if (!job)
            goto done;
        job->info.hDC = pd.hDC;
        pd.hDC = NULL;
        job->info.hres = GetDeviceCaps(job->info.hDC, LOGPIXELSX);
        job->info.vres = GetDeviceCaps(job->info.hDC, LOGPIXELSY);
        job->info.printx = GetDeviceCaps(job->info.hDC, HORZRES);
        job->info.printy = GetDeviceCaps(job->info.hDC, VERTRES);
        if (job->info.hres <= 0 || job->info.vres <= 0)
            goto done;
        selected.width = MulDiv(GetDeviceCaps(job->info.hDC, PHYSICALWIDTH), 72, job->info.hres);
        selected.height = MulDiv(GetDeviceCaps(job->info.hDC, PHYSICALHEIGHT), 72, job->info.vres);
        selected.first = pd.Flags & PD_PAGENUMS ? pd.nFromPage : 1;
        selected.last = pd.Flags & PD_PAGENUMS ? pd.nToPage : 9999;
        selected.copies = 1; /* The driver owns copies. */
        job->info.physx = selected.width;
        job->info.physy = selected.height;
    }
    if (selected.width < 73 || selected.height < 73 || selected.width > 32000 || selected.height > 32000 ||
        selected.first < 1 || selected.last < selected.first) {
        result = -1;
        goto done;
    }
    save_printer(mode, names);
    mode = names = NULL;
    if (job) {
        release_info(native_job, NULL);
        native_job = job;
        job = NULL;
    }
    *settings = selected;
    result = 1;
done:
    if (mode)
        GlobalFree(mode);
    if (names)
        GlobalFree(names);
    if (pd.hDC)
        DeleteDC(pd.hDC);
    release_info(job, NULL);
    RESTORE_FP_ENVIRONMENT();
    return result;
}

/* The standalone print test uses this older interface. */
PUBLIC boolean_t get_info(win_printp_t* wpp, int width, int height, orientation_t orientation, int copies,
                          uint32* error)
{
    host_print_settings settings = {width, height, 1, 9999, copies, 36, 36, 36, 36};
    int result = host_print_dialog(0, &settings);
    *wpp = result == 1 ? native_job : NULL;
    if (result == 1)
        native_job = NULL;
    if (error)
        *error = result < 0 ? GetLastError() : 0;
    return result == 1;
}

FILE* host_print_spool(char** path)
{
    char directory[MAX_PATH], filename[MAX_PATH];
    FILE* file;
    DWORD length = GetTempPathA(sizeof directory, directory);
    *path = NULL;
    if (!length || length >= sizeof directory || !GetTempFileNameA(directory, "EXE", 0, filename))
        return NULL;
    *path = strdup(filename);
    file = *path ? fopen(*path, "wb") : NULL;
    if (!file) {
        DeleteFileA(filename);
        free(*path);
        *path = NULL;
    }
    return file;
}

/* Legacy PostScript submissions are no longer a native print path. */
int host_print_submit(const char* path)
{
    return -1;
}

static win_printp_t raster_job;
static int raster_failed, raster_width, raster_height;

int host_raster_begin(int width, int height)
{
    DOCINFOW info = {0};
    if (raster_job || !native_job || width <= 0 || height <= 0 || width > 32000 || height > 32000)
        return -1;
    raster_job = native_job;
    native_job = NULL;
    info.cbSize = sizeof info;
    info.lpszDocName = L"ROMAW document";
    SAVE_FP_ENVIRONMENT();
    int result = StartDocW(raster_job->info.hDC, &info);
    RESTORE_FP_ENVIRONMENT();
    if (result <= 0) {
        release_info(raster_job, NULL);
        raster_job = NULL;
        host_dialog_error("The printer could not start the document.");
        return -1;
    }
    raster_failed = 0;
    raster_width = width;
    raster_height = height;
    return 1;
}

int host_raster_page(const unsigned char* pixels, int width, int height, int stride)
{
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[2];
    } bitmap = {0};

    if (!raster_job || raster_failed || !pixels || width <= 0 || height <= 0 || width > 32767 || height > 32767 ||
        stride < (width + 7) / 8) {
        raster_failed = 1;
        return -1;
    }
    size_t row = ((size_t)width + 31) / 32 * 4;
    if ((size_t)height > SIZE_MAX / row) {
        raster_failed = 1;
        return -1;
    }
    unsigned char* dib = calloc(height, row);
    if (!dib) {
        raster_failed = 1;
        return -1;
    }
    for (int y = 0; y < height; ++y)
        memcpy(dib + y * row, pixels + (size_t)y * stride, (width + 7) / 8);
    bitmap.header.biSize = sizeof bitmap.header;
    bitmap.header.biWidth = width;
    bitmap.header.biHeight = -height;
    bitmap.header.biPlanes = 1;
    bitmap.header.biBitCount = 1;
    bitmap.header.biCompression = BI_RGB;
    bitmap.colors[0].rgbRed = bitmap.colors[0].rgbGreen = bitmap.colors[0].rgbBlue = 255;
    HDC dc = raster_job->info.hDC;
    SAVE_FP_ENVIRONMENT();
    int result = StartPage(dc);
    if (result > 0) {
        int x = -GetDeviceCaps(dc, PHYSICALOFFSETX);
        int y = -GetDeviceCaps(dc, PHYSICALOFFSETY);
        int w = MulDiv(raster_width, raster_job->info.hres, 72);
        int h = MulDiv(raster_height, raster_job->info.vres, 72);
        result = StretchDIBits(dc, x, y, w, h, 0, 0, width, height, dib, (BITMAPINFO*)&bitmap, DIB_RGB_COLORS, SRCCOPY);
        if (result == GDI_ERROR || result == 0 || EndPage(dc) <= 0)
            result = -1;
    }
    RESTORE_FP_ENVIRONMENT();
    free(dib);
    if (result <= 0)
        raster_failed = 1;
    return result > 0 ? 1 : -1;
}

int host_raster_end(int cancel)
{
    if (!raster_job)
        return cancel ? 1 : -1;
    SAVE_FP_ENVIRONMENT();
    int result;
    if (cancel || raster_failed) {
        AbortDoc(raster_job->info.hDC);
        result = cancel ? 1 : -1;
    }
    else {
        result = EndDoc(raster_job->info.hDC) > 0 ? 1 : -1;
        if (result < 0)
            AbortDoc(raster_job->info.hDC);
    }
    RESTORE_FP_ENVIRONMENT();
    release_info(raster_job, NULL);
    raster_job = NULL;
    raster_failed = 0;
    return result;
}
