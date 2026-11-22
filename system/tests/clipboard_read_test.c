/*+===================================================================
  File:      clipboard_read_test.c

  Summary:   Implements clipboard read test support for regression tests and test fixtures.

  Classes:   None.

  Functions: get_scrap_helper, get_scrap_helper_dib, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <assert.h>
#include "rsys/scrap.h"
static unsigned char payload[128];
static SIZE_T payload_size;
static int failure, opened, opens, closes, locks, unlocks, helpers, dibs, use_dib;
static int helper_length, helper_text;
static unsigned busy_opens;
HWND Win_Window;

static BOOL WINAPI mock_available(UINT format)
{
    return failure != 1;
}

static BOOL WINAPI mock_open(HWND window)
{
    ++opens;
    if (busy_opens) {
        --busy_opens;
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    if (failure == 2)
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

static HANDLE WINAPI mock_data(UINT format)
{
    assert(opened);
    return failure == 3 ? NULL : payload;
}

static LPVOID WINAPI mock_lock(HGLOBAL p)
{
    ++locks;
    return failure == 4 ? NULL : p;
}

static SIZE_T WINAPI mock_size(HGLOBAL p)
{
    return payload_size;
}

static BOOL WINAPI mock_unlock(HGLOBAL p)
{
    ++unlocks;
    return TRUE;
}

static UINT WINAPI mock_format(LPCSTR name)
{
    return 1234;
}

static int WINAPI mock_priority(UINT* formats, int count)
{
    return use_dib ? CF_DIB : formats[0];
}

int get_scrap_helper(void* h, void* bytes, int length, boolean_t text)
{
    ++helpers;
    helper_length = length;
    helper_text = text;
    assert(bytes >= (void*)payload && (char*)bytes + length <= (char*)payload + payload_size);
    return length;
}

int get_scrap_helper_dib(void* h, void* bytes, size_t length)
{
    assert(length == payload_size);
    ++dibs;
    return 17;
}

#define IsClipboardFormatAvailable mock_available
#define OpenClipboard mock_open
#define CloseClipboard mock_close
#define GetClipboardData mock_data
#define GlobalLock mock_lock
#define GlobalSize mock_size
#define GlobalUnlock mock_unlock
#define RegisterClipboardFormatA mock_format
#define GetPriorityClipboardFormat mock_priority
#include "../../platform/os/cygwin32/win_clip.c"
#undef main

static void reset(void)
{
    assert(!opened);
    failure = opens = closes = locks = unlocks = helpers = dibs = use_dib = 0;
    busy_opens = 0;
    memset(payload, 0xA5, sizeof payload);
    payload_size = 0;
}

int main(void)
{
    char* handle = NULL;
    int i;
    int32 length;
    for (i = 1; i <= 4; ++i) {
        failure = i;
        assert(GetScrapX(T('T', 'E', 'X', 'T'), &handle) == -1);
        assert(!opened && !helpers && closes == (i >= 3));
        assert(!unlocks);
        reset();
    }
    memcpy(payload, "ab\r\n\0", 5);
    payload_size = 5;
    busy_opens = 2;
    assert(GetScrapX(T('T', 'E', 'X', 'T'), &handle) == 4);
    assert(opens == 3);
    assert(helpers == 1 && helper_length == 4 && helper_text && unlocks == 1 && closes == 1);
    reset();
    busy_opens = 20;
    assert(GetScrapX(T('T', 'E', 'X', 'T'), &handle) == -1);
    assert(opens == 10 && !closes && !locks && !helpers && !opened);
    reset();
    payload_size = 4; /* No terminator within this block. */
    assert(GetScrapX(T('T', 'E', 'X', 'T'), &handle) == -1 && !helpers);
    assert(unlocks == 1 && closes == 1);
    reset();
    assert(GetScrapX(T('T', 'E', 'X', 'T'), &handle) == -1 && !helpers);
    assert(unlocks == 1 && closes == 1);
    reset();
    for (i = 0; i < 4; ++i) {
        payload_size = i;
        assert(GetScrapX(T('P', 'I', 'C', 'T'), &handle) == -1 && !helpers);
    }
    reset();
    payload_size = 7;
    length = 3;
    memcpy(payload, &length, 4);
    memcpy(payload + 4, "abc", 3);
    assert(GetScrapX(T('P', 'I', 'C', 'T'), &handle) == 3);
    assert(helpers == 1 && helper_length == 3 && !helper_text && unlocks == 1 && closes == 1);
    reset();
    payload_size = 7;
    length = 4;
    memcpy(payload, &length, 4);
    assert(GetScrapX(T('P', 'I', 'C', 'T'), &handle) == -1 && !helpers);
    length = -1;
    memcpy(payload, &length, 4);
    assert(GetScrapX(T('P', 'I', 'C', 'T'), &handle) == -1 && !helpers);
    length = INT_MAX;
    memcpy(payload, &length, 4);
    assert(GetScrapX(T('P', 'I', 'C', 'T'), &handle) == -1 && !helpers);
    reset();
    payload_size = 4;
    length = 0;
    memcpy(payload, &length, 4);
    assert(GetScrapX(T('P', 'I', 'C', 'T'), &handle) == 0 && helpers == 1);
    reset();
    use_dib = 1;
    payload_size = 64;
    assert(GetScrapX(T('P', 'I', 'C', 'T'), &handle) == 17 && dibs == 1 && !helpers);
    assert(unlocks == 1 && closes == 1);
    reset();
    assert(GetScrapX(T('T', 'E', 'X', 'T'), NULL) == -1 && !opens);
    puts(
        "PASS: bounded clipboard text/custom reads, missing data, failed locks and balanced close/unlock; real clipboard untouched");
    return 0;
}
