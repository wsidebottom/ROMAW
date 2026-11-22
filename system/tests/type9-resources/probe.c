/*+===================================================================
  File:      probe.c

  Summary:   Implements probe support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests/type9-resources; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Resources.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <string.h>

static int fail(int code)
{
    SysBeep(-code);
    return code;
}

int main(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    short file = OpenResFile("\pD:Source:ROMAW:build:type9-resource-check:Fixtures");
    if (file == -1)
        return fail(1);
    short count = Count1Resources('ACT9');
    if (!count)
        return fail(2);
    for (short id = 0; id < count; ++id) {
        Handle actual = Get1Resource('ACT9', id);
        if (!actual || ResError())
            return fail(3);
        HLock(actual);
        Handle expected = Get1Resource('EXP9', id);
        if (!expected || ResError() || GetHandleSize(actual) != GetHandleSize(expected) ||
            memcmp(*actual, *expected, GetHandleSize(actual)))
            return fail(4);
        HUnlock(actual);
        ReleaseResource(actual);
        ReleaseResource(expected);
    }
    for (short id = 1; id <= 4; ++id) {
        if (Get1Resource('BAD9', id) || ResError() != -186)
            return fail(5 + id);
    }
    Handle code = Get1Resource('EXE9', 0);
    if (!code || ResError())
        return fail(10);
    /* Keep the small allocation from coalescing with subsequent free space. */
    Ptr barrier = NewPtr(2048);
    if (!barrier)
        return fail(11);
    unsigned char* address = (unsigned char*)*code;
    address[1] = 11;
    FlushCodeCache();
    if (((int (*)(void))address)() != 11)
        return fail(12);
    EmptyHandle(code);
    Handle reloaded = Get1Resource('EXE9', 0);
    if (reloaded != code || ResError() || (unsigned char*)*code != address || ((int (*)(void)) * code)() != 42)
        return fail(13);
    DisposePtr(barrier);
    CloseResFile(file);
    SysBeep(777);
    return 0;
}
