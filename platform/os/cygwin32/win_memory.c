/*+===================================================================
  File:      win_memory.c

  Summary:   Implements win memory support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_win_accessible_span, ROMlib_win_ppc_error, physical_memory

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1997 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"
#include "rsys/drive_flags.h"
#include "rsys/error.h"
#include "Gestalt.h"
#include "rsys/gestalt.h"

#include <windows.h>

PUBLIC ULONGINT physical_memory(void)
{
    MEMORYSTATUS status;
    ULONGINT retval;

    GlobalMemoryStatus(&status);
    retval = status.dwTotalPhys;
    replace_physgestalt_selector(gestaltPhysicalRAMSize, retval);
    return retval;
}

int ROMlib_win_accessible_span(const void* pointer, uint32 length, int write_p)
{
    uintptr_t current = (uintptr_t)pointer, end = current + length;
    if (end < current)
        return 0;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info;
        DWORD protect;
        uintptr_t limit;
        if (!VirtualQuery((void*)current, &info, sizeof info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return 0;
        protect = info.Protect & 0xff;
        if (write_p && protect != PAGE_READWRITE && protect != PAGE_WRITECOPY && protect != PAGE_EXECUTE_READWRITE &&
            protect != PAGE_EXECUTE_WRITECOPY)
            return 0;
        if (!write_p && protect == PAGE_EXECUTE)
            return 0;
        limit = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (limit <= current)
            return 0;
        current = limit;
    }
    return 1;
}

/* Keep Windows UI declarations separate from Macintosh Toolbox typedefs. */
void ROMlib_win_ppc_error(const char* message)
{
    MessageBoxA(NULL, message, "ROMAW - PowerPC application", MB_OK | MB_ICONERROR);
}
