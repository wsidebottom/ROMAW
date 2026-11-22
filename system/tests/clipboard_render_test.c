/*+===================================================================
  File:      clipboard_render_test.c

  Summary:   Implements clipboard render test support for regression tests and test fixtures.

  Classes:   None.

  Functions: SDL_SaveCF_DIB, main, put_scrap_helper_dib

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <assert.h>
#include "SDL/SDL.h"
static int failure, blocks, unlocks, publications, encodes, encoded_blocks;
static UINT last_format, alloc_flags;
static size_t allocated;
static int opened, opens, closes, lost_owner, dib_renders, format_calls, fail_format;
static char format_name[64];
static HGLOBAL published;
static unsigned char picture[] = {3, 0, 0, 0, 'a', 'b', 'c'};
HWND Win_Window = (HWND)1234;
static int discarded;

static HLOCAL WINAPI mock_local_free(HLOCAL p)
{
    assert(p == picture);
    ++discarded;
    return NULL;
}

static HGLOBAL WINAPI mock_alloc(UINT flags, SIZE_T size)
{
    alloc_flags = flags;
    allocated = size;
    if (failure == 1)
        return NULL;
    ++blocks;
    return malloc(size);
}

static LPVOID WINAPI mock_lock(HGLOBAL p)
{
    return failure == 2 ? NULL : p;
}

static BOOL WINAPI mock_unlock(HGLOBAL p)
{
    ++unlocks;
    return TRUE;
}

static HGLOBAL WINAPI mock_free(HGLOBAL p)
{
    assert(p && blocks > 0);
    --blocks;
    free(p);
    return NULL;
}

static SIZE_T WINAPI mock_local_size(HLOCAL p)
{
    assert(p == picture);
    return failure == 5 ? 0 : failure == 7 ? sizeof picture + 8 : sizeof picture;
}

static HANDLE WINAPI mock_publish(UINT format, HANDLE data)
{
    ++publications;
    last_format = format;
    assert(data && !memcmp(data, picture, sizeof picture));
    if (failure == 3)
        return NULL;
    assert(!published);
    published = data;
    return data;
}

static UINT WINAPI mock_format(LPCSTR name)
{
    ++format_calls;
    strcpy(format_name, name);
    return fail_format ? 0 : 1234;
}

static BOOL WINAPI mock_open(HWND window)
{
    ++opens;
    if (failure == 8)
        return FALSE;
    assert(!opened);
    opened = 1;
    return TRUE;
}

static BOOL WINAPI mock_close(void)
{
    assert(opened);
    opened = 0;
    ++closes;
    return TRUE;
}

static HWND WINAPI mock_owner(void)
{
    return lost_owner ? (HWND)1 : Win_Window;
}

void put_scrap_helper_dib(void* bytes)
{
    assert(opened && !lost_owner);
    ++dib_renders;
}

static void encoded_free(void* p)
{
    if (p) {
        assert(encoded_blocks > 0);
        --encoded_blocks;
    }
    free(p);
}

int SDL_SaveCF_DIB(SDL_Surface* surface, char** bytes, size_t* length)
{
    ++encodes;
    if (failure == 4)
        return -1;
    if (failure == 6) {
        *bytes = NULL;
        *length = 0;
        return 0;
    }
    *bytes = malloc(sizeof picture);
    assert(*bytes);
    ++encoded_blocks;
    memcpy(*bytes, picture, sizeof picture);
    *length = sizeof picture;
    return 0;
}

#define OpenClipboard mock_open
#define CloseClipboard mock_close
#define GetClipboardOwner mock_owner
#define GlobalAlloc mock_alloc
#define GlobalLock mock_lock
#define GlobalUnlock mock_unlock
#define GlobalFree mock_free
#define LocalSize mock_local_size
#define LocalFree mock_local_free
#define SetClipboardData mock_publish
#define RegisterClipboardFormatA mock_format
#define free encoded_free
#include "../../platform/os/cygwin32/win_clip.c"
#undef free
#undef main

static void reset(void)
{
    if (published) {
        mock_free(published);
        published = NULL;
    }
    assert(!blocks && !encoded_blocks && !opened);
    failure = unlocks = publications = encodes = alloc_flags = last_format = 0;
    opens = closes = dib_renders = lost_owner = allocated = 0;
}

int main(void)
{
    SDL_Surface surface;
    int i;
    clip_data = picture;
    for (i = 1; i <= 3; ++i) {
        failure = i;
        write_pict_as_pict_to_clipboard();
        assert(!blocks && !published && clip_data == picture);
        assert(unlocks == (i == 3));
        reset();
        failure = i;
        write_surfp_to_clipboard(&surface);
        assert(!blocks && !published && encodes == 1 && !encoded_blocks);
        reset();
    }
    write_pict_as_pict_to_clipboard();
    assert(published && blocks == 1 && last_format == 1234 && unlocks == 1);
    assert(alloc_flags & GMEM_MOVEABLE);
    reset();
    write_surfp_to_clipboard(&surface);
    assert(published && blocks == 1 && last_format == CF_DIB && unlocks == 1);
    assert(alloc_flags & GMEM_MOVEABLE);
    reset();
    failure = 4;
    write_surfp_to_clipboard(&surface);
    assert(encodes == 1 && !publications && !blocks && !encoded_blocks);
    reset();
    failure = 5;
    write_pict_as_pict_to_clipboard();
    assert(!publications && !alloc_flags);
    reset();
    failure = 6;
    write_surfp_to_clipboard(&surface);
    assert(encodes == 1 && !publications && !blocks);
    reset();
    failure = 7;
    write_pict_as_pict_to_clipboard();
    assert(published && allocated == sizeof picture);
    reset();
    {
        int32 bad = -1, valid = 3;
        memcpy(picture, &bad, 4);
        write_pict_as_pict_to_clipboard();
        assert(!publications && !allocated);
        memcpy(picture, &valid, 4);
    }
    render_all_clipboard_formats();
    assert(opens == 1 && closes == 1 && dib_renders == 1 && published);
    reset();
    lost_owner = 1;
    render_all_clipboard_formats();
    assert(opens == 1 && closes == 1 && !dib_renders && !publications);
    reset();
    failure = 8;
    render_all_clipboard_formats();
    assert(opens == 1 && !closes && !dib_renders && !publications);
    reset();
    fail_format = 1;
    assert(!ROMlib_romaw_format((LONGINT)0xFFFFFFFFUL));
    fail_format = 0;
    {
        int attempts = format_calls;
        assert(ROMlib_romaw_format((LONGINT)0xFFFFFFFFUL) == 1234);
        assert(format_calls == attempts + 1 && !strcmp(format_name, "ROMAW_0xffffffff"));
        assert(ROMlib_romaw_format((LONGINT)0xFFFFFFFFUL) == 1234 && format_calls == attempts + 1);
    }
    clip_data = NULL;
    write_pict_as_pict_to_clipboard();
    write_surfp_to_clipboard(NULL);
    assert(!publications && !encodes);
    Win_Window = NULL;
    render_all_clipboard_formats();
    assert(!opens && !closes);
    clip_data = picture;
    discard_cached_clipboard_picture();
    discard_cached_clipboard_picture();
    assert(!clip_data && discarded == 1);
    puts(
        "PASS: picture/DIB rendering allocation, lock, publication and encoder failures; ownership and movable memory; real clipboard untouched");
    return 0;
}
