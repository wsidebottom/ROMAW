/*+===================================================================
  File:      VDriver.h

  Summary:   Declares vdriver support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__VDRIVER__)
#define __VDRIVER__

/*
 * Copyright 1986, 1989, 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: VDriver.h 63 2004-12-24 18:19:43Z ctm $
 */

#include "FileMgr.h"

typedef struct PACKED {
    COMMONFSQUEUEDEFS;
    INTEGER ioRefNum;
    INTEGER csCode;
    PACKED_MEMBER(Ptr, csParam);
} VDParamBlock;

typedef VDParamBlock* VDParamBlockPtr;

typedef struct PACKED {
    PACKED_MEMBER(Ptr, csTable);
    INTEGER csStart;
    INTEGER csCount;
} VDEntryRecord;

typedef VDEntryRecord* VDEntRecPtr;

typedef struct PACKED {
    PACKED_MEMBER(Ptr, csGTable);
} VDGammaRecord;

typedef VDGammaRecord* VDGamRecPtr;

typedef struct PACKED {
    INTEGER csMode;
    LONGINT csData;
    INTEGER csPage;
    PACKED_MEMBER(Ptr, csBaseAddr);
} VDPgInfo;

typedef VDPgInfo* VDPgInfoPtr;

typedef struct PACKED {
    SignedByte flag;
} VDFlagRec;

typedef VDFlagRec* VDFlagPtr;

typedef struct PACKED {
    SignedByte spID;
} VDDefModeRec;

typedef VDDefModeRec* VDDefModePtr;

#endif /* !__VDRIVER__ */
