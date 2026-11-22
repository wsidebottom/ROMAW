/*+===================================================================
  File:      CommTool.h

  Summary:   Declares comm tool support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__COMMTOOL__)
#define __COMMTOOL__

/*
 * Copyright 1999 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: CommTool.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    PACKED_MEMBER(QElemPtr, qLink);
    INTEGER qType;
    INTEGER crmVersion;
    LONGINT crmPrivate;
    INTEGER crmReserved;
    LONGINT crmDeviceType;
    LONGINT crmDeviceID;
    LONGINT crmAttributes;
    LONGINT crmStatus;
    LONGINT crmRefCon;
} CRMRec, *CRMRecPtr;

typedef OSErr CRMErr;

enum { crmGenericError = -1, crmNoErr = 0 };

enum { curCRMVersion = 1 };

enum { crmType = 9 };

enum { crmRecVersion = 1 };

enum { curCRMSerRecVer = 0 };

enum { crmSerialDevice = 1 };

typedef struct PACKED {
    INTEGER version;
    PACKED_MEMBER(StringHandle, inputDriverName);
    PACKED_MEMBER(StringHandle, outputDriverName);
    PACKED_MEMBER(StringHandle, name);
    PACKED_MEMBER(Handle, deviceIcon);
    LONGINT ratedSpeed;
    LONGINT maxSpeed;
    LONGINT reserved;
} CRMSerialRecord, *CRMSerialPtr;

extern INTEGER CRMGetCRMVersion(void);

extern QHdrPtr CRMGetHeader(void);

extern void CRMInstall(QElemPtr);

extern OSErr CRMRemove(QElemPtr);

extern QElemPtr CRMSearch(QElemPtr);

extern CRMErr InitCRM(void);

#endif
