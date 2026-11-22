/*+===================================================================
  File:      crc.h

  Summary:   Declares crc support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/tests/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _crc_h_
#define _crc_h_

extern unsigned short compute_crc(unsigned char* data, long length, unsigned short seed);

#endif /* Not _crc_h_ */
