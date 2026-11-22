/*+===================================================================
  File:      win_temp_test.c

  Summary:   Implements win temp test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <assert.h>
static char path[4096];
static int mode, calls, fail_alloc, live;

static DWORD WINAPI mock_temp_path(DWORD capacity, LPSTR buffer)
{
    size_t length = strlen(path);
    ++calls;
    if (mode == 5)
        return GetTempPathA(capacity, buffer);
    if (mode == 1 || (mode == 2 && calls == 2))
        return 0;
    if (mode == 3 && calls == 2)
        return capacity + 1;
    if (mode == 4)
        return MAXDWORD;
    if (length >= capacity)
        return length + 1;
    memcpy(buffer, path, length + 1);
    return length;
}

static void* temp_malloc(size_t size)
{
    void* p;
    if (fail_alloc)
        return NULL;
    p = malloc(size);
    assert(p);
    ++live;
    return p;
}

static void temp_free(void* p)
{
    if (p)
        --live;
    free(p);
}

#define GetTempPathA mock_temp_path
#define malloc temp_malloc
#define free temp_free
#include "../../platform/os/cygwin32/win_temp.c"
#undef malloc
#undef free
#undef main

static void expect(const char* input, const char* expected)
{
    char* result;
    strcpy(path, input);
    calls = 0;
    result = win_temp();
    assert(result && !strcmp(result, expected));
    assert(calls == 1 && live == 1);
    temp_free(result);
    assert(!live);
}

int main(void)
{
    char* result;
    expect("C:\\Users\\Temp\\", "C:/Users/Temp");
    expect("C:\\", "C:/");
    expect("\\", "/");
    expect("\\\\?\\C:\\", "//?/C:/");
    expect("\\\\server\\share\\", "//server/share");
    fail_alloc = 1;
    assert(!win_temp() && !live);
    fail_alloc = 0;
    mode = 1;
    assert(!win_temp() && !live);
    mode = 0;
    memset(path, 'a', 3000);
    path[3000] = '\\';
    path[3001] = 0;
    calls = 0;
    result = win_temp();
    assert(result && strlen(result) == 3000 && calls == 2);
    temp_free(result);
    calls = 0;
    fail_alloc = 1;
    assert(!win_temp() && calls == 1 && !live);
    fail_alloc = 0;
    calls = 0;
    mode = 2;
    assert(!win_temp() && calls == 2 && !live);
    calls = 0;
    mode = 3;
    assert(!win_temp() && calls == 2 && !live);
    calls = 0;
    mode = 4;
    assert(!win_temp() && calls == 1 && !live);
    mode = 5;
    calls = 0;
    result = win_temp();
    assert(result && *result && !strchr(result, '\\'));
    temp_free(result);
    assert(!live);
    puts("PASS: Windows temp path failures, allocation failures, growing paths and drive roots");
    return 0;
}
