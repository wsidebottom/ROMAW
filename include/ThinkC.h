/*+===================================================================
  File:      ThinkC.h

  Summary:   Declares think c support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__THINKCDOTH__)
#define __THINKCDOTH__
/*
 * Copyright 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: ThinkC.h 63 2004-12-24 18:19:43Z ctm $
 */

#if !defined(__STDC__)
extern void CDebugStr();
#else  /* __STDC__ */
extern void CDebugStr(StringPtr p);
#endif /* __STDC__ */

/* DO NOT DELETE THIS LINE */
#if !defined(__STDC__)
extern StringPtr CtoPstr();
extern char* PtoCstr();
extern void DebugStr();
#else  /* __STDC__ */
extern StringPtr CtoPstr(char* str);
extern char* PtoCstr(StringPtr str);
extern pascal trap void C_DebugStr(StringPtr p);
extern pascal trap void P_DebugStr(StringPtr p);
#endif /* __STDC__ */
#endif /* __THINKCDOTH__ */
