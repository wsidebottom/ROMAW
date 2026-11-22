/*+===================================================================
  File:      ScrapMgr.h

  Summary:   Declares scrap mgr support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__SCRAP__)
#define __SCRAP__

#include "ResourceMgr.h"

/*
 * Copyright 1986, 1989, 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: ScrapMgr.h 63 2004-12-24 18:19:43Z ctm $
 */

#define noScrapErr (-100)
#define noTypeErr (-102)

typedef struct PACKED {
    LONGINT scrapSize;
    PACKED_MEMBER(Handle, scrapHandle);
    INTEGER scrapCount;
    INTEGER scrapState;
    PACKED_MEMBER(StringPtr, scrapName);
} ScrapStuff;

typedef ScrapStuff* PScrapStuff;

#if !defined(ScrapHandle_H)
extern HIDDEN_Handle ScrapHandle_H;
extern HIDDEN_StringPtr ScrapName_H;
extern LONGINT ScrapSize;
extern INTEGER ScrapCount;
extern INTEGER ScrapState;
#endif

#define ScrapHandle (HIDDEN_RAW(ScrapHandle_H))
#define ScrapName (HIDDEN_RAW(ScrapName_H))

#if !defined(__STDC__)
extern PScrapStuff InfoScrap();
extern LONGINT UnloadScrap();
extern LONGINT LoadScrap();
extern LONGINT ROMlib_ZeroScrap();
extern LONGINT ZeroScrap();
extern LONGINT PutScrap();
extern LONGINT GetScrap();
#else  /* __STDC__ */
extern pascal trap PScrapStuff C_InfoScrap(void);
extern pascal trap PScrapStuff P_InfoScrap(void);
extern pascal trap LONGINT C_UnloadScrap(void);
extern pascal trap LONGINT P_UnloadScrap(void);
extern pascal trap LONGINT C_LoadScrap(void);
extern pascal trap LONGINT P_LoadScrap(void);
extern LONGINT ROMlib_ZeroScrap(void);
extern pascal trap LONGINT C_ZeroScrap(void);
extern pascal trap LONGINT P_ZeroScrap(void);
extern pascal trap LONGINT C_PutScrap(LONGINT len, ResType rest, Ptr p);
extern pascal trap LONGINT P_PutScrap(LONGINT len, ResType rest, Ptr p);
extern pascal trap LONGINT C_GetScrap(Handle h, ResType rest, LONGINT* off);
extern pascal trap LONGINT P_GetScrap(Handle h, ResType rest, LONGINT* off);
#endif /* __STDC__ */
#endif /* __SCRAP__ */
