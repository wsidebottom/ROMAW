/*+===================================================================
  File:      tempmem.c

  Summary:   Implements tempmem support for Macintosh heaps, handles and temporary memory.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/memory; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "MemoryMgr.h"
#include "rsys/mman.h"

/* #define TEMP_MEM_FAIL */
#define paramErr (-50)

P0(PUBLIC pascal trap, int32, TempFreeMem)
{
#if defined(TEMP_MEM_FAIL)
    return 0;
#else
    return FreeMemSys();
#endif
}

P1(PUBLIC pascal trap, Size, TempMaxMem, Size*, grow)
{
#if defined(TEMP_MEM_FAIL)
    return 0;
#else
    Size result;
    ZONE_SAVE_EXCURSION(SysZone, { result = MaxMem(grow); });
    return result;
#endif
}

P0(PUBLIC pascal trap, Ptr, TempTopMem)
{
    warning_unimplemented(NULL_STRING);
    return NULL;
}

P2(PUBLIC pascal trap, Handle, TempNewHandle, Size, logical_size, OSErr*, result_code)
{
#if defined(TEMP_MEM_FAIL)
    *result_code = CWC(memFullErr);
    return NULL;
#else
    {
        Handle retval;

        /* Temporary memory must be outside the application's partition.  Carbon's
       allocators distinguish temporary handles from application pointers by
       their address, and use different resize/disposal paths for each. */
        ZONE_SAVE_EXCURSION(SysZone, {
            retval = NewHandle(logical_size);
            if (result_code)
                *result_code = MemErr;
        });
        return retval;
    }
#endif
}

P2(PUBLIC pascal trap, void, TempHLock, Handle, h, OSErr*, result_code)
{
    HLock(h);
    *result_code = MemErr;
}

P2(PUBLIC pascal trap, void, TempHUnlock, Handle, h, OSErr*, result_code)
{
    HUnlock(h);
    *result_code = MemErr;
}

P2(PUBLIC pascal trap, void, TempDisposeHandle, Handle, h, OSErr*, result_code)
{
    DisposHandle(h);
    *result_code = MemErr;
}
