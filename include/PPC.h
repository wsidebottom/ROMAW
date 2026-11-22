/*+===================================================================
  File:      PPC.h

  Summary:   Declares ppc support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_PPC_H_)
#define _PPC_H_

/* Copyright 1996 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: PPC.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef int16 PPCPortKinds;
typedef int16 PPCLocationKind;

enum {
    ppcNoLocation = 0,
    ppcNBPLocation = 1,
    ppcNBPTypeLocation = 2,
    ppcByCreatorAndType = 1,
    ppcByString = 2,
    noPortErr = -903
};

/* Classic Toolbox records align these Pascal strings on word boundaries. */
typedef struct PACKED EntityName {
    Str32 objStr;
    Byte pad1;
    Str32 typeStr;
    Byte pad2;
    Str32 zoneStr;
    Byte pad3;
} EntityName;

typedef struct PACKED LocationNameRec {
    PPCLocationKind locationKindSelector;

    union {
        EntityName npbEntity;
        Str32 npbType;
    } u;
} LocationNameRec;

typedef struct PACKED PPCPortRec {
    ScriptCode nameScript;
    Str32 name;
    Byte name_padding;

    PPCPortKinds portKindsSelector;

    union {
        Str32 portTypeStr;

        struct PACKED {
            OSType creator;
            OSType type;
        } port;
    } u;

    Byte type_padding;
} PPCPortRec, *PPCPortPtr;

#endif /* !_PPC_H_ */
