/*+===================================================================
  File:      snth5.c

  Summary:   Implements snth5 support for Macintosh sound playback and conversion.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/sound; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "SoundMgr.h"
#include "rsys/soundopts.h"

/*
 * It's not really clear how an actual synthesizer is expected to work
 * with the Sound Manager's queues.  In this implementation we start
 * the relavent command playing and see that the ROMLIB_soundcomplete()
 * function is called when we're done.
 */

typedef pascal void (*callbackfp)(SndChannelPtr, SndCommand*);

P3(PUBLIC pascal, BOOLEAN, snth5, SndChannelPtr, chanp, SndCommand*, cmdp, ModifierStubPtr, mp)
{
    return FALSE;
}
