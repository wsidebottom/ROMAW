/*+===================================================================
  File:      TimeMgr.h

  Summary:   Declares time mgr support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__TIMEMGR__)
#define __TIMEMGR__

/*
 * Copyright 1989, 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: TimeMgr.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    PACKED_MEMBER(QElemPtr, qLink);
    INTEGER qType;
    PACKED_MEMBER(ProcPtr, tmAddr);
    LONGINT tmCount; /* I don't trust IMIV-301 */
} TMTask;

/* DO NOT DELETE THIS LINE */
#if !defined(__STDC__)
extern void InsTime();
extern void RmvTime();
extern void PrimeTime();
#else  /* __STDC__ */
extern void InsTime(QElemPtr taskp);
extern void RmvTime(QElemPtr taskp);
extern void PrimeTime(QElemPtr taskp, LONGINT count);
#endif /* __STDC__ */
#endif /* __TIMEMGR__ */
