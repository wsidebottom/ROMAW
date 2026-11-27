/*+===================================================================
  File:      pattern_origin.h

  Summary:   ifndef RSYS_PATTERN_ORIGIN_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_PATTERN_ORIGIN_H
#define RSYS_PATTERN_ORIGIN_H
#include "QuickDraw.h"
/* Native coordinates internally; Toolbox output records remain big endian. */
Point ROMlib_pattern_origin(void);
void ROMlib_QDGetPatternOrigin(Point* origin);
void ROMlib_QDSetPatternOrigin(Point origin);
#endif
