/*+===================================================================
  File:      time.h

  Summary:   Declares time support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_TIME_H_)
#define _RSYS_TIME_H_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: time.h 63 2004-12-24 18:19:43Z ctm $
 */

extern struct timeval ROMlib_start_time;

extern unsigned long msecs_elapsed(void);

#if !defined(USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES)
extern QHdr ROMlib_timehead;

#if defined(SYN68K)
extern syn68k_addr_t catchalarm(syn68k_addr_t pc, void* unused);
#endif
#endif

#endif /* _RSYS_TIME_H_ */
