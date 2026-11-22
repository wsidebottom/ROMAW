/*+===================================================================
  File:      crc.h

  Summary:   Declares crc support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_CRC_H_)
#define _RSYS_CRC_H_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: crc.h 63 2004-12-24 18:19:43Z ctm $
 */

extern short getthecrc(ResType type, long id);
extern unsigned short ROMlib_crcccitt(unsigned char* data, long length);

#endif /* _RSYS_CRC_H_ */
