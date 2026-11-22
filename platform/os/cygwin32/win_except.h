/*+===================================================================
  File:      win_except.h

  Summary:   Declares win except support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__WIN_EXCEPT_H__)
#define __WIN_EXCEPT_H__

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

extern void install_exception_handler(void);
extern void uninstall_exception_handler(void);

#endif
