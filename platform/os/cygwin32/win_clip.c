/*+===================================================================
  File:      win_clip.c

  Summary:   Implements win clip support for host-platform configuration and integration.

  Classes:   None.

  Functions: GetScrapX, PutScrapX, ROMlib_romaw_format, cygwin_sdlwindow, discard_cached_clipboard_picture,
             render_all_clipboard_formats, we_lost_clipboard, write_pict_as_dib_to_clipboard,
             write_pict_as_pict_to_clipboard, write_surfp_to_clipboard

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1998 - 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <rsys/scrap.h>
#include <rsys/error.h>

#include "win_win.h"
#include "SDL/SDL.h"
#include "SDL_bmp.h"

extern HWND Win_Window;

#define FORMAT_PREFIX "ROMAW_0x"

PUBLIC unsigned long ROMlib_romaw_format(LONGINT type)
{
    char str[sizeof FORMAT_PREFIX + 8];
    UINT retval;

    static struct {
        LONGINT type;
        UINT value;
        boolean_t valid;
    } cache;

    if (cache.valid && type == cache.type)
        retval = cache.value;
    else {
        snprintf(str, sizeof str, "%s%08lx", FORMAT_PREFIX, (unsigned long)(uint32)type);
        retval = RegisterClipboardFormat(str);
        cache.type = type;
        cache.value = retval;
        cache.valid = retval != 0;
    }
    return retval;
}

PUBLIC HWND cygwin_sdlwindow(void)
{
    return Win_Window;
}

/* Clipboard listeners can briefly open the clipboard between two formats of
   the same Mac copy. Retry contention without clearing or republishing data. */
static BOOL open_clipboard_for_transfer(HWND window)
{
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        SetLastError(ERROR_SUCCESS);
        if (OpenClipboard(window))
            return TRUE;
        DWORD error = GetLastError();
        if (error != ERROR_ACCESS_DENIED && error != ERROR_BUSY)
            return FALSE;
        if (attempt != 9)
            Sleep(10);
    }
    return FALSE;
}

PUBLIC LONGINT GetScrapX(LONGINT type, char** h)
{
    UINT format;
    LONGINT retval;

    retval = -1;
    if (!h)
        return retval;
    switch (type) {
    case T('T', 'E', 'X', 'T'):
        format = CF_TEXT;
        break;
    default:
        format = ROMlib_romaw_format(type);
        if (type == T('P', 'I', 'C', 'T')) {
            typeof(format) newval;
            UINT formats[2] = {format, CF_DIB};

            newval = GetPriorityClipboardFormat(formats, NELEM(formats));
            if (newval != 0 && newval != (UINT)-1)
                format = newval;
        }
        break;
    }
    if (format && IsClipboardFormatAvailable(format) && open_clipboard_for_transfer(cygwin_sdlwindow())) {
        HANDLE data = GetClipboardData(format);
        if (data) {
            void* bytes = GlobalLock(data);
            if (bytes) {
                SIZE_T size = GlobalSize(data);
                if (type == T('T', 'E', 'X', 'T')) {
                    const char* end;
                    size_t limit = size < (SIZE_T)INT_MAX ? size : (SIZE_T)INT_MAX;
                    end = memchr(bytes, 0, limit);
                    if (end)
                        retval = get_scrap_helper(h, bytes, end - (const char*)bytes, TRUE);
                }
#if defined(SDL)
                else if (format == CF_DIB) {
                    if (size)
                        retval = get_scrap_helper_dib(h, bytes, size);
                }
#endif
                else if (size >= sizeof(int32)) {
                    int32 length;
                    memcpy(&length, bytes, sizeof length);
                    if (length >= 0 && (SIZE_T)length <= size - sizeof length)
                        retval = get_scrap_helper(h, (char*)bytes + sizeof length, length, FALSE);
                }
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    return retval;
}

static size_t calc_length_and_format(UINT* formatp, LONGINT type, LONGINT length, const char* p)
{
    if (type == T('T', 'E', 'X', 'T')) {
        *formatp = CF_TEXT;
        return (size_t)length + count_char(p, length, '\r') + 1;
    }
    *formatp = ROMlib_romaw_format(type);
    return (size_t)length + sizeof(int32);
}

PRIVATE void fill_in_data(char* destp, LONGINT type, LONGINT length, const char* p)
{
    switch (type) {
    case T('T', 'E', 'X', 'T'):
        while (--length >= 0) {
            char c;

            c = *p++;
            *destp++ = c;
            if (c == '\r')
                *destp++ = '\n';
        }
        *destp++ = 0;
        break;
    default:
        *(int32*)destp = length;
        if (length)
            memcpy(destp + sizeof(int32), p, length);
        break;
    }
}

PRIVATE HANDLE clip_data = NULL; /* to hold a PICT that may need conversion
				    to a CF_DIB */

PUBLIC void PutScrapX(LONGINT type, LONGINT length, char* p, int scrap_count)
{
    static int old_count = -1;
    UINT format;
    size_t new_length;
    HGLOBAL data;
    HLOCAL pict_copy = NULL;
    char* dest;

    if (length < 0 || (length && !p) || (type == T('T', 'E', 'X', 'T') && length == INT_MAX) ||
        (type != T('T', 'E', 'X', 'T') && length > INT_MAX - sizeof(int32)))
        return;
    new_length = calc_length_and_format(&format, type, length, p);
    if (!format || new_length > INT_MAX)
        return;
    /* Prepare the transfer before clearing the previous clipboard contents. */
    data = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, new_length);
    if (!data)
        return;
    dest = GlobalLock(data);
    if (!dest) {
        GlobalFree(data);
        return;
    }
    fill_in_data(dest, type, length, p);
    GlobalUnlock(data);
    if (type == TICK("PICT")) {
        pict_copy = LocalAlloc(LMEM_FIXED, new_length);
        if (pict_copy)
            fill_in_data((char*)pict_copy, type, length, p);
    }
    if (open_clipboard_for_transfer(cygwin_sdlwindow())) {
        if (scrap_count != old_count || GetClipboardOwner() != cygwin_sdlwindow()) {
            if (!EmptyClipboard())
                goto close_clipboard;
            if (clip_data)
                LocalFree(clip_data);
            clip_data = NULL;
        }
        if (SetClipboardData(format, data)) {
            /* The system owns data only after successful publication. */
            data = NULL;
            old_count = scrap_count;
            if (type == TICK("PICT")) {
                if (clip_data)
                    LocalFree(clip_data);
                clip_data = pict_copy;
                pict_copy = NULL;
                if (clip_data)
                    SetClipboardData(CF_DIB, NULL);
            }
        }
    close_clipboard:
        CloseClipboard();
    }
    if (data)
        GlobalFree(data);
    if (pict_copy)
        LocalFree(pict_copy);
}

PUBLIC boolean_t we_lost_clipboard(void)
{
    boolean_t retval;

    retval = GetClipboardOwner() != cygwin_sdlwindow();
    return retval;
}

PUBLIC void write_pict_as_dib_to_clipboard(void)
{
#if defined(SDL)
    if (clip_data)
        put_scrap_helper_dib((LPVOID)clip_data);
#endif
}

/* Rendering callbacks supply the clipboard context. Transfer ownership only
 * after SetClipboardData succeeds; otherwise the block remains ours to free. */
static void publish_clipboard_copy(UINT format, const void* bytes, size_t length)
{
    HGLOBAL data;
    void* dest;

    if (!format || !bytes || !length)
        return;
    data = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, length);
    if (!data)
        return;
    dest = GlobalLock(data);
    if (!dest) {
        GlobalFree(data);
        return;
    }
    memcpy(dest, bytes, length);
    GlobalUnlock(data);
    if (!SetClipboardData(format, data))
        GlobalFree(data);
}

PUBLIC void write_pict_as_pict_to_clipboard(void)
{
    if (clip_data) {
        SIZE_T capacity = LocalSize(clip_data);
        int32 length;
        if (capacity < sizeof length)
            return;
        memcpy(&length, clip_data, sizeof length);
        if (length >= 0 && (SIZE_T)length <= capacity - sizeof length)
            publish_clipboard_copy(ROMlib_romaw_format(T('P', 'I', 'C', 'T')), clip_data,
                                   sizeof length + (size_t)length);
    }
}

#if defined(SDL)
void write_surfp_to_clipboard(SDL_Surface* surfp)
{
    char* bytes = NULL;
    size_t length = 0;

    if (!surfp)
        return;
    if (SDL_SaveCF_DIB(surfp, &bytes, &length) == 0)
        publish_clipboard_copy(CF_DIB, bytes, length);
    free(bytes);
}
#endif

PUBLIC void render_all_clipboard_formats(void)
{
    HWND window = cygwin_sdlwindow();
    if (!window)
        return;
    if (open_clipboard_for_transfer(window)) {
        if (GetClipboardOwner() == window) {
            write_pict_as_pict_to_clipboard();
            write_pict_as_dib_to_clipboard();
        }
        CloseClipboard();
    }
}

PUBLIC void discard_cached_clipboard_picture(void)
{
    if (clip_data)
        LocalFree(clip_data);
    clip_data = NULL;
}
