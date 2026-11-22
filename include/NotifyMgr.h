/*+===================================================================
  File:      NotifyMgr.h

  Summary:   Declares notify mgr support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__NOTIFYMGR__)
#define __NOTIFYMGR__

/*
 * Copyright 1993 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: NotifyMgr.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    PACKED_MEMBER(QElemPtr, qLink);
    INTEGER qType;
    INTEGER nmFlags;
    LONGINT nmPrivate;
    INTEGER nmReserved;
    INTEGER nmMark;
    PACKED_MEMBER(Handle, nmIcon);
    PACKED_MEMBER(Handle, nmSound);
    PACKED_MEMBER(StringPtr, nmStr);
    PACKED_MEMBER(ProcPtr, nmResp); /* pascal void myresponse(NMRecPtr foo) */
                                    /* value of -1 means remove queue element
                           automatically */
    LONGINT nmRefCon;
} NMRec, *NMRecPtr;

extern trap OSErrRET NMInstall(NMRecPtr nmptr);
extern trap OSErrRET NMRemove(NMRecPtr nmptr);

#endif /* __NOTIFYMGR__ */
