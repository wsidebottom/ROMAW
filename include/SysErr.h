/*+===================================================================
  File:      SysErr.h

  Summary:   Declares sys err support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SYSERR_H_)
#define _SYSERR_H_

/*
 * Copyright 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: SysErr.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef enum { EXIST_YES = 0, EXIST_NO = 0xFF } exist_enum_t;

#if !defined(DSAlertTab_H)
extern HIDDEN_Ptr DSAlertTab_H;
extern Rect DSAlertRect;
extern Byte WWExist;
extern Byte QDExist;
#endif

#define DSAlertTab (HIDDEN_RAW(DSAlertTab_H))

extern char syserr_msg[];

extern pascal void C_SysError(short errorcode);

#endif /* !_SYSERR_H_ */
