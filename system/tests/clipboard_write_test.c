/*+===================================================================
  File:      clipboard_write_test.c

  Summary:   Implements clipboard write test support for regression tests and test fixtures.

  Classes:   None.

  Functions: count_char, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <assert.h>
static int failure, opened, opens, closes, empties, sets, dibs, globals, locals;
static HGLOBAL published;
static size_t allocated;
HWND Win_Window;
static int lost_owner;
static unsigned busy_opens;

static HWND WINAPI mock_owner(void)
{
    return lost_owner ? (HWND)1 : Win_Window;
}

int count_char(const char* p, int length, char c)
{
    int count = 0;
    while (length-- > 0)
        if (*p++ == c)
            ++count;
    return count;
}

static BOOL WINAPI mock_open(HWND window)
{
    ++opens;
    if (busy_opens) {
        --busy_opens;
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (failure == 1)
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

static BOOL WINAPI mock_empty(void)
{
    assert(opened);
    ++empties;
    return failure != 2;
}

static HGLOBAL WINAPI mock_alloc(UINT flags, SIZE_T size)
{
    if (failure == 3)
        return NULL;
    allocated = size;
    ++globals;
    return malloc(size);
}

static LPVOID WINAPI mock_lock(HGLOBAL p)
{
    return failure == 4 ? NULL : p;
}

static BOOL WINAPI mock_unlock(HGLOBAL p)
{
    return TRUE;
}

static HGLOBAL WINAPI mock_free(HGLOBAL p)
{
    assert(p && globals > 0);
    --globals;
    free(p);
    return NULL;
}

static HLOCAL WINAPI mock_local_alloc(UINT flags, SIZE_T size)
{
    if (failure == 6)
        return NULL;
    ++locals;
    return malloc(size);
}

static HLOCAL WINAPI mock_local_free(HLOCAL p)
{
    assert(p && locals > 0);
    --locals;
    free(p);
    return NULL;
}

static HANDLE WINAPI mock_publish(UINT format, HANDLE data)
{
    assert(opened);
    if (!data) {
        assert(format == CF_DIB);
        ++dibs;
        return NULL;
    }
    ++sets;
    if (failure == 5)
        return NULL;
    assert(!published);
    published = data;
    return data;
}

static UINT WINAPI mock_format(LPCSTR name)
{
    return 1234;
}

#define GetClipboardOwner mock_owner
#define OpenClipboard mock_open
#define CloseClipboard mock_close
#define EmptyClipboard mock_empty
#define GlobalAlloc mock_alloc
#define GlobalLock mock_lock
#define GlobalUnlock mock_unlock
#define GlobalFree mock_free
#define LocalAlloc mock_local_alloc
#define LocalFree mock_local_free
#define SetClipboardData mock_publish
#define RegisterClipboardFormatA mock_format
#include "../../platform/os/cygwin32/win_clip.c"
#undef main

static void reset(void)
{
    if (published) {
        mock_free(published);
        published = NULL;
    }
    assert(!globals && !opened);
    failure = opens = closes = empties = sets = dibs = lost_owner = 0;
    busy_opens = 0;
}

int main(void)
{
    int i;
    for (i = 1; i <= 5; ++i) {
        failure = i;
        PutScrapX(T('T', 'E', 'X', 'T'), 3, "a\rb", i);
        assert(!opened && !globals && !locals && !published);
        assert(closes == (i == 2 || i == 5));
        if (i == 3 || i == 4)
            assert(!opens);
        reset();
    }
    PutScrapX(T('T', 'E', 'X', 'T'), 3, "a\rb", 100);
    assert(published && globals == 1 && allocated == 5 && closes == 1);
    assert(!memcmp(published, "a\r\nb\0", 5));
    reset();
    lost_owner = 1;
    PutScrapX(T('T', 'E', 'X', 'T'), 3, "abc", 100);
    assert(published && empties == 1 && closes == 1);
    reset();
    busy_opens = 2;
    PutScrapX(T('T', 'E', 'X', 'T'), 3, "abc", 100);
    assert(published && opens == 3 && closes == 1 && !opened);
    reset();
    busy_opens = 20;
    PutScrapX(T('T', 'E', 'X', 'T'), 3, "abc", 100);
    assert(!published && opens == 10 && !closes && !globals && !opened);
    reset();
    /* A second format in the same scrap does not clear the first. */
    PutScrapX(T('P', 'I', 'C', 'T'), 3, "abc", 100);
    assert(published && !empties && closes == 1 && locals == 1 && dibs == 1);
    assert(*(int32*)published == 3 && !memcmp((char*)published + 4, "abc", 3));
    reset();
    {
        HANDLE previous = clip_data;
        failure = 1;
        PutScrapX(T('P', 'I', 'C', 'T'), 3, "new", 101);
        assert(!globals && locals == 1 && clip_data == previous && !closes);
        reset();
        failure = 5;
        PutScrapX(T('P', 'I', 'C', 'T'), 3, "new", 100);
        assert(!globals && locals == 1 && clip_data == previous && closes == 1);
        reset();
    }
    failure = 6;
    PutScrapX(T('P', 'I', 'C', 'T'), 3, "def", 101);
    assert(published && closes == 1 && !locals && !dibs && !clip_data);
    reset();
    PutScrapX(T('T', 'E', 'X', 'T'), 0, NULL, 102);
    assert(published && allocated == 1 && !*(char*)published);
    reset();
    PutScrapX(T('T', 'E', 'X', 'T'), -1, "x", 103);
    PutScrapX(T('T', 'E', 'X', 'T'), 1, NULL, 103);
    PutScrapX(T('T', 'E', 'X', 'T'), INT_MAX, "x", 103);
    PutScrapX(T('P', 'I', 'C', 'T'), INT_MAX, "x", 103);
    assert(!opens && !globals && !locals);
    puts(
        "PASS: clipboard write allocation/lock/open/clear/publish failures, ownership and text/PICT payloads; real clipboard untouched");
    return 0;
}
