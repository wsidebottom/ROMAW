/*+===================================================================
  File:      scrap.h

  Summary:   Declares scrap support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_SCRAP_H__)
#define __RSYS_SCRAP_H__

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: scrap.h 63 2004-12-24 18:19:43Z ctm $
 */

extern void sendsuspendevent(void);
extern void sendresumeevent(boolean_t cvtclip);

#if defined(CYGWIN32)
extern int get_scrap_helper(void* vh, void* lp, int len, boolean_t cvt_rets);
extern int count_char(const char* p, int len, char c);
extern boolean_t we_lost_clipboard(void);
#if defined(SDL)
extern int get_scrap_helper_dib(void* vh, void* lp, size_t length);
extern void put_scrap_helper_dib(void* lp);
#endif
#endif

#endif
