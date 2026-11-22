/*+===================================================================
  File:      winfs.c

  Summary:   Implements winfs support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_lockunlockrange, ROMlib_win_volume_bytes, fsync, sync

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/*
 * Copyright 1997-2000 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

/* NOTE: we have to include common.h since it sets up some macros that
   we need, including PUBLIC and PRIVATE.  However, since this is file's
   purpose is to export routines that are definitely not Mac-routines, we
   included common.h first and then include windows.h later so that we have
   access to all the Windows data types and structures. */

#define _STUBIFY_H_         /* prevent conflicts between stubify.h and windows.h */
#define BOOLEAN MAC_BOOLEAN /* we want the Windows version of these two */
#define CHAR MAC_CHAR
#include "rsys/common.h"
#undef BOOLEAN
#undef CHAR
#undef Time

#include <windows.h>
#include <stdio.h>
#include <errno.h>

#include "winfs.h"
#include "rsys/lockunlock.h"

#warning impotent fsync

PUBLIC int ROMlib_win_volume_bytes(const char* path, uint64* total, uint64* free_bytes)
{
    ULARGE_INTEGER available, size, unused;
    if (!GetDiskFreeSpaceExA(path, &available, &size, &unused))
        return 0;
    *total = size.QuadPart;
    *free_bytes = available.QuadPart;
    return 1;
}

PUBLIC int fsync(int fd)
{
    int retval;

    retval = 0;
    return retval;
}

#warning impotent sync

PUBLIC int sync(void)
{
    int retval;

    retval = 0;
    return retval;
}

PUBLIC int ROMlib_lockunlockrange(int fd, uint32 begin, uint32 count, lockunlock_t op)
{
    int retval;
    BOOL WINAPI (*routine)(HANDLE, DWORD, DWORD, DWORD, DWORD);

    warning_trace_info("fd = %d, begin = %d, count = %d, op = %d", fd, begin, count, op);
    switch (op) {
    case lock:
        routine = LockFile;
        break;
    case unlock:
        routine = UnlockFile;
        break;
    default:
        warning_unexpected("op = %d", op);
        routine = 0;
        break;
    }

    if (!routine)
        retval = paramErr;
    else {
        BOOL success;
        HANDLE h;

        h = (HANDLE)_get_osfhandle(fd);
        success = routine(h, begin, 0, count, 0);
        if (success)
            retval = noErr;
        else {
            DWORD err;

            err = GetLastError();
            switch (err) {
            case ERROR_LOCK_VIOLATION:
                retval = fLckdErr;
                break;
            case ERROR_NOT_LOCKED:
                retval = afpRangeNotLocked;
                break;
            case ERROR_LOCK_FAILED:
                retval = afpRangeOverlap;
                break;
            default:
                warning_unexpected("err = %ld, h = %p", err, h);
                retval = noErr;
                break;
            }
        }
    }
    return retval;
}
