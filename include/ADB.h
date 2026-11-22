/*+===================================================================
  File:      ADB.h

  Summary:   Declares adb support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_ADB_H_)
#define _ADB_H_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: ADB.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    SignedByte devType;
    SignedByte origADBAddr;
    PACKED_MEMBER(Ptr, dbServiceRtPtr);
    PACKED_MEMBER(Ptr, dbDataAreaAddr);
} ADBDataBlock;

typedef struct PACKED {
    PACKED_MEMBER(Ptr, siServiceRtPtr);
    PACKED_MEMBER(Ptr, siDataAreaAddr);
} ADBSetInfoBlock;

extern void ADBReInit(void);
extern OSErr ADBOp(Ptr data, ProcPtr procp, Ptr buffer, INTEGER command);
extern INTEGER CountADBs(void);
extern OSErr GetIndADB(ADBDataBlock* adbp, INTEGER index);
extern OSErr GetADBInfo(ADBDataBlock* adbp, INTEGER address);
extern OSErr SetADBInfo(ADBSetInfoBlock* adbp, INTEGER address);

#endif /* !_ADB_H_ */
