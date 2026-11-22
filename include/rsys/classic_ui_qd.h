/*+===================================================================
  File:      classic_ui_qd.h

  Summary:   QuickDraw adapter for the host-independent classic UI drawing module. ifndef ROMAW_CLASSIC_UI_QD_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* QuickDraw adapter for the host-independent classic UI drawing module. */
#ifndef ROMAW_CLASSIC_UI_QD_H
#define ROMAW_CLASSIC_UI_QD_H
#include "classic-ui/classic_ui.h"
#include "QuickDraw.h"

/* The borrowed role table contains native pointers to Toolbox RGBColor values.
   Those values retain their classic byte order and full 16-bit precision. */
ROMAWClassicCanvas romaw_classic_quickdraw(RGBColor* const* roles);
#endif
