/*+===================================================================
  File:      think.c

  Summary:   Implements think support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: CDebugStr, CtoPstr, PtoCstr

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ThinkC.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "MemoryMgr.h"

A1(PUBLIC, StringPtr, CtoPstr, char*, str)
{
    register Size len;

    len = strlen((char*)str);
    BlockMove((Ptr)str, (Ptr)(str + 1), len);
    str[0] = len;
    return (StringPtr)str;
}

A1(PUBLIC, char*, PtoCstr, StringPtr, str)
{
    register Size len;

    len = str[0];
    BlockMove((Ptr)(str + 1), (Ptr)str, len);
    str[len] = 0;
    return (char*)str;
}

P1(PUBLIC pascal trap, void, DebugStr, StringPtr, p)
{
    int i;

    fprintf(stderr, "debugstr: ");
    for (i = *p++; i-- > 0; fprintf(stderr, "%c", (LONGINT)*p++))
        ;
    fprintf(stderr, "\n");
}

PUBLIC void CDebugStr(StringPtr p)
{
    char* pstr;
    LONGINT n;

    n = strlen((char*)p);
    pstr = alloca(n + 1);
    pstr[0] = n;
    BlockMove((Ptr)p, (Ptr)pstr + 1, n);
}
