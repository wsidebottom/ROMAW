/*+===================================================================
  File:      fileplay.h

  Summary:   ifndef RSYS_FILEPLAY_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_FILEPLAY_H
#define RSYS_FILEPLAY_H
#include "rsys/soundopts.h"

boolean_t ROMlib_sound_resample(uint8*, uint8*, unsigned, unsigned, uint32, uint32, snd_time*, uint8*, snd_time*,
                                snd_time, unsigned);
boolean_t ROMlib_fileplay_busy(SndChannelPtr);
void ROMlib_fileplay_mix(SndChannelPtr, struct hunger_info);
void ROMlib_fileplay_cancel(SndChannelPtr);
void ROMlib_fileplay_reset(void);
void ROMlib_fileplay_poll(void);
OSErr ROMlib_parse_aiff_header(INTEGER refnum, void* description, uint32* frames, uint32* offset);
OSErr ROMlib_parse_snd_header(Handle resource, void* description, uint32* frames, uint32* offset);
#endif
