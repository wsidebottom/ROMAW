/*+===================================================================
  File:      misc.h

  Summary:   Declares misc support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_MISC__)
#define __RSYS_MISC__

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: misc.h 63 2004-12-24 18:19:43Z ctm $
 */

#if !defined(nilhandle_H)
extern HIDDEN_Ptr nilhandle_H;
extern HIDDEN_Ptr dodusesit_H;
extern LONGINT trapvectors;
extern LONGINT hyperlong;
extern LONGINT mathones;
extern INTEGER graphlooksat;
extern LONGINT macwritespace;
extern LONGINT LastSPExtra;
extern LONGINT lastlowglobal;
extern INTEGER MCLKPCmiss1;
extern INTEGER MCLKPCmiss2;
#endif

#define nilhandle (HIDDEN_RAW(nilhandle_H))
#define dodusesit (HIDDEN_RAW(dodusesit_H))

#endif /* !defined(__RSYS_MISC__) */
