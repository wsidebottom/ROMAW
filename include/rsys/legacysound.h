/*+===================================================================
  File:      legacysound.h

  Summary:   Classic .Sound driver, shared by Device Manager and the audio mixer. ifndef RSYS_LEGACYSOUND_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic .Sound driver, shared by Device Manager and the audio mixer. */
#ifndef RSYS_LEGACYSOUND_H
#define RSYS_LEGACYSOUND_H
#include "FileMgr.h"
#include "rsys/soundopts.h"
extern OSErr ROMlib_legacy_sound_write(ParmBlkPtr pb, BOOLEAN async);
extern OSErr ROMlib_legacy_sound_control(ParmBlkPtr pb, BOOLEAN async);
extern void ROMlib_legacy_sound_reset(boolean_t complete);
extern boolean_t ROMlib_legacy_sound_mix(struct hunger_info info);
#endif
