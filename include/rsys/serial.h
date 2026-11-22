/*+===================================================================
  File:      serial.h

  Summary:   Declares serial support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SERIAL_H_)
#define _SERIAL_H_

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: serial.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    PACKED_MEMBER(Ptr, p);
    INTEGER i;
} sersetbuf_t;

extern OSErr ROMlib_serialopen(ParmBlkPtr pbp, DCtlPtr dcp);
extern OSErr ROMlib_serialprime(ParmBlkPtr pbp, DCtlPtr dcp);
extern OSErr ROMlib_serialctl(ParmBlkPtr pbp, DCtlPtr dcp);
extern OSErr ROMlib_serialstatus(ParmBlkPtr pbp, DCtlPtr dcp);
extern OSErr ROMlib_serialclose(ParmBlkPtr pbp, DCtlPtr dcp);

#endif
