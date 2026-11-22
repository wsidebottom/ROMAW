/*+===================================================================
  File:      Finder.h

  Summary:   Declares finder support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_FINDER_H_)
#define _FINDER_H_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: Finder.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    PACKED_MEMBER(QElemPtr, qLink);
    INTEGER qType;
    INTEGER ioTrap;
    PACKED_MEMBER(Ptr, ioCmdAddr);
    PACKED_MEMBER(ProcPtr, ioCompletion);
    OSErr ioResult;
    PACKED_MEMBER(StringPtr, ioNamePtr);
    INTEGER ioVRefNum;
    INTEGER ioDTRefNum;
    INTEGER ioIndex;
    LONGINT ioTagInfo;
    PACKED_MEMBER(Ptr, ioDTBuffer);
    LONGINT ioDTReqCount;
    LONGINT ioDTActCount;
    SignedByte filler1;
    SignedByte ioIconType;
    INTEGER filler2;
    LONGINT ioDirID;
    OSType ioFileCreator;
    OSType ioFileType;
    LONGINT ioFiller3;
    LONGINT ioDTLgLen;
    LONGINT ioDTPyLen;
    INTEGER ioFiller4[14];
    LONGINT ioAPPLParID;
} DTPBRec, *DTPBRecPtr, *DTPBPtr;

extern OSErr PBDTGetPath(DTPBPtr dtp);
extern OSErr PBDTOpenInform(DTPBPtr dtp);
extern OSErr PBDTCloseDown(DTPBPtr dtp);
extern OSErr PBDTGetIcon(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTGetIconInfo(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTGetAPPL(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTGetComment(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTAddIcon(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTAddAPPL(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTSetComment(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTRemoveAPPL(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTRemoveComment(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTFlush(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTGetInfo(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTReset(DTPBPtr dtp, BOOLEAN async);
extern OSErr PBDTDelete(DTPBPtr dtp, BOOLEAN async);

#endif
